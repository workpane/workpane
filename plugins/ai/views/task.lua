-- The surface of one task: its conversation, the runs it recorded with their logs and output, and the buttons that stop it or clear its conversation.
local async = require("async")
local catalog = include("catalog")
local codec = include("codec")
local engine = include("engine")
local store = include("store")
local conversationView = include("views/conversation")
local status = include("views/status")

local ui = workpane.ui
local text = workpane.i18n.text
local number = workpane.i18n.number
local translate = workpane.i18n.translate

local taskView = {}

local listed = 100
local refreshDelayMilliseconds = 200
local payloadKinds = { ["response-received"] = true, ["tool-called"] = true, ["tool-returned"] = true, compacted = true }

-- An exchanged payload is shown whole in its own window, indented when it is JSON.
local function showPayload(kind, detail)
    local body = detail:match("^[^\n]*\n(.*)$")
    local candidate = body ~= nil and codec.read(body) or codec.read(detail)
    local shown = candidate ~= nil and type(candidate) == "table" and ((body ~= nil and (detail:match("^[^\n]*") .. "\n") or "") .. codec.pretty(candidate)) or detail
    local viewer = workpane.dialogs.custom({ title = translate("ai.log-kind." .. kind), width = 820, content = ui.codeEditor({ value = shown, language = candidate ~= nil and "json" or "none", readOnly = true, height = 480 }), buttons = { { id = "close", text = translate("ai.dialog.close") } } })
    workpane.await(viewer)
end

local function outputOf(execution)
    if execution == nil then
        return text("ai.output.none")
    end

    if execution.status == "running" then
        return text("ai.output.running")
    end

    if execution.content ~= "" then
        return execution.content
    end

    if execution.status == "failed" then
        return execution.error_message ~= "" and execution.error_message or text("ai.output.failed")
    end

    if execution.status == "cancelled" then
        return text("ai.output.cancelled")
    end

    return execution.stop_reason == "answered" and text("ai.output.empty") or text("ai.stop-reason." .. execution.stop_reason)
end

