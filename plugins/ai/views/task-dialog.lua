-- The form that creates or edits a task: its identity, what it runs, where it runs, its schedule and its prompt, each checked in that order.
local fs = require("fs")
local cron = include("cron")
local engine = include("engine")
local preferences = include("preferences")

local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

local taskDialog = {}

local hour = 3600
local defaultIntervalMinutes = 60

local function trimmed(value)
    return (value or ""):match("^%s*(.-)%s*$")
end

-- The first rule a form breaks is named, so the reader fixes one thing at a time.
local function validate(values)
    if trimmed(values.title) == "" then
        return "ai.validation.title"
    end

    if values.executionKind == "agent" and trimmed(values.prompt) == "" then
        return "ai.validation.prompt"
    end

    if values.executionKind == "command" and trimmed(values.command) == "" then
        return "ai.validation.command"
    end

    local workdir = trimmed(values.workdir)
    local required = values.executionKind == "command" or workdir ~= ""
    local folder = required and workpane.files.absolute(workdir) and fs.stat(workdir):await() or nil

    if required and (folder == nil or not folder.isDir) then
        return "ai.validation.workdir"
    end

    if values.executionKind == "agent" and preferences.agent(values.agentId or "") == nil then
        return "ai.validation.agent-missing"
    end

    local issue = trimmed(values.issueUrl)

    if issue ~= "" and not issue:match("^https?://[^/%s]+") then
        return "ai.validation.issue-url"
    end

    if values.schedule ~= nil and values.schedule.kind == "once" and (cron.epoch(values.schedule.onceAtUtc) or 0) <= os.time() then
        return "ai.validation.schedule-past"
    end

    if values.schedule ~= nil and values.schedule.kind == "cron" and not pcall(cron.parse, values.schedule.cronExpression) then
        return "ai.validation.cron"
    end

    return nil
end

local function row(labelKey, control, hintKey)
    return ui.formField({ label = text(labelKey), hint = hintKey ~= nil and text(hintKey) or nil }, control)
end

