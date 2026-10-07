-- Keeps workspaces, tasks, schedules, the queue, executions, their logs and the conversations in the tables of the plugin, checked whole when read.
local crypto = require("crypto")
local codec = include("codec")

local database = workpane.database

local store = {}

store.migrations = {
    {
        "CREATE TABLE ai__workspaces(id TEXT PRIMARY KEY NOT NULL, name TEXT NOT NULL, position INTEGER NOT NULL CHECK(position >= 0), active INTEGER NOT NULL CHECK(active IN (0, 1)), created_at_utc TEXT NOT NULL, updated_at_utc TEXT NOT NULL) STRICT",
        "CREATE TABLE ai__tasks(id TEXT PRIMARY KEY NOT NULL, workspace_id TEXT NOT NULL REFERENCES ai__workspaces(id) ON DELETE CASCADE, title TEXT NOT NULL, description TEXT NOT NULL, prompt TEXT NOT NULL, issue_url TEXT NOT NULL, agent_id TEXT NOT NULL, execution_kind TEXT NOT NULL CHECK(execution_kind IN ('agent', 'command')), workdir TEXT NOT NULL, command TEXT NOT NULL, command_timeout_seconds INTEGER NOT NULL CHECK(command_timeout_seconds >= 0), column_name TEXT NOT NULL CHECK(column_name IN ('todo', 'doing', 'blocked', 'review', 'done')), position INTEGER NOT NULL CHECK(position >= 0), created_at_utc TEXT NOT NULL, updated_at_utc TEXT NOT NULL) STRICT",
        "CREATE TABLE ai__schedules(task_id TEXT PRIMARY KEY NOT NULL REFERENCES ai__tasks(id) ON DELETE CASCADE, schedule_kind TEXT NOT NULL CHECK(schedule_kind IN ('once', 'interval', 'cron')), enabled INTEGER NOT NULL CHECK(enabled IN (0, 1)), once_at_utc TEXT NOT NULL, interval_seconds INTEGER NOT NULL CHECK(interval_seconds >= 0), cron_expression TEXT NOT NULL, time_zone TEXT NOT NULL, next_run_at_utc TEXT NOT NULL, last_triggered_at_utc TEXT NOT NULL) STRICT",
        "CREATE TABLE ai__queue(task_id TEXT PRIMARY KEY NOT NULL REFERENCES ai__tasks(id) ON DELETE CASCADE, queued_at_utc TEXT NOT NULL) STRICT",
        "CREATE TABLE ai__executions(id TEXT PRIMARY KEY NOT NULL, task_id TEXT NOT NULL REFERENCES ai__tasks(id) ON DELETE CASCADE, status TEXT NOT NULL CHECK(status IN ('running', 'succeeded', 'failed', 'cancelled')), started_at_utc TEXT NOT NULL, finished_at_utc TEXT NOT NULL, input_tokens INTEGER NOT NULL CHECK(input_tokens >= 0), output_tokens INTEGER NOT NULL CHECK(output_tokens >= 0), finish_reason TEXT NOT NULL, error_message TEXT NOT NULL, content TEXT NOT NULL, stop_reason TEXT NOT NULL CHECK(stop_reason IN ('answered', 'iteration-limit', 'output-budget', 'tool-repetition', 'cancelled', 'failed')), provider_id TEXT NOT NULL, model_id TEXT NOT NULL) STRICT",
        "CREATE TABLE ai__logs(id TEXT PRIMARY KEY NOT NULL, execution_id TEXT NOT NULL REFERENCES ai__executions(id) ON DELETE CASCADE, sequence INTEGER NOT NULL CHECK(sequence >= 0), timestamp_utc TEXT NOT NULL, level TEXT NOT NULL CHECK(level IN ('debug', 'info', 'warning', 'error')), kind TEXT NOT NULL, detail TEXT NOT NULL) STRICT",
        "CREATE TABLE ai__messages(id TEXT PRIMARY KEY NOT NULL, task_id TEXT NOT NULL REFERENCES ai__tasks(id) ON DELETE CASCADE, sequence INTEGER NOT NULL CHECK(sequence >= 1), role TEXT NOT NULL CHECK(role IN ('user', 'assistant', 'tool')), content TEXT NOT NULL, tool_calls TEXT NOT NULL, tool_call_id TEXT NOT NULL, summarized_until INTEGER NOT NULL CHECK(summarized_until >= 0), parts_json TEXT NOT NULL, created_at_utc TEXT NOT NULL, UNIQUE(task_id, sequence)) STRICT",
        "CREATE UNIQUE INDEX ai__active_workspace ON ai__workspaces(active) WHERE active = 1",
        "CREATE INDEX ai__tasks_workspace ON ai__tasks(workspace_id, column_name, position)",
        "CREATE INDEX ai__schedules_due ON ai__schedules(enabled, next_run_at_utc) WHERE enabled = 1",
        "CREATE INDEX ai__executions_task ON ai__executions(task_id, started_at_utc)",
        "CREATE INDEX ai__logs_execution ON ai__logs(execution_id, sequence)",
    },
}

