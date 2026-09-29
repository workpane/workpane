-- The editor destination: one tab per open folder, the page header that opens folders and saves them, and the stored order of both.
local async = require("async")
local crypto = require("crypto")
local fs = require("fs")
local store = include("store")
local finder = include("views/finder")
local workspace = include("views/workspace")

local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

local view = {}

local stopWaitMilliseconds = 2500
local stopStepMilliseconds = 20

local workspaces = {}
local closingFolders = {}
local active
local controls
local shownOrder

local function persist()
    local snapshot = {}

    for index, opened in ipairs(workspaces) do
        snapshot[index] = opened:snapshot()
        snapshot[index].active = opened == active
    end

    store.save(snapshot)
end

local function settled()
    for _, opened in ipairs(workspaces) do
        if not opened:settled() then
            return false
        end
    end

    return true
end

local function find(id)
    for index, opened in ipairs(workspaces) do
        if opened.id == id then
            return opened, index
        end
    end

    return nil, nil
end

local function orderOf()
    local order = {}

    for index, opened in ipairs(workspaces) do
        order[index] = opened.id
    end

    return table.concat(order, " ")
end

-- The pages are sent again only when the folders changed, together with the tabs, because a page that only moved keeps its place.
local function render()
    if controls == nil then
        return
    end

    local items = {}
    local pages = {}

    for index, opened in ipairs(workspaces) do
        items[index] = { id = opened.id, text = opened:name(), tooltip = opened.root }
        pages[index] = opened.node
    end

    local shown = { items = items, current = active ~= nil and active.id or "", visible = #items > 0 }

    if orderOf() == shownOrder then
        controls.tabs:set(shown)
    else
        shownOrder = orderOf()
        controls.tabs:setChildren(pages, shown)
    end

    controls.empty:set({ visible = #items == 0 })
    controls.saveAll:set({ enabled = active ~= nil })
end

-- Only the folder in front follows its files, so the folders behind it cost nothing while they wait.
local function current(opened)
    return opened == active
end

local function create(record, stored)
    return workspace.new(record, { persist = persist, current = current }, stored)
end

-- The folders stored by the previous run come back in their order and with the same one active, and each one opens its documents and starts its servers once the editor is first shown.
function view.restore(records)
    for _, record in ipairs(records) do
        local restored = create(record, record.documents)
        workspaces[#workspaces + 1] = restored
        active = record.active and restored or active
    end
end

-- A folder already open comes to the front instead of opening twice, and a new one opens last and active.
local function openFolder(path)
    local info = type(path) == "string" and path ~= "" and fs.stat(path):await() or nil
    local canonical = info ~= nil and info.isDir and workpane.await(workpane.files.canonical(path)) or nil

    if canonical == nil then
        error({ code = "code_editor_workspace_invalid", message = "The code editor workspace is unavailable", detail = tostring(path) }, 0)
    end

    for _, opened in ipairs(workspaces) do
        if opened.root == canonical then
            active = opened
            render()
            persist()
            return opened.id
        end
    end

    local created = create({ id = crypto.uuidV4(), root = canonical }, {})
    workspaces[#workspaces + 1] = created
    active = created
    render()
    created:start()
    persist()

    return created.id
end

-- Another plugin asks with exactly a path and receives the folder it opened, with the editor brought on screen.
function view.folderRequested(payload)
    local keys = 0

    for _ in pairs(type(payload) == "table" and payload or {}) do
        keys = keys + 1
    end

    if type(payload) ~= "table" or type(payload.path) ~= "string" or keys ~= 1 then
        error({ code = "code_editor_request_invalid", message = "A request to open a folder carries exactly its path", detail = "" }, 0)
    end

    local id = openFolder(payload.path)
    workpane.shell.navigate("editor")

    return { workspaceId = id }
end

local function chooseFolder()
    local chosen = workpane.await(workpane.dialogs.selectFolder({ title = translate("code-editor.actions.open-folder"), initial = active ~= nil and active.root or workpane.system.home() }))

    if chosen == nil then
        return
    end

    local opened, failure = pcall(openFolder, chosen)

    if not opened then
        workpane.notify.error(translate("code-editor.error.title"), translate("code-editor.error.folder-unavailable") .. "\n" .. tostring(type(failure) == "table" and failure.detail or chosen))
    end
end

-- A folder closes only after the reader agrees to lose every unsaved change in it, and its neighbour comes to the front.
-- A close asked for again meanwhile waits for nothing, and the folder is found again after the confirmation, since another close may have moved it.
local function closeFolder(id)
    local closing = find(id)

    if closing == nil or closingFolders[id] then
        return
    end

    closingFolders[id] = true
    local confirmed = closing:confirmClose()
    closingFolders[id] = nil

    if not confirmed then
        return
    end

    closing:shutdown()
    local _, index = find(id)
    table.remove(workspaces, index)

    if active == closing then
        active = workspaces[math.min(index, #workspaces)]
    end

    render()
    persist()
end

function view.applyPreferences(key, value)
    for _, opened in ipairs(workspaces) do
        opened:applyPreferences(key, value)
    end
end

function view.saveAll()
    if active ~= nil then
        active:saveAll()
    end
end

function view.save()
    if active ~= nil then
        active:saveActive()
    end
end

function view.closeDocument()
    if active ~= nil then
        active:close(active.active, true)
    end
end

function view.findFile()
    if active ~= nil then
        finder.open(active.root, function(path)
            active:open(path)
        end)
    end
end

function view.goToDefinition()
    if active ~= nil then
        active:navigate("definition")
    end
end

function view.findReferences()
    if active ~= nil then
        active:navigate("references")
    end
end

-- The cursors of every document are stored, the servers of every folder are asked to shut down together, and the product closes once each one was told to exit or its grace passed.
function view.stop()
    persist()

    for _, opened in ipairs(workspaces) do
        opened:shutdown()
    end

    local waited = 0

    while waited < stopWaitMilliseconds and not settled() do
        async.sleep(stopStepMilliseconds):await()
        waited = waited + stopStepMilliseconds
    end
end

function view.build()
    for _, opened in ipairs(workspaces) do
        workpane.task(opened.start, opened)
    end

    controls = {
        tabs = ui.tabs({ items = {}, closable = true, movable = true, grow = 1, visible = false, onSelect = function(event)
            active = find(event.id)
            persist()
        end, onClose = function(event)
            closeFolder(event.id)
        end, onMove = function(event)
            local _, index = find(event.id)
            table.insert(workspaces, event.index + 1, table.remove(workspaces, index))
            shownOrder = orderOf()
            persist()
        end }, {}),
        empty = ui.column({ grow = 1, justify = "center" }, { ui.emptyState({ text = text("code-editor.view.empty"), icon = "edit" }) }),
        saveAll = ui.button({ text = text("code-editor.actions.save-all"), onClick = view.saveAll }),
    }

    render()

    return ui.column({}, {
        ui.pageHeader({ title = text("code-editor.plugin.title") }, {
            ui.button({ text = text("code-editor.actions.open-folder"), icon = "folder", variant = "primary", onClick = chooseFolder }),
            controls.saveAll,
        }),
        controls.tabs,
        controls.empty,
    })
end

return view
