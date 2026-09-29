-- Runs the tasks of the board: workspaces and tasks in memory and in storage, the queue, one execution per running task, the agent loop, commands, schedules and the MCP servers every agent reaches.
local async = require("async")
local crypto = require("crypto")
local process = require("process")
local catalog = include("catalog")
local chat = include("chat")
local codec = include("codec")
local commands = include("commands")
local completion = include("completion")
local connections = include("connections")
local cron = include("cron")
local mcp = include("mcp")
local messages = include("messages")
local preferences = include("preferences")
local prompt = include("prompt")
local protocols = include("protocols")
local resources = include("resources")
local store = include("store")
local tools = include("tools")
local window = include("window")

local translate = workpane.i18n.translate

local engine = {}

local pageSize = 100
local cachedPages = 2
local streamedMilliseconds = 50
local pollMilliseconds = 20
local languages = { en = "English", pt = "Portuguese" }

local state = { workspaces = {}, tasks = {}, queue = {}, active = {}, phases = {}, outcomes = {}, conversations = {}, older = {}, loadingOlder = {}, paged = {}, sequences = {}, logSequences = {}, servers = {}, listeners = {}, streamed = {}, starting = {}, operatingSystem = "", stopping = false, scheduling = false, scheduleChanged = nil }

local wakeScheduler

-- A change of the tasks may change their schedules, so it also wakes the scheduler.
local function announce(kind, taskId)
    for _, entry in ipairs({ table.unpack(state.listeners) }) do
        entry.listener(kind, taskId)
    end

    if kind == "tasks" then
        wakeScheduler()
    end
end