local columns = { todo = true, doing = true, blocked = true, review = true, done = true }
local keptExecutions = 100
local statuses = { running = true, succeeded = true, failed = true, cancelled = true }
local stopReasons = { answered = true, ["iteration-limit"] = true, ["output-budget"] = true, ["tool-repetition"] = true, cancelled = true, failed = true }

local function invalid(detail)
    workpane.database.invalid(detail)
end

local function timestamp(value)
    return type(value) == "string" and value:match("^%d%d%d%d%-%d%d%-%d%dT%d%d:%d%d:%d%d%.%d%d%dZ$") ~= nil
end

local function validSchedule(schedule)
    if schedule.kind == "once" then
        return timestamp(schedule.onceAtUtc) and schedule.intervalSeconds == 0 and schedule.cronExpression == "" and schedule.timeZone == "" and (not schedule.enabled or schedule.nextRunAtUtc == schedule.onceAtUtc)
    end

    if schedule.kind == "interval" then
        return schedule.intervalSeconds >= 60 and schedule.onceAtUtc == "" and schedule.cronExpression == "" and schedule.timeZone == ""
    end

    return schedule.kind == "cron" and schedule.cronExpression ~= "" and schedule.timeZone ~= "" and schedule.onceAtUtc == "" and schedule.intervalSeconds == 0
end

-- A task names a title, an address that is empty or on the web, a working directory and what it runs, and nothing else.
function store.validTask(task)
    local issue = task.issueUrl == "" or task.issueUrl:match("^https?://[^/%s]+") ~= nil
    local timeout = math.type(task.commandTimeoutSeconds) == "integer" and task.commandTimeoutSeconds >= 0 and task.commandTimeoutSeconds <= 86400
    local command = task.executionKind == "command" and task.command ~= "" and workpane.files.absolute(task.workdir) and task.agentId == ""
    local agent = task.executionKind == "agent" and task.prompt ~= "" and task.agentId ~= "" and (task.workdir == "" or workpane.files.absolute(task.workdir))
    return task.title:match("^%s*(.-)%s*$") ~= "" and issue and timeout and (command or agent) and columns[task.column] ~= nil
end

