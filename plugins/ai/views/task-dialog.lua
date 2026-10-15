-- The form that creates or edits a task: its identity, what it runs, where it runs, its schedule and its prompt, each checked in that order.
local cron = include("cron")
local engine = include("engine")
local filesystem = include("filesystem")
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
    local remote = values.hostKind == "ssh"
    local required = remote or values.executionKind == "command" or workdir ~= ""
    local absolute = workpane.files.paths(values.hostPlatform).absolute(workdir)
    local folder = required and not remote and absolute and workpane.files.stat(workdir):await() or nil

    if required and (not absolute or not remote and (folder == nil or not folder.isDir)) then
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

    local profiles = {}
    local hostOptions

    local function availableHosts(selected)
        local known = {}
        local options = { { value = "local", text = text("ai.task.host-local") } }

        for _, profile in ipairs(workpane.await(filesystem.profiles())) do
            known[profile.profile] = profile
            options[#options + 1] = { value = "ssh:" .. profile.profile, text = profile.name }
        end

        if selected ~= "local" and known[selected:sub(5)] == nil then
            options[#options + 1] = { value = selected, text = selected:sub(5) }
        end

        return options, known
    end

    hostOptions, profiles = availableHosts(task ~= nil and task.hostKind == "ssh" and "ssh:" .. task.hostProfile or "local")
    local schedule = task ~= nil and task.schedule or nil
    local onceAt = schedule ~= nil and schedule.kind == "once" and cron.epoch(schedule.onceAtUtc) or (os.time() + hour)
    local zone = workpane.time.zone()
    local fields = {
        title = ui.textField({ value = task ~= nil and task.title or "" }),
        description = ui.textField({ value = task ~= nil and task.description or "" }),
        issueUrl = ui.textField({ value = task ~= nil and task.issueUrl or "", placeholder = text("ai.task.issue-url-placeholder") }),
        kind = ui.combo({ value = task ~= nil and task.executionKind or "agent", sorted = true, width = 240, options = { { value = "agent", text = text("ai.task.kind-agent") }, { value = "command", text = text("ai.task.kind-command") } } }),
        agent = ui.combo({ value = task ~= nil and preferences.agent(task.agentId) ~= nil and task.agentId or (agents[1] ~= nil and agents[1].id or ""), options = agentOptions, sorted = true, width = 320 }),
        host = ui.combo({ value = task ~= nil and task.hostKind == "ssh" and "ssh:" .. task.hostProfile or "local", options = hostOptions, width = 320 }),
        workdir = ui.pathField({ value = task ~= nil and task.workdir or workpane.system.home(), mode = "folder", pickerTitle = text("ai.task.choose-workdir"), grow = 1 }),
        command = ui.textField({ value = task ~= nil and task.command or "", monospace = true }),
        timeout = ui.numberField({ value = task ~= nil and task.commandTimeoutSeconds or execution.commandTimeoutSeconds, minimum = 0, maximum = 86400, step = 1, decimals = 0, width = 160, tooltip = text("ai.task.unlimited-hint") }),
        scheduleKind = ui.combo({ value = schedule ~= nil and schedule.kind or "none", width = 240, options = { { value = "none", text = text("ai.schedule.none") }, { value = "once", text = text("ai.schedule.once") }, { value = "interval", text = text("ai.schedule.interval") }, { value = "cron", text = text("ai.schedule.cron") } } }),
        once = ui.dateTimeField({ mode = "datetime", value = cron.localText(onceAt, zone), width = 240 }),
        interval = ui.numberField({ value = schedule ~= nil and schedule.kind == "interval" and schedule.intervalSeconds // 60 or defaultIntervalMinutes, minimum = 1, maximum = 100000, step = 1, decimals = 0, width = 160, tooltip = text("ai.schedule.minutes") }),
        cron = ui.textField({ value = schedule ~= nil and schedule.cronExpression or "", placeholder = "0 3 * * 1", width = 240, monospace = true }),
        prompt = ui.textArea({ value = task ~= nil and task.prompt or "", placeholder = text("ai.task.prompt-placeholder"), rows = 14 }),
    }
    local problem = ui.alert({ text = "", visible = false })
    local remoteStatus = ui.alert({ text = text("ai.task.host-disconnected"), visible = false })
    local dialog
    local live = true
    local busy = false
    local committing = false

    local function buttons()
        return {
            { id = "cancel", text = translate("ai.dialog.cancel"), closes = false, enabled = not committing },
            { id = "save", text = translate("ai.dialog.save"), variant = "primary", closes = false, enabled = not busy },
        }
    end

    fields.workdir:on("browse", function(event)
        local host = fields.host:get("value")

        if host == "local" then
            return workpane.await(workpane.dialogs.selectFolder({ title = event.title, initial = event.value }))
        end

        local profile = profiles[host:sub(5)]

        if profile == nil or profile.state ~= "connected" then
            error({ code = "ssh_connection_closed", message = "The selected SSH profile is not connected", detail = "" }, 0)
        end

        return workpane.await(workpane.dialogs.selectFolder({ title = event.title, initial = event.value, isCurrent = event.isCurrent, workspace = { open = "ssh.workspace.open", request = "ssh.workspace.request", close = "ssh.workspace.close", changed = "remote-ssh.workspace.changed", profile = profile.profile, session = profile.session, closedCodes = { "ssh_connection_closed", "ssh_workspace_unavailable" } } }))
    end)

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
        local host = fields.host:get("value")
        local remote = host ~= "local"
        local profile = remote and profiles[host:sub(5)] or nil
        fields.workdir:set({ browseEnabled = not remote or profile ~= nil and profile.state == "connected" and workpane.capabilities.available("ssh.workspace.open") })
        remoteStatus:set({ visible = remote and (profile == nil or profile.state ~= "connected") })
    end

    fields.host:on("change", function()
        local host = fields.host:get("value")
        local profile = profiles[host:sub(5)]
        fields.workdir:set({ value = host == "local" and workpane.system.home() or profile ~= nil and profile.root or "" })
        refresh()
    end)

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

        local host = fields.host:get("value")
        local profileId = host ~= "local" and host:sub(5) or ""
        local profile = profiles[profileId]

        local platform = host == "local" and workpane.app.platform or profile ~= nil and profile.platform or task ~= nil and task.hostKind == "ssh" and task.hostProfile == profileId and task.hostPlatform or ""

        return { hostKind = host == "local" and "local" or "ssh", hostProfile = profileId, hostPlatform = platform, id = task ~= nil and task.id or nil, workspaceId = workspaceId, title = fields.title:get("value") or "", description = fields.description:get("value") or "", issueUrl = fields.issueUrl:get("value") or "", executionKind = fields.kind:get("value"), agentId = fields.agent:get("value") or "", workdir = fields.workdir:get("value") or "", command = fields.command:get("value") or "", commandTimeoutSeconds = math.tointeger(fields.timeout:get("value")) or 0, prompt = fields.prompt:get("value") or "", schedule = chosen }
    end

    local pages

    -- The field a rule names takes the keyboard, on the page that holds it, so the reader fixes it at once.
    local culprits = { ["ai.validation.host"] = "host", ["ai.validation.remote-workdir"] = "workdir", ["ai.validation.title"] = "title", ["ai.validation.prompt"] = "prompt", ["ai.validation.command"] = "command", ["ai.validation.workdir"] = "workdir", ["ai.validation.agent-missing"] = "agent", ["ai.validation.issue-url"] = "issueUrl", ["ai.validation.schedule-past"] = "once", ["ai.validation.cron"] = "cron" }

    local function refuse(key)
        if not live then
            return
        end

        local culprit = culprits[key]
        problem:set({ text = text(key), visible = true })

        if culprit ~= nil then
            pages:set({ current = culprit == "prompt" and "prompt" or "general" })
            fields[culprit]:command("focus")
        end
    end

    local function prepare()
        local chosen = values()

        if chosen.hostKind == "ssh" and (profiles[chosen.hostProfile] == nil or chosen.hostPlatform == "") then
            return "ai.validation.host"
        end

        local refused = validate(chosen)

        if refused ~= nil then
            return refused
        end

        if not live then
            return nil
        end

        local profile = profiles[chosen.hostProfile]

        if profile ~= nil and profile.state == "connected" then
            local function inspect()
                return workpane.await(filesystem.open(chosen))
            end

            local inspected, backend = pcall(inspect)

            if not inspected then
                return "ai.validation.remote-workdir"
            end

            workpane.await(backend.close())
        end

        if not live then
            return nil
        end

        committing = true
        dialog:setButtons(buttons())
        local saved, failure = pcall(engine.saveTask, chosen)

        if not saved then
            return type(failure) == "table" and failure.code == "ai_tasks_schedule_once_past" and "ai.validation.schedule-past" or "ai.error.task-save"
        end

        dialog:close("save")

        return true
    end

    local function save()
        if busy or not live then
            return
        end

        busy = true
        dialog:setButtons(buttons())
        local prepared, failure = pcall(prepare)
        busy = false
        committing = false

        if not live then
            return
        end

        if prepared and failure == true then
            live = false
            return
        end

        if not prepared or failure ~= nil then
            refuse(prepared and failure or "ai.error.task-save")
        end

        dialog:setButtons(buttons())
    end

    local general = ui.column({ spacing = 12 }, {
        row("ai.task.title", fields.title),
        row("ai.task.description", fields.description),
        row("ai.task.issue-url", fields.issueUrl),
        row("ai.task.execution-kind", fields.kind),
        rows.agent,
        row("ai.task.host", fields.host),
        remoteStatus,
        row("ai.task.workdir", fields.workdir),
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
    dialog = workpane.dialogs.custom({ title = translate(task ~= nil and "ai.task.edit" or "ai.task.add"), width = 780, content = ui.column({ spacing = 12 }, { pages, problem }), buttons = buttons(), onButton = function(button)
        if button == "save" then
            save()
        elseif button == "cancel" and not committing then
            live = false
            dialog:close("cancel")
        end
    end })

    local hostRevision = 0

    local function refreshHosts()
        hostRevision = hostRevision + 1
        local revision = hostRevision
        local options, known = availableHosts(fields.host:get("value"))

        if live and revision == hostRevision then
            profiles = known
            local selected = fields.host:get("value")

            if selected ~= "local" and known[selected:sub(5)] == nil then
                local found = false

                for _, option in ipairs(options) do
                    found = found or option.value == selected
                end

                if not found then
                    options[#options + 1] = { value = selected, text = selected:sub(5) }
                end
            end

            fields.host:set({ options = options })
            refresh()
        end
    end

    local unsubscribe = workpane.events.subscribe("remote-ssh.connections.changed", refreshHosts)
    local unwatch = workpane.capabilities.watch("ssh.connections.list", refreshHosts)
    fields.title:command("focus")
    workpane.await(dialog)
    live = false
    unsubscribe()
    unwatch()
end

return taskDialog