-- A listener hears every announcement, and the answer removes it, which a dialog does as it closes.
function engine.listen(listener)
    local entry = { listener = listener }
    state.listeners[#state.listeners + 1] = entry

    return function()
        for index, existing in ipairs(state.listeners) do
            if existing == entry then
                table.remove(state.listeners, index)
                return
            end
        end
    end
end

local function refuse(code, message, detail)
    error({ code = code, message = message, detail = detail or "" }, 0)
end

-- The error carries the diagnostic that belongs in the log, and the message is the sentence the reader sees.
-- A failure a run ends with reaches the reader in their language, with the text a provider gave as its argument, while its English message stays in the log.
local failureKeys = {
    ai_api_key_missing = { key = "ai.error.api-key-missing" },
    ai_secret_environment_missing = { key = "ai.error.secret-missing", argument = "detail" },
    ai_stream_invalid = { key = "ai.error.stream-invalid" },
    ai_stream_idle = { key = "ai.error.stream-idle" },
    ai_stream_too_large = { key = "ai.error.stream-too-large" },
    ai_request_failed = { key = "ai.error.request-failed", argument = "message" },
    ai_provider_error = { key = "ai.error.request-failed", argument = "message" },
    ai_tools_unsupported = { key = "ai.error.tools-unsupported" },
}

-- The failures of command line agents and of commands already carry a message in the language of the reader or the output of their program.
local translatedFailures = { ai_cli_provider_invalid = true, ai_cli_workdir_required = true, ai_cli_program_missing = true, ai_cli_failed = true, ai_provider_unknown = true }

local function report(failure, message)
    workpane.log.error("ai.tasks", tostring(failure.message), { code = failure.code or "", detail = failure.detail or "" })
    workpane.notify.error(translate("ai.error.title"), message)
end

local function persisted(future, messageKey)
    workpane.task(function()
        local _, failure = future:await()

        if failure ~= nil then
            report(failure, translate(messageKey))
        end
    end)
end

local function now()
    return workpane.time.now()
end

function engine.workspaces()
    return state.workspaces
end

function engine.tasks(workspaceId)
    local list = {}

    for _, task in ipairs(state.tasks) do
        if workspaceId == nil or task.workspaceId == workspaceId then
            list[#list + 1] = task
        end
    end

    table.sort(list, function(first, second)
        return first.position < second.position
    end)

    return list
end

function engine.task(id)
    for _, task in ipairs(state.tasks) do
        if task.id == id then
            return task
        end
    end

    return nil
end

local function queued(taskId)
    for _, id in ipairs(state.queue) do
        if id == taskId then
            return true
        end
    end

    return false
end

-- A task whose prompt is still being recorded already counts as queued, so a second start meanwhile is refused instead of recording the prompt twice.
function engine.runState(taskId)
    if state.active[taskId] ~= nil then
        return "running"
    end

    return (queued(taskId) or state.starting[taskId]) and "queued" or "idle"
end

function engine.phase(taskId)
    if state.active[taskId] ~= nil then
        return state.phases[taskId] or "running"
    end

    return (queued(taskId) or state.starting[taskId]) and "queued" or "idle"
end

-- The tools a turn is running are named on its card, one by its title and several by their count.
function engine.runningTools(taskId)
    local execution = state.active[taskId]
    local names = {}

    for _, call in ipairs(execution ~= nil and execution.calls or {}) do
        if call.started and not call.finished then
            names[#names + 1] = tools.presentation(execution.schemas, call.call.name, call.call.arguments, translate).title
        end
    end

    return names
end

function engine.outcome(taskId)
    return state.outcomes[taskId]
end

-- The text a turn streamed is kept in pieces and joined only when read, so a long answer costs its length rather than its square.
function engine.streamed(taskId)
    local streamed = state.streamed[taskId]

    if streamed == nil then
        return ""
    end

    streamed.pieces = { table.concat(streamed.pieces) }

    return streamed.pieces[1]
end

function engine.servers()
    return state.servers
end

-- The workspace list keeps exactly one active workspace, so a new one or a removed one moves the active mark along.
local function saveWorkspaces()
    persisted(store.saveWorkspaces(state.workspaces), "ai.error.workspace-save")
    announce("workspaces")
end

function engine.createWorkspace(name)
    local trimmed = (name or ""):match("^%s*(.-)%s*$")

    if trimmed == "" then
        refuse("ai_tasks_workspace_invalid", "The workspace name is required", "")
    end

    local stamp = now()

    for _, workspace in ipairs(state.workspaces) do
        workspace.active = false
    end

    local created = { id = crypto.uuidV4(), name = trimmed, active = true, createdAt = stamp, updatedAt = stamp }
    state.workspaces[#state.workspaces + 1] = created
    saveWorkspaces()

    return created.id
end

function engine.renameWorkspace(id, name)
    local trimmed = (name or ""):match("^%s*(.-)%s*$")

    if trimmed == "" then
        refuse("ai_tasks_workspace_invalid", "The workspace name is required", id)
    end

    for _, workspace in ipairs(state.workspaces) do
        if workspace.id == id then
            workspace.name = trimmed
            workspace.updatedAt = now()
        end
    end

    saveWorkspaces()
end

function engine.activateWorkspace(id)
    for _, workspace in ipairs(state.workspaces) do
        workspace.active = workspace.id == id
    end

    saveWorkspaces()
end

-- A task that leaves the board leaves nothing of its runs, its conversation or its pages behind.
local function forgetTask(taskId)
    for _, kept in ipairs({ state.conversations, state.outcomes, state.older, state.loadingOlder, state.paged, state.sequences, state.phases, state.streamed, state.starting }) do
        kept[taskId] = nil
    end
end

-- A workspace with a task still running or queued stays, because removing it would take those tasks with it mid run, and the board always keeps one workspace.
function engine.removeWorkspace(id)
    if #state.workspaces <= 1 then
        refuse("ai_tasks_workspace_required", "The board keeps at least one workspace", id)
    end

    for _, task in ipairs(engine.tasks(id)) do
        if engine.runState(task.id) ~= "idle" then
            refuse("ai_tasks_workspace_busy", "The workspace has active tasks", id)
        end
    end

    local remaining = {}
    local position
    local wasActive = false

    for index, workspace in ipairs(state.workspaces) do
        if workspace.id == id then
            position = index
            wasActive = workspace.active
        else
            remaining[#remaining + 1] = workspace
        end
    end

    local tasks = {}

    for _, task in ipairs(state.tasks) do
        if task.workspaceId ~= id then
            tasks[#tasks + 1] = task
        else
            forgetTask(task.id)
        end
    end

    state.tasks = tasks

    -- Removing the active workspace activates its neighbour, while removing another one keeps the workspace the reader was on.
    if wasActive and remaining[1] ~= nil then
        for _, workspace in ipairs(remaining) do
            workspace.active = false
        end

        remaining[math.min(position or 1, #remaining)].active = true
    end

    state.workspaces = remaining
    persisted(store.removeWorkspace(id, remaining), "ai.error.workspace-save")
    announce("workspaces")
    announce("tasks")
end

function engine.activeWorkspace()
    for _, workspace in ipairs(state.workspaces) do
        if workspace.active then
            return workspace
        end
    end

    return nil
end

local function nextPosition(workspaceId, column, except)
    local position = 0

    for _, task in ipairs(state.tasks) do
        if task.workspaceId == workspaceId and task.column == column and task.id ~= except then
            position = math.max(position, task.position + 1)
        end
    end

    return position
end

-- A schedule saved with a task starts from now: once at its moment, by interval from now and by cron at its next wall clock in the zone of the system, which it keeps.
local function prepareSchedule(schedule, nowSeconds)
    if schedule == nil then
        return nil
    end

    local prepared = { kind = schedule.kind, enabled = true, onceAtUtc = "", intervalSeconds = 0, cronExpression = "", timeZone = "", nextRunAtUtc = "", lastTriggeredAtUtc = "" }

    if schedule.kind == "once" then
        local moment = cron.epoch(schedule.onceAtUtc or "")

        if moment == nil or moment <= nowSeconds then
            refuse("ai_tasks_schedule_once_past", "The one-time schedule must be in the future", "")
        end

        prepared.onceAtUtc = schedule.onceAtUtc
        prepared.nextRunAtUtc = schedule.onceAtUtc
    elseif schedule.kind == "interval" then
        if math.type(schedule.intervalSeconds) ~= "integer" or schedule.intervalSeconds < 60 then
            refuse("ai_tasks_schedule_invalid", "The interval must be at least one minute", "")
        end

        prepared.intervalSeconds = schedule.intervalSeconds
        prepared.nextRunAtUtc = cron.stamp(nowSeconds + schedule.intervalSeconds)
    elseif schedule.kind == "cron" then
        prepared.cronExpression = schedule.cronExpression
        prepared.timeZone = workpane.time.zone()
        prepared.nextRunAtUtc = cron.stamp(cron.nextAfter(cron.parse(schedule.cronExpression), nowSeconds, prepared.timeZone))
    else
        refuse("ai_tasks_schedule_invalid", "The schedule kind is unknown", tostring(schedule.kind))
    end

    return prepared
end

-- A due schedule moves to its next occurrence: once disables it, an interval adds itself until it passes now and cron finds its next wall clock.
local function advanceSchedule(schedule, triggered)
    local advanced = {}

    for key, value in pairs(schedule) do
        advanced[key] = value
    end

    advanced.lastTriggeredAtUtc = cron.stamp(triggered)

    if schedule.kind == "once" then
        advanced.enabled = false
        advanced.nextRunAtUtc = ""
    elseif schedule.kind == "interval" then
        local next = cron.epoch(schedule.nextRunAtUtc) or triggered

        repeat
            next = next + schedule.intervalSeconds
        until next > triggered

        advanced.nextRunAtUtc = cron.stamp(next)
    else
        advanced.nextRunAtUtc = cron.stamp(cron.nextAfter(cron.parse(schedule.cronExpression), triggered, schedule.timeZone))
    end

    return advanced
end

-- A task is saved whole after its rules are checked, keeping its column or opening in To Do, and a running task is never edited.
function engine.saveTask(fields)
    local existing = fields.id ~= nil and engine.task(fields.id) or nil

    if existing ~= nil and engine.runState(existing.id) ~= "idle" then
        refuse("ai_tasks_task_busy", "The AI task is already queued or running", existing.id)
    end

    local stamp = now()
    local task = {
        id = existing ~= nil and existing.id or crypto.uuidV4(),
        workspaceId = existing ~= nil and existing.workspaceId or fields.workspaceId,
        title = fields.title:match("^%s*(.-)%s*$"),
        description = fields.description:match("^%s*(.-)%s*$"),
        prompt = fields.prompt:match("^%s*(.-)%s*$"),
        issueUrl = fields.issueUrl:match("^%s*(.-)%s*$"),
        agentId = fields.executionKind == "agent" and fields.agentId or "",
        executionKind = fields.executionKind,
        workdir = fields.workdir:match("^%s*(.-)%s*$"):gsub("[/\\]+$", ""),
        command = fields.executionKind == "command" and fields.command:match("^%s*(.-)%s*$") or "",
        commandTimeoutSeconds = fields.commandTimeoutSeconds,
        column = existing ~= nil and existing.column or "todo",
        position = existing ~= nil and existing.position or nextPosition(fields.workspaceId, "todo"),
        createdAt = existing ~= nil and existing.createdAt or stamp,
        updatedAt = stamp,
        schedule = prepareSchedule(fields.schedule, cron.epoch(stamp)),
    }

    if not store.validTask(task) then
        refuse("ai_tasks_task_invalid", "The AI task is invalid", task.id)
    end

    if existing ~= nil then
        for index, candidate in ipairs(state.tasks) do
            if candidate.id == task.id then
                state.tasks[index] = task
            end
        end
    else
        state.tasks[#state.tasks + 1] = task
    end

    persisted(store.saveTask(task), "ai.error.task-save")
    announce("tasks")

    return task.id
end

function engine.removeSchedule(taskId)
    local task = engine.task(taskId)

    if task == nil or engine.runState(taskId) ~= "idle" then
        refuse("ai_tasks_task_busy", "The AI task is already queued or running", taskId)
    end

    task.schedule = nil
    task.updatedAt = now()
    persisted(store.saveTask(task), "ai.error.task-save")
    announce("tasks")
end

function engine.removeTask(taskId)
    if engine.runState(taskId) ~= "idle" then
        refuse("ai_tasks_task_busy", "The AI task is already queued or running", taskId)
    end

    local kept = {}

    for _, task in ipairs(state.tasks) do
        if task.id ~= taskId then
            kept[#kept + 1] = task
        end
    end

    state.tasks = kept
    forgetTask(taskId)
    persisted(store.removeTask(taskId), "ai.error.task-save")
    announce("tasks")
end

-- Dropping a card on Doing starts it, dropping a running card elsewhere stops it, and any other drop moves it to the end of the column.
function engine.moveTask(taskId, column)
    local task = engine.task(taskId)

    if task == nil then
        refuse("ai_tasks_task_unknown", "The AI task is unknown", taskId)
    end

    if column == "doing" then
        if engine.runState(taskId) == "idle" then
            engine.startTask(taskId)
        end

        return
    end

    if engine.runState(taskId) ~= "idle" then
        engine.stopTask(taskId)
        return
    end

    task.position = nextPosition(task.workspaceId, column, task.id)
    task.column = column
    task.updatedAt = now()
    persisted(store.placeTasks({ task }), "ai.error.task-save")
    announce("tasks")
end

-- A stored message keeps its text apart from the parts it carries besides it, such as attachments, images a tool returned and the reasoning of the model.
local function buildMessage(taskId, role, content, toolCalls, toolCallId, parts)
    state.sequences[taskId] = (state.sequences[taskId] or 0) + 1

    return { id = crypto.uuidV4(), taskId = taskId, sequence = state.sequences[taskId], role = role, content = content, toolCalls = toolCalls or "[]", toolCallId = toolCallId or "", summarizedUntil = 0, parts = parts or {}, createdAt = now() }
end

-- The conversation of a task is read a page at a time, newest first, the first time anyone asks for it, and a reader that waited behind another keeps the page the first one read.
function engine.loadConversation(taskId)
    if state.conversations[taskId] ~= nil then
        return state.conversations[taskId]
    end

    local page = store.messages(taskId, math.maxinteger, pageSize)

    if state.conversations[taskId] == nil then
        state.conversations[taskId] = page
        state.older[taskId] = #page == pageSize
    end

    return state.conversations[taskId]
end

-- One older page is read at a time, so two requests never insert the same page twice.
function engine.loadOlder(taskId)
    local loaded = engine.loadConversation(taskId)

    if not state.older[taskId] or loaded[1] == nil or state.loadingOlder[taskId] then
        return false
    end

    state.loadingOlder[taskId] = true
    local read, page = pcall(store.messages, taskId, loaded[1].sequence, pageSize)
    state.loadingOlder[taskId] = nil

    if not read then
        error(page, 0)
    end

    for index = #page, 1, -1 do
        table.insert(loaded, 1, page[index])
    end

    state.older[taskId] = #page == pageSize
    state.paged[taskId] = #loaded
    announce("conversation", taskId)

    return #page > 0
end

-- The cache keeps the newest pages, or as many messages as the reader paged back to, so a task that runs for months holds no more than its chat shows.
local function bound(taskId, conversation)
    local excess = #conversation - math.max(cachedPages * pageSize, state.paged[taskId] or 0)

    if excess <= 0 then
        return
    end

    table.move(conversation, excess + 1, #conversation, 1)

    for index = #conversation, #conversation - excess + 1, -1 do
        conversation[index] = nil
    end

    state.older[taskId] = true
end

local completeExecution

-- The conversation is the source of truth, so a turn that could not be written is taken back and the run that produced it stops.
local function record(taskId, messages)
    local conversation = engine.loadConversation(taskId)

    for _, message in ipairs(messages) do
        conversation[#conversation + 1] = message
    end

    bound(taskId, conversation)
    announce("conversation", taskId)
    local _, failure = store.appendMessages(messages):await()

    if failure == nil then
        return true
    end

    for _, message in ipairs(messages) do
        for index = #conversation, 1, -1 do
            if conversation[index].id == message.id then
                table.remove(conversation, index)
            end
        end
    end

    local reason = translate("ai.error.conversation-save")
    report(failure, reason)
    announce("conversation", taskId)

    if state.active[taskId] ~= nil then
        completeExecution(taskId, "failed", reason, "failed")
    else
        state.outcomes[taskId] = { status = "failed", stopReason = "failed", errorMessage = reason }
        announce("run", taskId)
    end

    return false
end

local function hasCapacity()
    local limit = preferences.execution().parallelExecutions
    local running = 0

    for _ in pairs(state.active) do
        running = running + 1
    end

    return limit == 0 or running < limit
end

local startExecution

function engine.dispatch()
    if state.stopping then
        return
    end

    -- A task a run that ended at once already started and ended is no longer queued, so it is never started twice from the same list.
    for _, taskId in ipairs({ table.unpack(state.queue) }) do
        local task = engine.task(taskId)

        if task ~= nil and queued(taskId) and state.active[taskId] == nil and hasCapacity() then
            startExecution(task)
        end
    end
end

-- The task enters the queue before its row is written, so a second start asked for meanwhile never reaches storage as a duplicate.
local function enqueue(taskId, schedule)
    local task = engine.task(taskId)
    local stamp = now()
    local previousColumn, previousSchedule = task.column, task.schedule

    if schedule ~= nil then
        task.schedule = schedule
    end

    state.queue[#state.queue + 1] = taskId
    task.column = "doing"
    task.updatedAt = stamp
    announce("tasks")
    announce("run", taskId)

    workpane.task(function()
        local _, failure = store.enqueue(task, stamp):await()

        if failure ~= nil then
            for index, id in ipairs(state.queue) do
                if id == taskId then
                    table.remove(state.queue, index)
                    break
                end
            end

            task.column, task.schedule = previousColumn, previousSchedule
            report(failure, translate("ai.error.task-run"))
            announce("tasks")
            announce("run", taskId)
            return
        end

        engine.dispatch()
    end)
end

local function agentFor(task)
    local agent = preferences.agent(task.agentId)

    if agent == nil then
        refuse("ai_agent_unknown", "The agent of the task is not configured", task.agentId)
    end

    local connection = connections.find(preferences.connections(), agent.connectionKey)

    if connection == nil then
        refuse("ai_connection_unknown", "The connection the agent runs on is not configured", agent.connectionKey)
    end

    return agent, connection
end

-- Starting an agent task sends its prompt again, because that prompt is the standing instruction a run and a schedule repeat.
function engine.startTask(taskId, schedule)
    local task = engine.task(taskId)

    if task == nil then
        refuse("ai_tasks_task_unknown", "The AI task is unknown", taskId)
    end

    if engine.runState(taskId) ~= "idle" then
        refuse("ai_tasks_task_busy", "The AI task is already queued or running", taskId)
    end

    if task.executionKind == "agent" then
        local found, failure = pcall(agentFor, task)

        if not found then
            state.outcomes[taskId] = { status = "failed", stopReason = "failed", errorMessage = translate("ai.error.agent-removed", task.agentId) }
            announce("run", taskId)
            error(failure, 0)
        end

        state.starting[taskId] = true
        local read, recorded = pcall(record, taskId, { buildMessage(taskId, "user", task.prompt) })
        state.starting[taskId] = nil

        if not read then
            error(recorded, 0)
        end

        -- A prompt that could not be kept already ended the start, which its caller hears as a failure rather than as a queued task.
        if not recorded then
            refuse("ai_tasks_start_failed", "The prompt of the AI task could not be kept", taskId)
        end
    end

    enqueue(taskId, schedule)
end

-- Another plugin asks with exactly the identifier of a task and receives it once the task is queued.
function engine.startRequested(payload)
    local keys = 0

    for _ in pairs(type(payload) == "table" and payload or {}) do
        keys = keys + 1
    end

    if type(payload) ~= "table" or type(payload.taskId) ~= "string" or keys ~= 1 then
        refuse("ai_tasks_request_invalid", "A request to start a task carries exactly its identifier", "")
    end

    engine.startTask(payload.taskId)

    return { taskId = payload.taskId }
end

-- A message typed while a turn runs joins the conversation at once and is read at the next iteration of that turn, and the answer tells whether it was kept.
-- The attachments are checked as the parts of a user message, so a file the protocols cannot carry is refused before it is kept.
function engine.sendMessage(taskId, text, attachments)
    local task = engine.task(taskId)
    local trimmed = (text or ""):match("^%s*(.-)%s*$")
    local parts = attachments or {}

    if task == nil or task.executionKind ~= "agent" or (trimmed == "" and #parts == 0) then
        refuse("ai_conversation_invalid", "The message is invalid", taskId)
    end

    local checked = messages.normalize({ { role = "user", content = trimmed == "" and parts or { { type = "text", text = trimmed }, table.unpack(parts) } } })[1].content

    if trimmed ~= "" then
        table.remove(checked, 1)
    end

    if not record(taskId, { buildMessage(taskId, "user", trimmed, nil, nil, checked) }) then
        return false
    end

    if engine.runState(taskId) == "idle" then
        enqueue(taskId)
    end

    return true
end

-- An agent that is removed stops the tasks it was running, because a task without its agent has nobody to answer it.
function engine.stopOrphans()
    for _, task in ipairs(state.tasks) do
        if task.executionKind == "agent" and preferences.agent(task.agentId) == nil then
            if engine.runState(task.id) ~= "idle" then
                engine.stopTask(task.id)
            end

            state.outcomes[task.id] = { status = "failed", stopReason = "failed", errorMessage = translate("ai.error.agent-removed", task.agentId) }
            announce("run", task.id)
        end
    end
end

function engine.resetConversation(taskId)
    if engine.runState(taskId) ~= "idle" then
        refuse("ai_tasks_task_busy", "The AI task is already queued or running", taskId)
    end

    workpane.await(store.clearConversation(taskId))
    state.conversations[taskId] = {}
    state.sequences[taskId] = 0
    state.older[taskId] = false
    state.paged[taskId] = nil
    state.outcomes[taskId] = nil
    announce("conversation", taskId)
    announce("run", taskId)
end

local function appendLog(executionId, level, kind, detail)
    state.logSequences[executionId] = (state.logSequences[executionId] or 0) + 1
    persisted(store.appendLog({ id = crypto.uuidV4(), executionId = executionId, sequence = state.logSequences[executionId], timestamp = now(), level = level, kind = kind, detail = detail or "" }), "ai.error.log-save")

    for taskId, execution in pairs(state.active) do
        if execution.record.id == executionId then
            announce("activity", taskId)
        end
    end
end

local function setPhase(taskId, phase)
    if state.active[taskId] ~= nil and state.phases[taskId] ~= phase then
        state.phases[taskId] = phase
        announce("run", taskId)
    end
end

-- A run ends once: its record is closed, what it still ran is stopped, its card moves to Done or back to To Do and the queue moves on.
completeExecution = function(taskId, status, errorMessage, stopReason)
    local execution = state.active[taskId]

    if execution == nil then
        return
    end

    local record = execution.record
    local cancelled = execution.cancelled

    if execution.output ~= nil then
        record.content = table.concat(execution.output)
    end

    record.status = cancelled and "cancelled" or status
    record.stopReason = cancelled and "cancelled" or stopReason
    record.finishedAt = now()
    record.errorMessage = errorMessage or ""
    record.providerId = execution.connection ~= nil and execution.connection.providerId or ""
    record.modelId = execution.connection ~= nil and execution.connection.modelId or ""

    if cancelled then
        appendLog(record.id, "warning", "cancelled", "")
    elseif status == "succeeded" then
        appendLog(record.id, stopReason == "answered" and "info" or "warning", "succeeded", stopReason == "answered" and "" or translate("ai.stop-reason." .. stopReason))
    end

    execution.finished = true
    state.active[taskId] = nil
    state.phases[taskId] = nil
    state.streamed[taskId] = nil
    state.logSequences[record.id] = nil
    state.outcomes[taskId] = { status = record.status, stopReason = record.stopReason, errorMessage = record.status == "failed" and record.errorMessage or "" }

    if execution.client ~= nil then
        execution.client:cancel()
    end

    if execution.summary ~= nil then
        execution.summary:cancel()
    end

    for _, call in ipairs(execution.calls or {}) do
        if call.started and not call.finished then
            tools.cancel(call.call.id)
        end
    end

    if execution.run ~= nil and not execution.run.done then
        execution.run:cancel()
    end

    persisted(store.finishExecution(record), "ai.error.execution-save")
    local task = engine.task(taskId)
    local column = record.status == "succeeded" and "done" or "todo"
    local stamp = record.finishedAt

    for index, id in ipairs(state.queue) do
        if id == taskId then
            table.remove(state.queue, index)
            break
        end
    end

    if task ~= nil then
        task.column = column
        task.position = nextPosition(task.workspaceId, column, task.id)
        task.updatedAt = stamp
        persisted(store.settle(taskId, column, stamp), "ai.error.task-save")
        persisted(store.placeTasks({ task }), "ai.error.task-save")
    end

    announce("tasks")
    announce("run", taskId)
    announce("conversation", taskId)
    engine.dispatch()
end

local function fail(taskId, failure, message)
    local execution = state.active[taskId]

    if execution == nil or execution.cancelled then
        return
    end

    if failure.detail ~= nil and failure.detail ~= "" then
        appendLog(execution.record.id, "debug", "response-received", failure.detail)
    end

    appendLog(execution.record.id, "error", "failed", message)
    report(failure, message)
    completeExecution(taskId, "failed", message, "failed")
end

function engine.stopTask(taskId)
    if engine.runState(taskId) == "idle" then
        refuse("ai_tasks_task_idle", "The AI task is not queued or running", taskId)
    end

    local execution = state.active[taskId]

    if execution ~= nil then
        execution.cancelled = true
        completeExecution(taskId, "cancelled", "", "cancelled")
        return
    end

    local task = engine.task(taskId)
    local stamp = now()

    for index, id in ipairs(state.queue) do
        if id == taskId then
            table.remove(state.queue, index)
            break
        end
    end

    task.column = "todo"
    task.updatedAt = stamp
    persisted(store.settle(taskId, "todo", stamp), "ai.error.task-save")
    announce("tasks")
    announce("run", taskId)
end

-- The machine is inspected once, by the first run that describes its environment.
local function operatingSystem()
    if state.operatingSystem == "" then
        local snapshot = workpane.system.information():await()
        local system = type(snapshot) == "table" and snapshot.os or nil
        local named = type(system) == "table" and ((system.name or "") .. " " .. (system.version or "")):match("^%s*(.-)%s*$") or ""
        state.operatingSystem = named ~= "" and named or workpane.app.platform
    end

    return state.operatingSystem
end

local function environmentSection(home)
    local user = process.getenv("USER") or process.getenv("USERNAME") or ""
    local language = workpane.app.language()
    local lines = { translate("ai.agent.environment") }

    if user ~= "" then
        lines[#lines + 1] = "- user: " .. user
    end

    lines[#lines + 1] = "- home directory: " .. home
    local zone = workpane.time.zone()
    local seconds = os.time()
    lines[#lines + 1] = "- local time: " .. os.date("!%Y-%m-%dT%H:%M:%S", seconds + workpane.time.offset(zone, seconds)) .. " (" .. zone .. ")"
    lines[#lines + 1] = "- utc time: " .. now()
    lines[#lines + 1] = "- locale: " .. language
    lines[#lines + 1] = "- language: " .. (languages[language] or language)
    lines[#lines + 1] = "- operating system: " .. operatingSystem()

    return table.concat(lines, "\n")
end

local function contextInstructions(found)
    local collected = {}

    for _, context in ipairs(resources.ofKind(found, "context")) do
        if context.content ~= "" then
            collected[#collected + 1] = translate("ai.agent.context-file", context.name, context.content)
        end
    end

    return collected
end

-- A skill is disclosed progressively, so only its name and description are offered and its body is loaded by a tool.
local function skillCatalog(found)
    local skills = resources.ofKind(found, "skill")

    if #skills == 0 then
        return ""
    end

    local lines = { translate("ai.agent.skills") }

    for _, skill in ipairs(skills) do
        lines[#lines + 1] = "- " .. skill.name .. ": " .. skill.description
    end

    return table.concat(lines, "\n")
end

local function serverCatalog()
    local names = {}

    for _, client in ipairs(state.servers) do
        if client.ready then
            names[#names + 1] = client.descriptor.id
        end
    end

    table.sort(names)

    return #names == 0 and translate("ai.capability.servers-none") or translate("ai.capability.servers", table.concat(names, ", "))
end

-- The instructions are the prompt of the agent with every tag answered by what this run really has.
local function instructionsFor(task, agent, connection, found, schemas)
    local home = workpane.system.home()
    local provider = catalog.provider(connection.providerId)
    local traits = provider ~= nil and catalog.traits(provider, connection.modelId) or {}
    local model = provider ~= nil and catalog.model(provider, connection.modelId) or nil
    local search = preferences.search()
    local speech = preferences.speech()
    local names = {}

    for index, schema in ipairs(schemas) do
        names[index] = schema.name
    end

    local sections = { task.workdir == "" and translate("ai.agent.no-workdir") or translate("ai.agent.workdir", task.workdir), environmentSection(home) }

    for _, context in ipairs(contextInstructions(found)) do
        sections[#sections + 1] = context
    end

    local skills = skillCatalog(found)

    if skills ~= "" then
        sections[#sections + 1] = skills
    end

    local language = workpane.app.language()
    local values = {
        SYSTEM_PROMPT_DATA = table.concat(sections, "\n\n"),
        AGENT_NAME = agent.name,
        AGENT_DESCRIPTION = agent.description,
        TASK_TITLE = task.title,
        TASK_DESCRIPTION = task.description,
        TASK_PROMPT = task.prompt,
        TASK_WORKDIR = task.workdir,
        TASK_ISSUE_URL = task.issueUrl,
        DATE_TIME = workpane.time.localPresentation(now()),
        DATE_TIME_UTC = os.date("!%Y-%m-%dT%H:%M:%SZ"),
        TIME_ZONE = workpane.time.zone(),
        LOCALE = language,
        LANGUAGE = languages[language] or language,
        OPERATING_SYSTEM = operatingSystem(),
        USER_NAME = process.getenv("USER") or process.getenv("USERNAME") or "",
        HOME_DIRECTORY = home,
        TOOLS = table.concat(names, ", "),
        SKILLS = skills,
        CONTEXT_FILES = table.concat(contextInstructions(found), "\n\n"),
        MODEL = connections.key(connection),
        MODEL_TRAITS = table.concat(catalog.sortedTraits(traits), ", "),
        VISION = translate(traits.vision and "ai.capability.vision-yes" or "ai.capability.vision-no"),
        SEARCH = search.configured and translate("ai.capability.search-yes", search.provider) or translate("ai.capability.search-no"),
        SPEECH = speech.configured and translate("ai.capability.speech-yes", speech.provider) or translate("ai.capability.speech-no"),
        SERVERS = serverCatalog(),
        CONTEXT_WINDOW = tostring(model ~= nil and model.context or 0),
        OUTPUT_BUDGET = tostring(connections.outputBudget(connection)),
    }

    return { role = "system", content = prompt.render(agent.systemPrompt, values) }
end

-- Answers the canonical content of a stored message, the reasoning of the model first and the parts it carried after its text.
local function storedContent(stored)
    local content = {}

    for _, entry in ipairs(stored.parts) do
        if entry.type == "reasoning" then
            content[#content + 1] = entry
        end
    end

    if stored.content ~= "" then
        content[#content + 1] = { type = "text", text = stored.content }
    end

    for _, entry in ipairs(stored.parts) do
        if entry.type ~= "reasoning" then
            content[#content + 1] = entry
        end
    end

    return content
end

-- The stored conversation becomes the canonical one every protocol is built from, so the same dialogue survives a change of provider, and every emitted message names the turn that produced it.
-- A call a stopped run never answered is answered as interrupted, since every protocol refuses a call without its result.
-- Once a summary covers the oldest turns, the opening of the conversation still comes first and the newest summary right after it, so the model reads what it was first asked and what came before the turns that follow.
function engine.project(instructions, conversation, translateText)
    local projected = { instructions }
    local sequences = { 0 }
    local calls = {}
    local answered = {}
    local callSequence = 0
    local pending = {}
    local deferred = {}
    local summarized = 0
    local summary = nil
    local opening = nil

    for _, stored in ipairs(conversation) do
        if stored.summarizedUntil > summarized then
            summarized = stored.summarizedUntil
            summary = stored
        end

        if opening == nil and stored.role == "user" and stored.summarizedUntil == 0 then
            opening = stored
        end
    end

    local function append(message, sequence)
        projected[#projected + 1] = message
        sequences[#sequences + 1] = sequence
    end

    if summary ~= nil and opening ~= nil and opening.sequence <= summarized then
        append({ role = "user", content = storedContent(opening) }, opening.sequence)
    end

    if summary ~= nil then
        append({ role = "user", content = storedContent(summary) }, summary.sequence)
    end

    local function unanswered()
        local count = 0

        for _, call in ipairs(calls) do
            count = count + (answered[call.id] and 0 or 1)
        end

        return count
    end

    -- The results of a turn follow the calls they answer, and a message the reader sent meanwhile follows those results.
    local function close()
        for _, call in ipairs(calls) do
            if not answered[call.id] then
                pending[#pending + 1] = { message = { role = "tool", toolCallId = call.id, failed = true, content = translateText("ai.message.call-interrupted", call.name) }, sequence = callSequence }
            end
        end

        for _, entry in ipairs(pending) do
            append(entry.message, entry.sequence)
        end

        for _, entry in ipairs(deferred) do
            append(entry.message, entry.sequence)
        end

        calls = {}
        answered = {}
        pending = {}
        deferred = {}
    end

    for _, stored in ipairs(conversation) do
        local current = stored.sequence > summarized and stored.summarizedUntil == 0
        local open = current and stored.role == "tool" and answered[stored.toolCallId] == false

        if open then
            answered[stored.toolCallId] = true
            pending[#pending + 1] = { message = { role = "tool", toolCallId = stored.toolCallId, content = storedContent(stored) }, sequence = stored.sequence }
        elseif current and stored.role == "user" and unanswered() > 0 then
            deferred[#deferred + 1] = { message = { role = "user", content = storedContent(stored) }, sequence = stored.sequence }
        elseif current and stored.role == "user" then
            close()
            append({ role = "user", content = storedContent(stored) }, stored.sequence)
        elseif current and stored.role == "assistant" then
            close()
            calls = codec.read(stored.toolCalls, true) or {}
            callSequence = stored.sequence
            append({ role = "assistant", content = storedContent(stored), toolCalls = calls }, stored.sequence)

            for _, call in ipairs(calls) do
                answered[call.id] = false
            end
        end

        if unanswered() == 0 and (#pending > 0 or #deferred > 0) then
            close()
        end
    end

    close()

    return projected, sequences
end

-- A summary covers every message before the oldest one the window kept, so a message the window kept is never left out of a later run, even one a turn moved after results it arrived before.
local function coveredUntil(sequences, fitted)
    local first = fitted.preservedHead + #fitted.dropped + 1
    local oldest = nil
    local newest = 0

    for index = fitted.preservedHead + 1, #sequences do
        if index >= first then
            oldest = math.min(oldest or sequences[index], sequences[index])
        end

        newest = math.max(newest, sequences[index])
    end

    return oldest ~= nil and oldest - 1 or newest
end

-- A delta is told at most once every fifty milliseconds while more arrive, and the telling that follows the last one always shows it.
local function stream(taskId, execution, delta)
    local streamed = state.streamed[taskId]
    execution.output[#execution.output + 1] = delta
    streamed.pieces[#streamed.pieces + 1] = delta

    if streamed.telling then
        return
    end

    streamed.telling = true

    workpane.task(function()
        async.sleep(streamedMilliseconds):await()
        streamed.telling = false
        announce("streamed", taskId)
    end)
end

-- A request is logged by where it went, its size and the messages it carried, because the conversation it sends is already kept whole.
local function chatHandlers(taskId, execution)
    return {
        requestSent = function(target, bytes, count)
            appendLog(execution.record.id, "debug", "request-sent", translate("ai.log.request-summary", target, tostring(bytes), tostring(count)))

            if state.phases[taskId] == "throttled" then
                setPhase(taskId, execution.beforeThrottle or "sending")
            end
        end,
        content = function(delta)
            if execution.finished then
                return
            end

            if state.phases[taskId] ~= "streaming" and not execution.summarizing then
                setPhase(taskId, "streaming")
                appendLog(execution.record.id, "info", "first-token", "")
            end

            if not execution.summarizing then
                stream(taskId, execution, delta)
            end
        end,
        throttled = function(reason, milliseconds, cause)
            if execution.finished then
                return
            end

            appendLog(execution.record.id, "info", "throttled", reason == "retry" and translate("ai.log.retry-delay", tostring(milliseconds), cause) or translate("ai.log.rate-limit-delay", tostring(milliseconds)))

            if state.phases[taskId] ~= "throttled" then
                execution.beforeThrottle = state.phases[taskId]
            end

            setPhase(taskId, "throttled")
        end,
    }
end

-- The answer and the tool declarations take room beside the conversation, and a command line agent reserves neither because it receives a prompt and runs its own tools.
local function reserved(connection, schemas)
    local protocol = connections.protocol(connection)

    if protocol == "command-line" then
        return 0
    end

    return messages.textTokens(codec.encode(protocols.serializeTools(protocol, schemas, translate))) + math.max(connections.outputBudget(connection), 0)
end

local function said(text)
    return { { type = "text", text = text } }
end

-- The reasoning a model returned is kept with the turn it produced, one part for each block, since a protocol that proves its reasoning needs every block back unchanged beside the calls of that turn.
local function reasoningParts(reasoning)
    local parts = {}

    for index, block in ipairs(reasoning or {}) do
        parts[index] = { type = "reasoning", text = block.text, signature = block.signature, redacted = block.redacted }
    end

    return parts
end

-- What no longer fits the window is replaced by one summary, so the agent keeps what it learned instead of only what is recent.
local function summarize(taskId, execution, fitted)
    local connection = connections.withBudget(execution.connection, catalog.limit("summaryMaximumTokens"))
    local provider = catalog.provider(execution.connection.providerId)
    local model = provider ~= nil and catalog.model(provider, execution.connection.modelId) or nil
    local limit = window.limit(model ~= nil and model.context or 0, catalog.limit("summaryMaximumTokens"))
    local dropped = {}

    for index, message in ipairs(fitted.dropped) do
        dropped[index] = message
    end

    local pruned = window.prune(dropped, limit)

    if pruned > 0 then
        appendLog(execution.record.id, "info", "compacted", translate("ai.log.pruned", tostring(pruned)))
    end

    local lines = {}

    for index, message in ipairs(window.fit(dropped, limit).messages) do
        lines[index] = message.role .. ": " .. messages.describe(message, translate)
    end

    execution.summarizing = true
    setPhase(taskId, "compacting")
    local handlers = chatHandlers(taskId, execution)
    execution.summary = completion.client(connection, handlers)
    local request = { { role = "user", content = said(translate("ai.agent.summarize") .. "\n\n" .. table.concat(lines, "\n\n")) } }
    local answered, result = pcall(execution.summary.send, execution.summary, { connection = connection, address = connections.address(connection), messages = request, tools = {}, workdir = execution.task.workdir }, translate)
    execution.summary = nil
    execution.summarizing = false

    if execution.finished then
        return nil
    end

    if not answered then
        appendLog(execution.record.id, "warning", "compacted", type(result) == "table" and tostring(result.message) or tostring(result))
        return fitted.messages
    end

    execution.record.inputTokens = execution.record.inputTokens + result.usage.input
    execution.record.outputTokens = execution.record.outputTokens + result.usage.output
    local summary = result.content:match("^%s*(.-)%s*$")

    if summary == "" then
        return fitted.messages
    end

    local text = translate("ai.agent.summary", summary)
    local kept = {}

    for index, message in ipairs(fitted.messages) do
        kept[#kept + 1] = message

        if index == fitted.preservedHead then
            kept[#kept + 1] = { role = "user", content = said(text) }
        end
    end

    if fitted.preservedHead == 0 then
        table.insert(kept, 1, { role = "user", content = said(text) })
    end

    appendLog(execution.record.id, "info", "compacted", summary)
    local message = buildMessage(taskId, "user", text)
    message.summarizedUntil = execution.summarizedUntil

    -- A summary that could not be kept already ended the run, so nothing is sent without it.
    if not record(taskId, { message }) then
        return nil
    end

    return kept
end

-- The calls of a turn start as soon as nothing running can reach what they touch, each within its deadline, and their results are kept in the order the model asked.
local function runTools(taskId, execution, calls)
    local repeatLimit = catalog.limit("repeatedToolCallLimit")
    local stored = {}

    for index, call in ipairs(calls) do
        stored[index] = { id = call.id, name = call.name, arguments = call.arguments }
    end

    if not record(taskId, { buildMessage(taskId, "assistant", execution.turnText, codec.encode(codec.list(stored)), nil, reasoningParts(execution.turnReasoning)) }) then
        return false
    end

    execution.calls = {}

    for _, call in ipairs(calls) do
        local arguments = call.unreadable or codec.encode(call.arguments)
        local signature = call.name .. arguments
        appendLog(execution.record.id, "info", "tool-called", call.name .. " " .. arguments)
        execution.signatures[signature] = (execution.signatures[signature] or 0) + 1

        if execution.signatures[signature] > repeatLimit then
            local reason = execution.failures[call.name]
            appendLog(execution.record.id, "warning", "tool-returned", reason ~= nil and translate("ai.error.tool-repeated-reason", call.name, reason) or translate("ai.error.tool-repeated", call.name))
            completeExecution(taskId, "succeeded", "", "tool-repetition")
            return false
        end

        execution.calls[#execution.calls + 1] = { call = call, access = tools.access(execution.schemas, call, execution.task.workdir, translate) }
    end

    local context = { root = execution.task.workdir, task = execution.task, connection = execution.connection, media = preferences.defaultConnection(), search = preferences.search(), speech = preferences.speech(), servers = function()
        return state.servers
    end, translate = translate, resources = function()
        return resources.discover(execution.task.workdir, false)
    end }

    announce("run", taskId)

    while not execution.finished do
        local waiting = false

        for _, pending in ipairs(execution.calls) do
            local blocked = false

            for _, other in ipairs(execution.calls) do
                blocked = blocked or (other.started and not other.finished and tools.conflict(pending.access, other.access))
            end

            if not pending.started and not blocked then
                pending.started = true
                local deadline = tools.deadline(pending.call)
                pending.deadline = deadline > 0 and (os.time() + deadline / 1000) or nil
                announce("run", taskId)

                if pending.call.unreadable ~= nil then
                    pending.result = { callId = pending.call.id, text = translate("ai.error.tool-arguments-unreadable", pending.call.name, pending.call.unreadable), failed = true }
                    pending.finished = true
                else
                    workpane.task(function()
                        local result = tools.invoke(execution.schemas, pending.call, context)

                        if not pending.finished then
                            pending.result = result
                            pending.finished = true
                        end
                    end)
                end
            end

            if pending.started and not pending.finished and pending.deadline ~= nil and os.time() > pending.deadline then
                tools.cancel(pending.call.id)
                pending.result = { callId = pending.call.id, text = translate("ai.error.tool-deadline", pending.call.name), failed = true }
                pending.finished = true
            end

            waiting = waiting or not pending.finished
        end

        if not waiting then
            break
        end

        async.sleep(pollMilliseconds):await()
    end

    if execution.finished then
        return false
    end

    local answered = {}

    for _, pending in ipairs(execution.calls) do
        local result = pending.result
        appendLog(execution.record.id, result.failed and "warning" or "info", "tool-returned", pending.call.name .. " " .. result.text)
        execution.failures[pending.call.name] = result.failed and result.text or nil
        local image = result.imageData ~= nil and result.imageData ~= "" and { { type = "image", mediaType = result.imageMediaType, data = result.imageData } } or {}
        answered[#answered + 1] = buildMessage(taskId, "tool", result.text, "[]", result.callId, image)
    end

    execution.calls = {}
    announce("run", taskId)

    return record(taskId, answered)
end

-- A message that arrived while the turn was giving its final answer never reached the model, so it opens the next run, while a summary never does.
local function continueWhenPending(taskId, delivered)
    for _, message in ipairs(engine.loadConversation(taskId)) do
        if message.role == "user" and message.summarizedUntil == 0 and message.sequence > delivered and engine.runState(taskId) == "idle" then
            enqueue(taskId)
            return
        end
    end
end

-- Each iteration fits the conversation to the window of the model, sends it and either answers, runs the tools the model asked for or stops for a reason it records.
local function runAgent(taskId, execution)
    local agent, connection = agentFor(execution.task)
    execution.connection = connection
    execution.maximumIterations = agent.maximumIterations
    execution.output = {}
    execution.client = completion.client(connection, chatHandlers(taskId, execution))
    appendLog(execution.record.id, "info", "started", connections.key(connection))

    local found = resources.discover(execution.task.workdir, true)
    local instructions = instructionsFor(execution.task, agent, connection, found, tools.schemas(state.servers))
    local provider = catalog.provider(connection.providerId)
    local model = catalog.model(provider, connection.modelId)
    local traits = catalog.traits(provider, connection.modelId)
    local adjusted = {}

    while not execution.finished do
        execution.iteration = execution.iteration + 1

        if execution.maximumIterations > 0 and execution.iteration > execution.maximumIterations then
            completeExecution(taskId, "succeeded", "", "iteration-limit")
            return
        end

        -- The catalog is built again for every iteration, so the tools of servers that restarted during the run are the ones offered.
        execution.schemas = connections.protocol(connection) == "command-line" and {} or tools.schemas(state.servers)

        local limit = window.limit(model ~= nil and model.context or 0, reserved(connection, execution.schemas))
        local context = store.context(taskId)

        -- A run stopped while its conversation was read sends nothing.
        if execution.finished then
            return
        end

        local projected, sequences = engine.project(instructions, context, translate)
        local canonical, adjustments = messages.fit(messages.normalize(projected), traits, translate)

        -- A change the model forced on the conversation is written once per run, so the reader learns why an attachment went unread.
        for _, kind in ipairs(adjustments) do
            if not adjusted[kind] then
                adjusted[kind] = true
                appendLog(execution.record.id, "info", "adjusted", translate("ai.log.adjusted-" .. kind))
            end
        end

        local pruned = window.prune(canonical, limit)

        if pruned > 0 then
            appendLog(execution.record.id, "info", "compacted", translate("ai.log.pruned", tostring(pruned)))
        end

        local fitted = window.fit(canonical, limit)
        local outgoing = fitted.messages

        if #fitted.dropped > 0 then
            appendLog(execution.record.id, "warning", "compacted", translate("ai.log.compacted", tostring(#fitted.dropped)))
            execution.summarizedUntil = coveredUntil(sequences, fitted)
            outgoing = summarize(taskId, execution, fitted)

            if outgoing == nil then
                return
            end
        end

        if execution.maximumIterations > 0 and execution.iteration == execution.maximumIterations then
            outgoing[#outgoing + 1] = { role = "user", content = said(translate("ai.agent.final-turn")) }
        end

        appendLog(execution.record.id, "info", "iteration", tostring(execution.iteration))
        local conversation = engine.loadConversation(taskId)
        execution.delivered = conversation[#conversation] ~= nil and conversation[#conversation].sequence or 0
        state.streamed[taskId] = { pieces = {}, telling = false }
        setPhase(taskId, "sending")
        local result = execution.client:send({ connection = connection, address = connections.address(connection), messages = outgoing, tools = execution.schemas, workdir = execution.task.workdir }, translate)

        if execution.finished then
            return
        end

        execution.record.inputTokens = execution.record.inputTokens + result.usage.input
        execution.record.outputTokens = execution.record.outputTokens + result.usage.output
        execution.record.finishReason = result.finishReason
        appendLog(execution.record.id, "debug", "response-received", result.content)
        appendLog(execution.record.id, "info", "usage-reported", translate("ai.log.usage", tostring(result.usage.input), tostring(result.usage.output), result.finishReason))

        if chat.truncated(result.finishReason) or #result.calls == 0 then
            if result.content:match("%S") then
                record(taskId, { buildMessage(taskId, "assistant", result.content, nil, nil, reasoningParts(result.reasoning)) })
            end

            local delivered = execution.delivered
            completeExecution(taskId, "succeeded", "", chat.truncated(result.finishReason) and "output-budget" or "answered")

            if not chat.truncated(result.finishReason) then
                continueWhenPending(taskId, delivered)
            end

            return
        end

        if result.content ~= "" then
            execution.output[#execution.output + 1] = "\n"
        end

        execution.turnText = result.content
        execution.turnReasoning = result.reasoning
        setPhase(taskId, "calling-tool")

        if not runTools(taskId, execution, result.calls) then
            return
        end
    end
end

local function runCommand(taskId, execution)
    local task = execution.task
    state.phases[taskId] = "running"
    appendLog(execution.record.id, "info", "started", task.command)
    local program, arguments = commands.shell(task.command)
    execution.run = commands.start({ program = program, arguments = arguments, workdir = task.workdir, timeoutSeconds = task.commandTimeoutSeconds, cancelled = function()
        return execution.finished
    end })

    announce("run", taskId)
    local finished, code, output = pcall(execution.run.await, execution.run)

    if execution.finished then
        return
    end

    -- A command that failed keeps what it printed before it failed.
    if not finished then
        execution.record.content = execution.run:printed()
        fail(taskId, code, commands.message(code, translate))
        return
    end

    execution.record.content = output
    execution.record.finishReason = tostring(code)
    appendLog(execution.record.id, "debug", "response-received", output)

    if code == 0 then
        completeExecution(taskId, "succeeded", "", "answered")
        return
    end

    local message = translate("ai.error.exit-code", tostring(code))
    appendLog(execution.record.id, "error", "failed", message)
    completeExecution(taskId, "failed", message, "failed")
end

-- An execution is recorded before it runs, and a failure anywhere in it ends the run with the reason the reader sees on the card.
startExecution = function(task)
    local stamp = now()
    local execution = { task = task, record = { id = crypto.uuidV4(), taskId = task.id, status = "running", startedAt = stamp, finishedAt = "", inputTokens = 0, outputTokens = 0, finishReason = "", errorMessage = "", content = "", stopReason = "answered", providerId = "", modelId = "" }, iteration = 0, signatures = {}, failures = {}, calls = {}, finished = false }
    state.active[task.id] = execution
    state.phases[task.id] = task.executionKind == "command" and "running" or "sending"

    for index, id in ipairs(state.queue) do
        if id == task.id then
            table.remove(state.queue, index)
            break
        end
    end

    persisted(store.startExecution(execution.record), "ai.error.execution-save")
    announce("run", task.id)

    workpane.task(function()
        local ran, failure = pcall(task.executionKind == "command" and runCommand or runAgent, task.id, execution)

        if ran or execution.finished then
            return
        end

        local structured = type(failure) == "table" and failure or { code = "ai_run_failed", message = tostring(failure), detail = "" }
        local mapped = failureKeys[structured.code]
        local message

        if structured.code == "ai_output_truncated" then
            message = translate("ai.error.output-truncated", tostring(connections.outputBudget(execution.connection or { providerId = "", modelId = "", parameters = {} })))
        elseif structured.code == "ai_agent_unknown" then
            message = translate("ai.error.agent-removed", structured.detail)
        elseif structured.code == "ai_connection_unknown" then
            message = translate("ai.error.connection-missing", structured.detail)
        elseif mapped ~= nil then
            message = mapped.argument ~= nil and translate(mapped.key, tostring(structured[mapped.argument])) or translate(mapped.key)
        elseif translatedFailures[structured.code] then
            message = tostring(structured.message)
        else
            message = translate("ai.error.run-failed")
        end

        fail(task.id, structured, message)
    end)
end

-- A schedule that cannot be advanced stops without a next occurrence, as every disabled schedule is stored, so it never falls due again until the reader edits it.
local function paused(schedule)
    local stopped = {}

    for key, value in pairs(schedule) do
        stopped[key] = value
    end

    stopped.enabled = false
    stopped.nextRunAtUtc = ""

    return stopped
end

-- Due tasks that are idle take their next occurrence and enter the queue, and a task already running waits for its next occurrence.
-- A start the task refuses, such as one whose agent was removed, still moves the schedule on, so the refusal is reported once rather than every second.
local function processSchedules()
    if state.stopping then
        return
    end

    local current = cron.epoch(now())

    for _, task in ipairs(state.tasks) do
        local schedule = task.schedule
        local due = schedule ~= nil and schedule.enabled and (cron.epoch(schedule.nextRunAtUtc) or math.huge) <= current and engine.runState(task.id) == "idle"

        if due then
            local advanced, following = pcall(advanceSchedule, schedule, current)
            local started, refused = false, following

            if advanced then
                started, refused = pcall(engine.startTask, task.id, following)
            end

            if not started then
                task.schedule = advanced and following or paused(schedule)
                report(refused, translate(advanced and "ai.error.task-run" or "ai.error.schedule-save"))

                workpane.task(function()
                    local _, failure = store.saveSchedule(task):await()

                    if failure ~= nil then
                        report(failure, translate("ai.error.schedule-save"))
                    end
                end)
            end
        end
    end
end

-- The servers are connected once each and their tools join the catalog every agent receives, restarted whenever their settings change.
-- Answers a promise that settles once every server has finished starting, whether it came up or failed within the time it is given.
function engine.restartServers()
    for _, client in ipairs(state.servers) do
        client:stop()
    end

    state.servers = {}
    local starts = {}

    for _, descriptor in ipairs(preferences.mcpServers()) do
        local client

        client = mcp.new(descriptor, catalog.limit("serverStartTimeoutMs"), {
            toolsChanged = function()
                announce("servers")
            end,
            failed = function(failure)
                report(failure, translate("ai.error.server-failed"))
            end,
            progress = function(params)
                workpane.log.info("mcp", tostring(params.message or params.progressToken or ""), { server = descriptor.id, progress = params.progress, total = params.total })
            end,
            sampling = function(params, maximumTokens)
                return engine.sample(params, maximumTokens)
            end,
        })

        state.servers[#state.servers + 1] = client
        local started, finish = async.deferred()
        starts[#starts + 1] = started

        workpane.task(function()
            local ran, failure = pcall(client.start, client)
            finish()
            announce("servers")

            if not ran then
                report(failure, translate("ai.error.server-failed"))
            end
        end)
    end

    announce("servers")

    return async.all(starts)
end

-- A part a server sends to sample is text, an image or an audio with its bytes in Base64, and anything else is refused by name.
local function sampledPart(content)
    local kind = type(content) == "table" and content.type or nil

    if kind == "text" and type(content.text) == "string" then
        return { type = "text", text = content.text }
    end

    local decoded, data = false, nil

    if (kind == "image" or kind == "audio") and type(content.data) == "string" then
        decoded, data = pcall(crypto.base64Decode, content.data)
    end

    if kind == "image" and decoded then
        return { type = "image", mediaType = content.mimeType, data = data }
    end

    if kind == "audio" and decoded then
        return { type = "audio", format = content.mimeType == "audio/mpeg" and "mp3" or "wav", data = data }
    end

    refuse("ai_sampling_invalid", "A sampled message carries a part that is not text, an image or an audio", tostring(kind))
end

-- A server allowed to sample is answered by the default connection within the budget its settings give it, and what it sends is fitted to the model like any conversation.
function engine.sample(params, maximumTokens)
    local list = {}

    if type(params.systemPrompt) == "string" and params.systemPrompt:match("%S") ~= nil then
        list[1] = { role = "system", content = params.systemPrompt }
    end

    for _, message in ipairs(type(params.messages) == "table" and params.messages or {}) do
        local parts = {}
        local contents = type(message.content) == "table" and message.content[1] ~= nil and message.content or { message.content }

        for index, content in ipairs(contents) do
            parts[index] = sampledPart(content)
        end

        list[#list + 1] = { role = message.role == "assistant" and "assistant" or "user", content = parts }
    end

    local answer = completion.answer({ messages = list, maximumTokens = maximumTokens > 0 and maximumTokens or nil })

    return { role = "assistant", model = answer.model, content = { type = "text", text = answer.content } }
end

function engine.toolCount(serverId)
    for _, client in ipairs(state.servers) do
        if client.descriptor.id == serverId then
            return #client.tools
        end
    end

    return 0
end

-- The stored board comes back as it was left: tasks that were in Doing without a queue entry are queued again and runs left marked running are closed.
function engine.load()
    local loaded = store.load()
    state.workspaces = loaded.workspaces
    state.tasks = loaded.tasks
    state.queue = loaded.queue
    state.sequences = loaded.sequences

    -- A run left marked running was interrupted, which the store closes as cancelled below, so its outcome reads the same on the card.
    for taskId, outcome in pairs(loaded.outcomes) do
        local interrupted = outcome.status == "running"
        state.outcomes[taskId] = { status = interrupted and "cancelled" or outcome.status, stopReason = interrupted and "cancelled" or outcome.stopReason, errorMessage = outcome.status == "failed" and outcome.errorMessage or "" }
    end

    for _, task in ipairs(state.tasks) do
        if task.column == "doing" and not queued(task.id) then
            state.queue[#state.queue + 1] = task.id
        end
    end

    workpane.await(store.closeInterrupted(now()))

    wakeScheduler()
    local started = engine.restartServers()

    -- A higher limit of runs at the same time starts the runs that waited for room.
    preferences.listen(function(key)
        if key == "parallelExecutions" then
            engine.dispatch()
        end
    end)

    -- The runs the last session left queued wait for the servers, so they start with the tools of every server that came up.
    workpane.task(function()
        started:await()
        engine.dispatch()
    end)
end

-- The earliest occurrence of the enabled schedules, in seconds since the epoch, or nothing when no schedule is enabled.
local function earliestOccurrence()
    local earliest = nil

    for _, task in ipairs(state.tasks) do
        local occurrence = task.schedule ~= nil and task.schedule.enabled and cron.epoch(task.schedule.nextRunAtUtc) or nil

        if occurrence ~= nil then
            earliest = math.min(earliest or occurrence, occurrence)
        end
    end

    return earliest
end

-- The scheduler runs only while a schedule is enabled and sleeps until its earliest occurrence, at most a minute so a machine waking from sleep catches up, so an idle product is never woken for it.
local function schedule()
    state.scheduling = true

    while not state.stopping do
        local earliest = earliestOccurrence()

        if earliest == nil then
            break
        end

        if earliest <= os.time() then
            processSchedules()
        end

        -- A schedule due for a task still running waits for the change its run makes when it ends.
        local remaining = (earliestOccurrence() or os.time()) - os.time()
        local wait = remaining > 0 and math.min(remaining * 1000, catalog.limit("scheduleWakeupMs")) or catalog.limit("scheduleWakeupMs")
        local changed, signal = async.deferred()
        state.scheduleChanged = signal
        async.race({ async.sleep(wait), changed }):await()
        state.scheduleChanged = nil
    end

    state.scheduling = false
end

-- A scheduler that sleeps wakes at once to read the schedules again, and a scheduler that ended starts again for a schedule that appeared.
wakeScheduler = function()
    if state.scheduleChanged ~= nil then
        state.scheduleChanged()
        return
    end

    if not state.scheduling and not state.stopping and earliestOccurrence() ~= nil then
        workpane.task(schedule)
    end
end

-- The product closes with every run abandoned where it stood, so the next start closes those runs and queues their tasks again.
function engine.stop()
    state.stopping = true

    if state.scheduleChanged ~= nil then
        state.scheduleChanged()
    end

    for _, execution in pairs(state.active) do
        execution.finished = true

        if execution.client ~= nil then
            execution.client:cancel()
        end

        if execution.summary ~= nil then
            execution.summary:cancel()
        end

        if execution.run ~= nil and not execution.run.done then
            execution.run:cancel()
        end

        for _, call in ipairs(execution.calls or {}) do
            if call.started and not call.finished then
                tools.cancel(call.call.id)
            end
        end
    end

    state.active = {}

    for _, client in ipairs(state.servers) do
        client:stop()
    end
end

return engine