function taskDialog.open(workspaceId, task)
    local execution = preferences.execution()
    local agents = preferences.agents()
    local agentOptions = {}

    for index, agent in ipairs(agents) do
        agentOptions[index] = { value = agent.id, text = agent.name }
    end

    local schedule = task ~= nil and task.schedule or nil
    local onceAt = schedule ~= nil and schedule.kind == "once" and cron.epoch(schedule.onceAtUtc) or (os.time() + hour)
    local zone = workpane.time.zone()
    local fields = {
        title = ui.textField({ value = task ~= nil and task.title or "" }),
        description = ui.textField({ value = task ~= nil and task.description or "" }),
        issueUrl = ui.textField({ value = task ~= nil and task.issueUrl or "", placeholder = text("ai.task.issue-url-placeholder") }),
        kind = ui.combo({ value = task ~= nil and task.executionKind or "agent", sorted = true, width = 240, options = { { value = "agent", text = text("ai.task.kind-agent") }, { value = "command", text = text("ai.task.kind-command") } } }),
        agent = ui.combo({ value = task ~= nil and preferences.agent(task.agentId) ~= nil and task.agentId or (agents[1] ~= nil and agents[1].id or ""), options = agentOptions, sorted = true, width = 320 }),
        workdir = ui.textField({ value = task ~= nil and task.workdir or workpane.system.home(), grow = 1 }),
        command = ui.textField({ value = task ~= nil and task.command or "", monospace = true }),
        timeout = ui.numberField({ value = task ~= nil and task.commandTimeoutSeconds or execution.commandTimeoutSeconds, minimum = 0, maximum = 86400, step = 1, decimals = 0, width = 160, tooltip = text("ai.task.unlimited-hint") }),
        scheduleKind = ui.combo({ value = schedule ~= nil and schedule.kind or "none", width = 240, options = { { value = "none", text = text("ai.schedule.none") }, { value = "once", text = text("ai.schedule.once") }, { value = "interval", text = text("ai.schedule.interval") }, { value = "cron", text = text("ai.schedule.cron") } } }),
        once = ui.dateTimeField({ mode = "datetime", value = cron.localText(onceAt, zone), width = 240 }),
        interval = ui.numberField({ value = schedule ~= nil and schedule.kind == "interval" and schedule.intervalSeconds // 60 or defaultIntervalMinutes, minimum = 1, maximum = 100000, step = 1, decimals = 0, width = 160, tooltip = text("ai.schedule.minutes") }),
        cron = ui.textField({ value = schedule ~= nil and schedule.cronExpression or "", placeholder = "0 3 * * 1", width = 240, monospace = true }),
        prompt = ui.textArea({ value = task ~= nil and task.prompt or "", placeholder = text("ai.task.prompt-placeholder"), rows = 14 }),
    }
    local problem = ui.alert({ text = "", visible = false })
    local dialog

    local browse = ui.button({ icon = "folder", variant = "icon", tooltip = text("ai.task.choose-workdir"), onClick = function()
        local chosen = workpane.await(workpane.dialogs.selectFolder({ title = translate("ai.task.choose-workdir"), initial = trimmed(fields.workdir:get("value")) }))

        if chosen ~= nil then
            fields.workdir:set({ value = chosen })
        end
    end })

    local rows = {
        agent = row("ai.task.agent", fields.agent),
        command = row("ai.task.command", fields.command),
        timeout = row("ai.task.command-timeout", fields.timeout, "ai.task.unlimited-hint"),
        once = row("ai.schedule.once", fields.once),
        interval = row("ai.schedule.interval", fields.interval),
        cron = row("ai.schedule.cron", fields.cron),
    }

    -- The rows of the other kind and the other schedules hide, so the form asks only what applies.
    local function refresh()
        local kind = fields.kind:get("value")
        local scheduleKind = fields.scheduleKind:get("value")
        rows.agent:set({ visible = kind == "agent" })
        rows.command:set({ visible = kind == "command" })
        rows.timeout:set({ visible = kind == "command" })
        rows.once:set({ visible = scheduleKind == "once" })
        rows.interval:set({ visible = scheduleKind == "interval" })
        rows.cron:set({ visible = scheduleKind == "cron" })
    end

    fields.kind:on("change", refresh)
    fields.scheduleKind:on("change", refresh)

    local function values()
        local scheduleKind = fields.scheduleKind:get("value")
        local chosen

        if scheduleKind == "once" then
            local moment = cron.localMoment(fields.once:get("value") or "", zone)
            chosen = { kind = "once", onceAtUtc = moment ~= nil and cron.stamp(moment) or "" }
        elseif scheduleKind == "interval" then
            chosen = { kind = "interval", intervalSeconds = math.tointeger(fields.interval:get("value")) * 60 }
        elseif scheduleKind == "cron" then
            chosen = { kind = "cron", cronExpression = trimmed(fields.cron:get("value")) }
        end

        return { id = task ~= nil and task.id or nil, workspaceId = workspaceId, title = fields.title:get("value") or "", description = fields.description:get("value") or "", issueUrl = fields.issueUrl:get("value") or "", executionKind = fields.kind:get("value"), agentId = fields.agent:get("value") or "", workdir = fields.workdir:get("value") or "", command = fields.command:get("value") or "", commandTimeoutSeconds = math.tointeger(fields.timeout:get("value")) or 0, prompt = fields.prompt:get("value") or "", schedule = chosen }
    end

    local pages

    -- The field a rule names takes the keyboard, on the page that holds it, so the reader fixes it at once.
    local culprits = { ["ai.validation.title"] = "title", ["ai.validation.prompt"] = "prompt", ["ai.validation.command"] = "command", ["ai.validation.workdir"] = "workdir", ["ai.validation.agent-missing"] = "agent", ["ai.validation.issue-url"] = "issueUrl", ["ai.validation.schedule-past"] = "once", ["ai.validation.cron"] = "cron" }

    local function refuse(key)
        local culprit = culprits[key]
        problem:set({ text = text(key), visible = true })

        if culprit ~= nil then
            pages:set({ current = culprit == "prompt" and "prompt" or "general" })
            fields[culprit]:command("focus")
        end
    end

    local function save()
        local chosen = values()
        local refused = validate(chosen)

        if refused ~= nil then
            refuse(refused)
            return
        end

        local saved, failure = pcall(engine.saveTask, chosen)

        if not saved then
            refuse(type(failure) == "table" and failure.code == "ai_tasks_schedule_once_past" and "ai.validation.schedule-past" or "ai.error.task-save")
            return
        end

        dialog:close("save")
    end

    local general = ui.column({ spacing = 12 }, {
        row("ai.task.title", fields.title),
        row("ai.task.description", fields.description),
        row("ai.task.issue-url", fields.issueUrl),
        row("ai.task.execution-kind", fields.kind),
        rows.agent,
        row("ai.task.workdir", ui.row({ spacing = 6 }, { fields.workdir, browse })),
        rows.command,
        rows.timeout,
        ui.sectionTitle({ text = text("ai.task.schedule") }),
        row("ai.schedule.type", fields.scheduleKind),
        rows.once,
        rows.interval,
        rows.cron,
    })
    local promptPage = ui.column({ spacing = 12 }, { ui.sectionTitle({ text = text("ai.task.prompt") }), fields.prompt })
    pages = ui.tabs({ items = { { id = "general", text = text("ai.task.tab-general") }, { id = "prompt", text = text("ai.task.tab-prompt") } }, current = "general" }, { general, promptPage })

    refresh()
    dialog = workpane.dialogs.custom({ title = translate(task ~= nil and "ai.task.edit" or "ai.task.add"), width = 780, content = ui.column({ spacing = 12 }, { pages, problem }), buttons = {
        { id = "cancel", text = translate("ai.dialog.cancel") },
        { id = "save", text = translate("ai.dialog.save"), variant = "primary", closes = false },
    }, onButton = function(button)
        if button == "save" then
            save()
        end
    end })

    fields.title:command("focus")
    workpane.await(dialog)
end

return taskDialog