function taskView.open(taskId, tab)
    local task = engine.task(taskId)

    if task == nil then
        return
    end

    local agentTask = task.executionKind == "agent"
    local executions = {}
    local selected
    local closed = false
    local pendingRefresh = false
    local executionTable = ui.table({ columns = {
        { id = "started", title = text("ai.execution.started"), width = 150 },
        { id = "status", title = text("ai.execution.status"), width = 110 },
        { id = "tokens", title = text("ai.execution.tokens"), width = 150 },
        { id = "cost", title = text("ai.execution.cost"), width = 100 },
        { id = "finish", title = text("ai.execution.finish-reason"), width = "stretch" },
        { id = "error", title = text("ai.execution.error"), width = 200 },
    }, rows = {}, selection = "accent", grow = 1 })
    local logTable = ui.table({ columns = {
        { id = "time", title = text("ai.log.timestamp"), width = 150 },
        { id = "level", title = text("ai.log.level"), width = 90 },
        { id = "event", title = text("ai.log.event"), width = 220 },
        { id = "detail", title = text("ai.log.detail"), width = "stretch" },
    }, rows = {}, selection = "subtle", grow = 1 })
    local output = ui.markdown({ text = "", breaks = true })
    local count = ui.label({ text = "", style = "caption", color = "text-muted" })
    local phase = ui.label({ text = "", style = "caption", color = "text-muted" })
    local busy = ui.busyIndicator({ running = true, size = 14, visible = false })
    local stop = ui.button({ text = text("ai.task.stop"), variant = "destructive", visible = false })
    local reset = ui.button({ text = text("ai.conversation.reset"), visible = agentTask })
    local logs = {}

    local function renderLogs()
        local rows = {}

        for index, entry in ipairs(logs) do
            local payload = payloadKinds[entry.kind] == true
            rows[index] = { id = entry.id, cells = { workpane.time.localPresentation(entry.timestamp_utc), { text = text("ai.log-level." .. entry.level), tone = entry.level == "error" and "danger" or entry.level == "warning" and "warning" or nil }, { text = text("ai.log-kind." .. entry.kind) }, payload and { text = text("ai.log.open-payload"), muted = true } or entry.detail }, actions = payload and { { id = "payload", icon = "external-link", tooltip = text("ai.log.open-payload") } } or nil }
        end

        logTable:set({ rows = rows })
    end

    local function select(executionId)
        selected = executionId
        local chosen

        for _, execution in ipairs(executions) do
            chosen = execution.id == executionId and execution or chosen
        end

        executionTable:set({ selected = executionId or "" })
        output:set({ text = outputOf(chosen) })
        logs = executionId ~= nil and workpane.await(store.logs(executionId, listed)) or {}
        renderLogs()
    end

    -- The runs are read again whenever the task reports activity, keeping the selected one.
    local function refresh()
        executions = workpane.await(store.executions(taskId, listed))
        local rows = {}
        local present = false

        for index, execution in ipairs(executions) do
            local cost = catalog.cost(execution.provider_id, execution.model_id, execution.input_tokens, execution.output_tokens)
            present = present or execution.id == selected
            rows[index] = { id = execution.id, cells = { workpane.time.localPresentation(execution.started_at_utc), { text = text("ai.status." .. execution.status) }, agentTask and { text = text("ai.execution.token-usage", number(execution.input_tokens, 0), number(execution.output_tokens, 0)) } or "", cost ~= nil and { text = text("ai.execution.cost-amount", number(cost, 4)) } or "", execution.finish_reason, execution.error_message } }
        end

        -- The rows and the selection change in one patch, because rows that drop the selected run would otherwise be refused.
        local chosen = present and selected or (executions[1] ~= nil and executions[1].id or nil)
        executionTable:set({ rows = rows, selected = chosen or "" })
        count:set({ text = text("ai.execution.count", number(#executions, 0)) })
        local running = engine.runState(taskId) == "running"
        busy:set({ visible = running })
        phase:set({ text = running and status.phase(taskId) or "", visible = running })
        stop:set({ visible = engine.runState(taskId) ~= "idle" })
        reset:set({ enabled = engine.runState(taskId) == "idle" })
        select(chosen)
    end

    executionTable:on("select", function(event)
        select(event.id)
    end)

    logTable:on("action", function(event)
        for _, entry in ipairs(logs) do
            if entry.id == event.id then
                showPayload(entry.kind, entry.detail)
            end
        end
    end)

    stop:on("click", function()
        local stopped = pcall(engine.stopTask, taskId)

        if not stopped then
            workpane.notify.error(translate("ai.error.title"), translate("ai.error.stop"))
        end
    end)

    reset:on("click", function()
        local confirmed = workpane.await(workpane.dialogs.confirm({ title = translate("ai.conversation.reset-title"), message = translate("ai.conversation.reset-message"), detail = translate("ai.conversation.reset-detail"), confirmText = translate("ai.conversation.reset") }))

        if confirmed then
            local cleared = pcall(engine.resetConversation, taskId)

            if not cleared then
                workpane.notify.error(translate("ai.plugin.title"), translate("ai.error.conversation-save"))
            end

            refresh()
        end
    end)

    local items = {}
    local pages = {}
    local forget

    if agentTask then
        local chatPage
        chatPage, forget = conversationView.build(taskId)
        items[#items + 1] = { id = "chat", text = text("ai.task.tab-chat") }
        pages[#pages + 1] = chatPage
    end

    items[#items + 1] = { id = "executions", text = text("ai.task.tab-executions") }
    pages[#pages + 1] = ui.column({ padding = 8 }, { executionTable })

    if agentTask then
        items[#items + 1] = { id = "logs", text = text("ai.task.tab-logs") }
        pages[#pages + 1] = ui.column({ padding = 8 }, { logTable })
    end

    items[#items + 1] = { id = "output", text = text("ai.task.tab-output") }
    pages[#pages + 1] = ui.scroll({ padding = 12 }, output)
    local current = (tab == "chat" and agentTask) and "chat" or "executions"

    -- A task whose runs cannot be read or shown leaves nothing listening behind it.
    local opened, dialog = pcall(function()
        refresh()

        local content = ui.column({ spacing = 10, height = 560 }, {
            ui.row({ spacing = 10 }, { busy, phase, ui.spacer({ grow = 1 }), count, reset, stop }),
            ui.tabs({ items = items, current = current, grow = 1 }, pages),
        })

        return workpane.dialogs.custom({ title = task.title, width = 820, content = content, buttons = { { id = "close", text = translate("ai.dialog.close") } } })
    end)

    if not opened then
        if forget ~= nil then
            forget()
        end

        error(dialog, 0)
    end

    local forgetRun = engine.listen(function(kind, changed)
        if closed or changed ~= taskId or pendingRefresh or (kind ~= "run" and kind ~= "activity") then
            return
        end

        pendingRefresh = true

        workpane.task(function()
            async.sleep(refreshDelayMilliseconds):await()
            pendingRefresh = false

            if not closed then
                refresh()
            end
        end)
    end)

    local _, refused = dialog:await()
    closed = true
    forgetRun()

    if forget ~= nil then
        forget()
    end

    if refused ~= nil then
        error(refused, 0)
    end
end

return taskView
