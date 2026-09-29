-- The Tasks view: a tab per workspace and a board of five columns whose cards run, stop, open, edit and move the tasks.
local engine = include("engine")
local status = include("views/status")
local taskView = include("views/task")
local taskDialog = include("views/task-dialog")

local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

local view = {}

local columns = { "todo", "doing", "blocked", "review", "done" }
local dragKind = "ai.task"
local folderActions = { { id = "workspace.folder.open", key = "ai.task.open-workdir", icon = "edit" }, { id = "workspace.folder.serve", key = "ai.task.serve-workdir", icon = "web-server" } }
local controls
local holders = {}

local function attempt(action, messageKey)
    workpane.task(function()
        local done, failure = pcall(action)

        if not done then
            local code = type(failure) == "table" and failure.code or ""
            local message = code == "ai_agent_unknown" and translate("ai.error.agent-removed", failure.detail) or code == "ai_tasks_workspace_required" and translate("ai.error.workspace-required") or translate(messageKey)
            workpane.log.error("ai.tasks", type(failure) == "table" and tostring(failure.message) or tostring(failure), { code = type(failure) == "table" and failure.code or "", detail = type(failure) == "table" and failure.detail or "" })
            workpane.notify.error(translate("ai.error.title"), message)
        end
    end)
end

-- A folder reaches the plugin that answers for it, which the menu offers only while that plugin runs.
local function offer(capability, path)
    workpane.task(function()
        local _, failure = workpane.capabilities.request(capability, { path = path }):await()

        if failure ~= nil then
            workpane.log.error("ai.tasks", tostring(failure.message), { code = failure.code, detail = failure.detail })
            workpane.notify.error(translate("ai.error.title"), translate("ai.error.destination-failed"))
        end
    end)
end

local function note(task)
    local outcome = engine.outcome(task.id)

    if engine.runState(task.id) ~= "idle" or outcome == nil then
        return nil
    end

    if outcome.status == "failed" and outcome.errorMessage ~= "" then
        return text("ai.task.last-error", outcome.errorMessage), "danger"
    end

    if outcome.stopReason ~= "answered" and outcome.stopReason ~= "failed" then
        return text("ai.stop-reason." .. outcome.stopReason), "warning"
    end

    return nil
end

local function scheduleText(task)
    local schedule = task.schedule

    if schedule == nil then
        return nil
    end

    if schedule.enabled then
        return text("ai.task.scheduled", workpane.time.localPresentation(schedule.nextRunAtUtc))
    end

    return schedule.lastTriggeredAtUtc ~= "" and text("ai.task.schedule-done", workpane.time.localPresentation(schedule.lastTriggeredAtUtc)) or nil
end

local function action(icon, tooltip, enabled, handler)
    return ui.button({ icon = icon, variant = "icon", tooltip = text(tooltip), enabled = enabled, onClick = handler })
end

local function confirmRemoval(task)
    workpane.task(function()
        local confirmed = workpane.await(workpane.dialogs.confirm({ title = translate("ai.task.remove"), message = translate("ai.task.remove-message"), detail = task.title, confirmText = translate("ai.task.remove"), destructive = true }))

        if confirmed then
            attempt(function()
                engine.removeTask(task.id)
            end, "ai.error.task-save")
        end
    end)
end

local function confirmUnscheduling(task)
    workpane.task(function()
        local confirmed = workpane.await(workpane.dialogs.confirm({ title = translate("ai.task.schedule-remove"), message = translate("ai.task.schedule-remove-message"), detail = task.title, confirmText = translate("ai.task.schedule-remove"), destructive = true }))

        if confirmed then
            attempt(function()
                engine.removeSchedule(task.id)
            end, "ai.error.task-save")
        end
    end)
end