-- Positions run from zero without holes, exactly one workspace is active, every task belongs to a workspace and every schedule agrees with itself.
function store.load()
    local workspaces = {}
    local byWorkspace = {}
    local active = 0

    for index, row in ipairs(workpane.await(database.query("SELECT id, name, position, active, created_at_utc, updated_at_utc FROM ai__workspaces ORDER BY position", {}))) do
        if row.position ~= index - 1 or row.name:match("^%s*(.-)%s*$") == "" or not timestamp(row.created_at_utc) or not timestamp(row.updated_at_utc) or row.updated_at_utc < row.created_at_utc then
            invalid(row.id)
        end

        active = active + row.active
        workspaces[index] = { id = row.id, name = row.name, active = row.active == 1, createdAt = row.created_at_utc, updatedAt = row.updated_at_utc }
        byWorkspace[row.id] = workspaces[index]
    end

    if #workspaces > 0 and active ~= 1 then
        invalid("active")
    end

    local schedules = {}

    for _, row in ipairs(workpane.await(database.query("SELECT task_id, schedule_kind, enabled, once_at_utc, interval_seconds, cron_expression, time_zone, next_run_at_utc, last_triggered_at_utc FROM ai__schedules", {}))) do
        local schedule = { kind = row.schedule_kind, enabled = row.enabled == 1, onceAtUtc = row.once_at_utc, intervalSeconds = row.interval_seconds, cronExpression = row.cron_expression, timeZone = row.time_zone, nextRunAtUtc = row.next_run_at_utc, lastTriggeredAtUtc = row.last_triggered_at_utc }

        if not validSchedule(schedule) or schedule.enabled ~= timestamp(schedule.nextRunAtUtc) then
            invalid(row.task_id)
        end

        schedules[row.task_id] = schedule
    end

    local tasks = {}

    for _, row in ipairs(workpane.await(database.query("SELECT id, workspace_id, title, description, prompt, issue_url, agent_id, execution_kind, workdir, command, command_timeout_seconds, column_name, position, created_at_utc, updated_at_utc FROM ai__tasks ORDER BY workspace_id, column_name, position", {}))) do
        local task = { id = row.id, workspaceId = row.workspace_id, title = row.title, description = row.description, prompt = row.prompt, issueUrl = row.issue_url, agentId = row.agent_id, executionKind = row.execution_kind, workdir = row.workdir, command = row.command, commandTimeoutSeconds = row.command_timeout_seconds, column = row.column_name, position = row.position, createdAt = row.created_at_utc, updatedAt = row.updated_at_utc, schedule = schedules[row.id] }

        if byWorkspace[task.workspaceId] == nil or not store.validTask(task) or not timestamp(task.createdAt) or not timestamp(task.updatedAt) or task.updatedAt < task.createdAt then
            invalid(row.id)
        end

        tasks[#tasks + 1] = task
    end

    local queue = {}

    for _, row in ipairs(workpane.await(database.query("SELECT task_id FROM ai__queue ORDER BY queued_at_utc", {}))) do
        queue[#queue + 1] = row.task_id
    end

    -- A card says what happened to its task, so the newest run of each one is read back with its reason.
    local outcomes = {}

    for _, row in ipairs(workpane.await(database.query("SELECT task_id, status, error_message, stop_reason, MAX(started_at_utc) AS started FROM ai__executions GROUP BY task_id", {}))) do
        if not statuses[row.status] or not stopReasons[row.stop_reason] then
            invalid(row.task_id)
        end

        outcomes[row.task_id] = { status = row.status, errorMessage = row.error_message, stopReason = row.stop_reason }
    end

    local sequences = {}

    for _, row in ipairs(workpane.await(database.query("SELECT task_id, MAX(sequence) AS newest FROM ai__messages GROUP BY task_id", {}))) do
        sequences[row.task_id] = row.newest
    end

    return { workspaces = workspaces, tasks = tasks, queue = queue, outcomes = outcomes, sequences = sequences }
end

local function statement(sql, bindings)
    return { sql = sql, bindings = bindings }
end

local function scheduleStatements(task)
    local list = { statement("DELETE FROM ai__schedules WHERE task_id = ?", { task.id }) }
    local schedule = task.schedule

    if schedule ~= nil then
        list[#list + 1] = statement("INSERT INTO ai__schedules(task_id, schedule_kind, enabled, once_at_utc, interval_seconds, cron_expression, time_zone, next_run_at_utc, last_triggered_at_utc) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)", { task.id, schedule.kind, schedule.enabled and 1 or 0, schedule.onceAtUtc, schedule.intervalSeconds, schedule.cronExpression, schedule.timeZone, schedule.nextRunAtUtc, schedule.lastTriggeredAtUtc })
    end

    return list
end

local function run(statements)
    return database.transaction(statements)
end

function store.saveWorkspaces(workspaces)
    local list = { statement("UPDATE ai__workspaces SET active = 0", {}) }

    for index, workspace in ipairs(workspaces) do
        list[#list + 1] = statement("INSERT INTO ai__workspaces(id, name, position, active, created_at_utc, updated_at_utc) VALUES(?, ?, ?, 0, ?, ?) ON CONFLICT(id) DO UPDATE SET name = excluded.name, position = excluded.position, updated_at_utc = excluded.updated_at_utc", { workspace.id, workspace.name, index - 1, workspace.createdAt, workspace.updatedAt })
    end

    for _, workspace in ipairs(workspaces) do
        if workspace.active then
            list[#list + 1] = statement("UPDATE ai__workspaces SET active = 1 WHERE id = ?", { workspace.id })
        end
    end

    return run(list)
end

-- The tasks of a removed workspace go with it, and the workspaces left behind close the gap it leaves.
function store.removeWorkspace(id, remaining)
    local list = { statement("DELETE FROM ai__workspaces WHERE id = ?", { id }), statement("UPDATE ai__workspaces SET active = 0", {}) }

    for index, workspace in ipairs(remaining) do
        list[#list + 1] = statement("UPDATE ai__workspaces SET position = ?, active = ? WHERE id = ?", { index - 1, workspace.active and 1 or 0, workspace.id })
    end

    return run(list)
end

function store.saveTask(task)
    local list = {
        statement("INSERT INTO ai__tasks(id, workspace_id, title, description, prompt, issue_url, agent_id, execution_kind, workdir, command, command_timeout_seconds, column_name, position, created_at_utc, updated_at_utc) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) ON CONFLICT(id) DO UPDATE SET title = excluded.title, description = excluded.description, prompt = excluded.prompt, issue_url = excluded.issue_url, agent_id = excluded.agent_id, execution_kind = excluded.execution_kind, workdir = excluded.workdir, command = excluded.command, command_timeout_seconds = excluded.command_timeout_seconds, column_name = excluded.column_name, position = excluded.position, updated_at_utc = excluded.updated_at_utc", { task.id, task.workspaceId, task.title, task.description, task.prompt, task.issueUrl, task.agentId, task.executionKind, task.workdir, task.command, task.commandTimeoutSeconds, task.column, task.position, task.createdAt, task.updatedAt }),
    }

    for _, entry in ipairs(scheduleStatements(task)) do
        list[#list + 1] = entry
    end

    return run(list)
end

function store.saveSchedule(task)
    return run(scheduleStatements(task))
end

function store.removeTask(id)
    return run({ statement("DELETE FROM ai__tasks WHERE id = ?", { id }) })
end

function store.placeTasks(tasks)
    local list = {}

    for _, task in ipairs(tasks) do
        list[#list + 1] = statement("UPDATE ai__tasks SET column_name = ?, position = ?, updated_at_utc = ? WHERE id = ?", { task.column, task.position, task.updatedAt, task.id })
    end

    return run(list)
end

-- A task leaves its schedule and enters the queue in one write, so a scheduler waking again never dispatches it twice.
function store.enqueue(task, now)
    local list = { statement("INSERT OR REPLACE INTO ai__queue(task_id, queued_at_utc) VALUES(?, ?)", { task.id, now }), statement("UPDATE ai__tasks SET column_name = 'doing', updated_at_utc = ? WHERE id = ?", { now, task.id }) }

    for _, entry in ipairs(scheduleStatements(task)) do
        list[#list + 1] = entry
    end

    return run(list)
end

function store.settle(taskId, column, now)
    return run({ statement("DELETE FROM ai__queue WHERE task_id = ?", { taskId }), statement("UPDATE ai__tasks SET column_name = ?, updated_at_utc = ? WHERE id = ?", { column, now, taskId }) })
end

-- A task keeps the runs its view lists, so the oldest run and its log go as a new one starts.
function store.startExecution(record)
    return run({ statement("INSERT INTO ai__executions(id, task_id, status, started_at_utc, finished_at_utc, input_tokens, output_tokens, finish_reason, error_message, content, stop_reason, provider_id, model_id) VALUES(?, ?, 'running', ?, '', 0, 0, '', '', '', 'answered', '', '')", { record.id, record.taskId, record.startedAt }), statement("DELETE FROM ai__executions WHERE task_id = ? AND id NOT IN (SELECT id FROM ai__executions WHERE task_id = ? ORDER BY started_at_utc DESC, id DESC LIMIT ?)", { record.taskId, record.taskId, keptExecutions }) })
end

function store.finishExecution(record)
    return run({ statement("UPDATE ai__executions SET status = ?, finished_at_utc = ?, input_tokens = ?, output_tokens = ?, finish_reason = ?, error_message = ?, content = ?, stop_reason = ?, provider_id = ?, model_id = ? WHERE id = ?", { record.status, record.finishedAt, record.inputTokens, record.outputTokens, record.finishReason, record.errorMessage, record.content, record.stopReason, record.providerId, record.modelId, record.id }) })
end

-- A run left marked as running by a product that ended mid way is closed as cancelled when the product starts again.
function store.closeInterrupted(now)
    return run({ statement("UPDATE ai__executions SET status = 'cancelled', stop_reason = 'cancelled', finished_at_utc = ? WHERE status = 'running'", { now }) })
end

function store.appendLog(entry)
    return run({ statement("INSERT INTO ai__logs(id, execution_id, sequence, timestamp_utc, level, kind, detail) VALUES(?, ?, ?, ?, ?, ?, ?)", { entry.id, entry.executionId, entry.sequence, entry.timestamp, entry.level, entry.kind, entry.detail }) })
end

-- The parts a message carries besides its text are kept as one JSON list, with the bytes of each part written in Base64.
local function encodedParts(parts)
    local list = codec.list()

    for index, entry in ipairs(parts) do
        local copy = {}

        for key, value in pairs(entry) do
            copy[key] = value
        end

        copy.data = entry.data ~= nil and crypto.base64Encode(entry.data) or nil
        list[index] = copy
    end

    return codec.encode(list)
end

-- Stored parts are read back with their bytes, and a list that is not what the plugin wrote refuses the page it belongs to.
local function decodedParts(text, id)
    local list = codec.read(text)

    if type(list) ~= "table" or not (codec.isList(list) or next(list) == nil) then
        invalid(id)
    end

    for _, entry in ipairs(list) do
        if type(entry) ~= "table" or type(entry.type) ~= "string" or (entry.data ~= nil and type(entry.data) ~= "string") then
            invalid(id)
        end

        if entry.data ~= nil then
            local decoded, data = pcall(crypto.base64Decode, entry.data)

            if not decoded then
                invalid(id)
            end

            entry.data = data
        end
    end

    return list
end

function store.appendMessages(messages)
    local list = {}

    for _, message in ipairs(messages) do
        list[#list + 1] = statement("INSERT INTO ai__messages(id, task_id, sequence, role, content, tool_calls, tool_call_id, summarized_until, parts_json, created_at_utc) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", { message.id, message.taskId, message.sequence, message.role, message.content, message.toolCalls, message.toolCallId, message.summarizedUntil, encodedParts(message.parts), message.createdAt })
    end

    return run(list)
end

local function messageOf(row, taskId)
    return { id = row.id, taskId = taskId, sequence = row.sequence, role = row.role, content = row.content, toolCalls = row.tool_calls, toolCallId = row.tool_call_id, summarizedUntil = row.summarized_until, parts = decodedParts(row.parts_json, row.id), createdAt = row.created_at_utc }
end

-- A page of the conversation is read newest first and answered oldest first, the way it is shown.
function store.messages(taskId, before, limit)
    local rows = workpane.await(database.query("SELECT id, sequence, role, content, tool_calls, tool_call_id, summarized_until, parts_json, created_at_utc FROM ai__messages WHERE task_id = ? AND sequence < ? ORDER BY sequence DESC LIMIT ?", { taskId, before, limit }))
    local page = {}

    for index = #rows, 1, -1 do
        page[#page + 1] = messageOf(rows[index], taskId)
    end

    return page
end

-- The conversation a run reads is its opening, its newest summary and every message after what that summary covers, whatever page the chat has loaded.
function store.context(taskId)
    local covered = workpane.await(database.query("SELECT COALESCE(MAX(summarized_until), 0) AS covered FROM ai__messages WHERE task_id = ?", { taskId }))[1].covered
    local rows = workpane.await(database.query("SELECT id, sequence, role, content, tool_calls, tool_call_id, summarized_until, parts_json, created_at_utc FROM ai__messages WHERE task_id = ? AND (sequence > ? OR (summarized_until > 0 AND summarized_until = ?) OR sequence = (SELECT MIN(sequence) FROM ai__messages WHERE task_id = ? AND role = 'user' AND summarized_until = 0)) ORDER BY sequence", { taskId, covered, covered, taskId }))
    local context = {}

    for index, row in ipairs(rows) do
        context[index] = messageOf(row, taskId)
    end

    return context
end

function store.clearConversation(taskId)
    return run({ statement("DELETE FROM ai__messages WHERE task_id = ?", { taskId }), statement("DELETE FROM ai__executions WHERE task_id = ?", { taskId }) })
end

function store.executions(taskId, limit)
    return database.query("SELECT id, status, started_at_utc, finished_at_utc, input_tokens, output_tokens, finish_reason, error_message, content, stop_reason, provider_id, model_id FROM ai__executions WHERE task_id = ? ORDER BY started_at_utc DESC LIMIT ?", { taskId, limit })
end

function store.logs(executionId, limit)
    return database.query("SELECT id, sequence, timestamp_utc, level, kind, detail FROM ai__logs WHERE execution_id = ? ORDER BY sequence DESC LIMIT ?", { executionId, limit })
end

return store