-- A card that never ran opens its form on a double click, and any other card opens its surface.
local function card(task)
    local idle = engine.runState(task.id) == "idle"
    local badgeText, tone = status.badge(task)
    local children = { ui.label({ text = task.title, style = "strong" }) }

    if task.description ~= "" then
        children[#children + 1] = ui.label({ text = task.description, style = "muted" })
    end

    children[#children + 1] = ui.row({ spacing = 6 }, { ui.badge({ text = badgeText, tone = tone }) })

    if not idle then
        children[#children + 1] = ui.label({ text = status.phase(task.id), style = "caption", color = "text-muted" })
    end

    local explained, tone = note(task)

    if explained ~= nil then
        children[#children + 1] = ui.alert({ text = explained, tone = tone })
    end

    local scheduled = scheduleText(task)

    if scheduled ~= nil then
        children[#children + 1] = ui.label({ text = scheduled, style = "caption", color = "text-muted" })
    end

    -- A card starts or stops its task from one place, since only one of them applies at a time.
    local actions = {
        idle and action("start", "ai.task.start", true, function()
            attempt(function()
                engine.startTask(task.id)
            end, "ai.error.task-run")
        end) or action("stop", "ai.task.stop", true, function()
            attempt(function()
                engine.stopTask(task.id)
            end, "ai.error.stop")
        end),
    }

    if task.executionKind == "agent" then
        actions[#actions + 1] = action("chat", "ai.task.chat", true, function()
            taskView.open(task.id, "chat")
        end)
    end

    actions[#actions + 1] = action("information", "ai.task.info", true, function()
        taskView.open(task.id, "executions")
    end)

    if task.schedule ~= nil then
        actions[#actions + 1] = action("schedule", "ai.task.schedule-remove", idle, function()
            confirmUnscheduling(task)
        end)
    end

    local folderItems = {}

    for _, folderAction in ipairs(folderActions) do
        if workpane.capabilities.available(folderAction.id) then
            folderItems[#folderItems + 1] = { id = folderAction.id, text = text(folderAction.key), icon = folderAction.icon }
        end
    end

    if task.workdir ~= "" and #folderItems > 0 then
        actions[#actions + 1] = ui.menuButton({ icon = "folder", variant = "icon", tooltip = text("ai.task.workdir"), items = folderItems, onSelect = function(event)
            offer(event.item, task.workdir)
        end })
    end

    actions[#actions + 1] = action("edit", "ai.task.edit", idle, function()
        taskDialog.open(task.workspaceId, task)
    end)

    actions[#actions + 1] = ui.spacer({ grow = 1 })
    actions[#actions + 1] = action("clear", "ai.task.remove", idle, function()
        confirmRemoval(task)
    end)

    children[#children + 1] = ui.row({ spacing = 2 }, actions)

    -- A running card is outlined in the success color and a queued one in the warning color, so the board shows what moves at a glance.
    local outlines = { running = "success", queued = "warning", idle = "border" }

    return ui.card({ padding = { 10, 10, 8, 10 }, spacing = 6, background = "raised", radius = 3, outline = outlines[engine.runState(task.id)], drag = { kind = dragKind, value = task.id, label = task.title }, onActivate = function()
        local neverRan = task.column == "todo" and idle and engine.outcome(task.id) == nil

        if neverRan then
            taskDialog.open(task.workspaceId, task)
        else
            taskView.open(task.id, task.executionKind == "agent" and "chat" or "executions")
        end
    end }, children)
end

local function render()
    if controls == nil then
        return
    end

    local items = {}
    local active = engine.activeWorkspace()

    local workspaces = engine.workspaces()

    for index, workspace in ipairs(workspaces) do
        items[index] = { id = workspace.id, text = workspace.name, closable = #workspaces > 1 }
    end

    controls.tabs:set({ items = items, current = active ~= nil and active.id or "" })
    controls.tabBar:set({ visible = #items > 0 })
    controls.kanban:set({ visible = #items > 0 })
    controls.empty:set({ visible = #items == 0 })
    controls.addTask:set({ enabled = active ~= nil })
    controls.rename:set({ enabled = active ~= nil })

    holders = {}

    for _, column in ipairs(columns) do
        local cards = {}

        for _, task in ipairs(active ~= nil and engine.tasks(active.id) or {}) do
            if task.column == column then
                holders[task.id] = ui.column({}, { card(task) })
                cards[#cards + 1] = holders[task.id]
            end
        end

        controls.lists[column]:setChildren(cards)
    end
end

-- A run that changed builds again only the card of its task, which the board shows only when the task belongs to the active workspace.
local function renderCard(taskId)
    local task = engine.task(taskId)

    if controls == nil or task == nil or holders[taskId] == nil then
        return
    end

    holders[taskId]:setChildren({ card(task) })
end

local function askName(titleKey, value, handler)
    workpane.task(function()
        local name = workpane.await(workpane.dialogs.prompt({ title = translate(titleKey), message = translate("ai.workspace.name"), value = value or "" }))

        if name ~= nil and name:match("%S") then
            attempt(function()
                handler(name)
            end, "ai.error.workspace-save")
        end
    end)
end

local function createWorkspace()
    askName("ai.workspace.add", "", engine.createWorkspace)
end

-- Closing a workspace tab asks first, because every task inside it goes with it.
local function removeWorkspace(id)
    workpane.task(function()
        local confirmed = workpane.await(workpane.dialogs.confirm({ title = translate("ai.workspace.remove"), message = translate("ai.workspace.remove-message"), confirmText = translate("ai.workspace.remove"), destructive = true }))

        if confirmed then
            attempt(function()
                engine.removeWorkspace(id)
            end, "ai.error.workspace-save")
        end
    end)
end

local function newTask()
    local active = engine.activeWorkspace()

    if active ~= nil then
        taskDialog.open(active.id, nil)
    end
end

function view.build()
    controls = { lists = {} }
    controls.tabs = ui.tabs({ items = {}, closable = true, grow = 1, onSelect = function(event)
        attempt(function()
            engine.activateWorkspace(event.id)
        end, "ai.error.workspace-save")
    end, onClose = function(event)
        removeWorkspace(event.id)
    end })

    controls.tabBar = ui.row({ padding = { 0, 8, 0, 0 }, background = "panel", borders = { "bottom" }, visible = false }, { controls.tabs })
    controls.addTask = ui.button({ icon = "add", variant = "icon", tooltip = text("ai.task.add"), onClick = newTask })
    controls.rename = ui.button({ icon = "edit", variant = "icon", tooltip = text("ai.workspace.rename"), onClick = function()
        local active = engine.activeWorkspace()

        if active ~= nil then
            askName("ai.workspace.rename", active.name, function(name)
                engine.renameWorkspace(active.id, name)
            end)
        end
    end })

    local lanes = {}

    for index, column in ipairs(columns) do
        controls.lists[column] = ui.column({ padding = 8, spacing = 8 }, {})

        if index > 1 then
            lanes[#lanes + 1] = ui.divider({ orientation = "vertical" })
        end

        lanes[#lanes + 1] = ui.column({ grow = 1, width = 0, height = 0, accepts = { dragKind }, onDrop = function(event)
            attempt(function()
                engine.moveTask(event.value, column)
            end, "ai.error.task-save")
        end }, {
            ui.row({ padding = { 12, 8, 4, 8 }, justify = "center" }, { ui.label({ text = text("ai.column." .. column), style = "strong" }) }),
            ui.scroll({ grow = 1 }, controls.lists[column]),
        })
    end

    controls.kanban = ui.row({ grow = 1, height = 0, visible = false }, lanes)
    controls.empty = ui.column({ grow = 1, justify = "center", spacing = 14 }, {
        ui.emptyState({ text = text("ai.workspace.empty"), icon = "tasks" }),
        ui.button({ text = text("ai.workspace.add"), icon = "workspace", variant = "primary", align = "center", onClick = createWorkspace }),
    })

    engine.listen(function(kind, taskId)
        if kind == "tasks" or kind == "workspaces" then
            render()
        elseif kind == "run" then
            renderCard(taskId)
        end
    end)

    for _, folderAction in ipairs(folderActions) do
        workpane.capabilities.watch(folderAction.id, render)
    end

    render()

    return ui.column({}, {
        ui.pageHeader({ title = text("ai.tasks.title") }, {
            controls.addTask,
            ui.button({ icon = "workspace", variant = "icon", tooltip = text("ai.workspace.add"), onClick = createWorkspace }),
            controls.rename,
        }),
        controls.tabBar,
        controls.kanban,
        controls.empty,
    })
end

return view
