-- The editor destination: one tab per open folder, the page header that opens folders and saves them, and the stored order of both.
local async = require("async")
local crypto = require("crypto")
local fs = require("fs")
local filesystem = include("filesystem")
local remoteFilesystem = include("remote-filesystem")
local store = include("store")
local finder = include("views/finder")
local paths = include("paths")
local workspace = include("views/workspace")

local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

local view = {}

local stopWaitMilliseconds = 2500
local stopStepMilliseconds = 20

local workspaces = {}
local closingFolders = {}
local openingRemote = {}
local active
local controls
local shownOrder
local stopping = false

local function acceptOpening()
    if stopping then
        error({ code = "code_editor_stopping", message = "The code editor is stopping", detail = "" }, 0)
    end
end

local function persist()
    local snapshot = {}

    local selected = false

    for _, opened in ipairs(workspaces) do
        if not opened.files.remote then
            local record = opened:snapshot()
            record.active = opened == active
            selected = selected or record.active
            snapshot[#snapshot + 1] = record
        end
    end

    if not selected and #snapshot > 0 then
        snapshot[1].active = true
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

    local opening = {}

    for _, pending in pairs(openingRemote) do
        opening[#opening + 1] = ui.row({ padding = 12, spacing = 8 }, {
            ui.busyIndicator({ running = true, size = 16 }),
            ui.label({ text = text(pending.cancelled and "code-editor.remote.cancelling" or "code-editor.remote.opening"), grow = 1 }),
            ui.button({ text = text("code-editor.actions.cancel"), enabled = not pending.cancelled, onClick = function()
                pending.cancelled = true

                if pending.backend ~= nil then
                    pending.backend.close()
                end

                render()
            end }),
        })
    end

    controls.opening:setChildren(opening, { visible = #opening > 0 })
    controls.empty:set({ visible = #items == 0 and #opening == 0 })
    controls.saveAll:set({ enabled = active ~= nil })
end

-- Only the folder in front follows its files, so the folders behind it cost nothing while they wait.
local function current(opened)
    return opened == active
end

local function create(record, stored, backend)
    acceptOpening()

    return workspace.new(record, { persist = persist, current = current }, stored, backend)
end

-- The folders stored by the previous run come back in their order and with the same one active, and each one opens its documents and starts its servers once the editor is first shown.
function view.restore(records)
    for _, record in ipairs(records) do
        local restored = create(record, record.documents, filesystem.native())
        workspaces[#workspaces + 1] = restored
        active = record.active and restored or active
    end
end

-- A folder already open comes to the front instead of opening twice, and a new one opens last and active.
local function openFolder(path)
    acceptOpening()
    local info = type(path) == "string" and path ~= "" and fs.stat(path):await() or nil
    local canonical = info ~= nil and info.isDir and workpane.await(workpane.files.canonical(path)) or nil
    acceptOpening()

    if canonical == nil then
        error({ code = "code_editor_workspace_invalid", message = "The code editor workspace is unavailable", detail = tostring(path) }, 0)
    end

    for _, opened in ipairs(workspaces) do
        if not opened.files.remote and opened.root == canonical then
            active = opened
            render()
            persist()
            return opened.id
        end
    end

    local created = create({ id = crypto.uuidV4(), root = canonical }, {}, filesystem.native())
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

function view.remoteRequested(payload)
    acceptOpening()

    if type(payload) ~= "table" or type(payload.profile) ~= "string" or type(payload.session) ~= "string" or #payload.profile == 0 or #payload.profile > 128 or #payload.session == 0 or #payload.session > 128 or payload.profile:find("%z") ~= nil or payload.session:find("%z") ~= nil or utf8.len(payload.profile) == nil or utf8.len(payload.session) == nil then
        error({ code = "code_editor_request_invalid", message = "A remote workspace requires its connection identity", detail = "" }, 0)
    end

    for key in pairs(payload) do
        if key ~= "profile" and key ~= "session" then
            error({ code = "code_editor_request_invalid", message = "A remote workspace carries an invalid field", detail = key }, 0)
        end
    end

    for _, opened in ipairs(workspaces) do
        if opened.files.remote and opened.files.available and opened.files.identity.profile == payload.profile and opened.files.identity.session == payload.session then
            active = opened
            render()
            workpane.shell.navigate("editor")
            return { workspaceId = opened.id }
        end
    end

    local key = payload.profile .. "\0" .. payload.session

    local pending = openingRemote[key]

    if pending == nil then
        pending = { cancelled = false }
        openingRemote[key] = pending
        render()
        pending.future = workpane.future(function()
            local backend
            local created, result = pcall(function()
                backend = remoteFilesystem.open(payload.profile, payload.session)
                pending.backend = backend
                acceptOpening()

                if pending.cancelled then
                    error({ code = "code_editor_open_cancelled", message = "The remote workspace opening was cancelled", detail = "" }, 0)
                end

                local opened = create({ id = crypto.uuidV4(), root = backend.identity.root }, {}, backend)
                workspaces[#workspaces + 1] = opened
                active = opened
                render()
                workpane.task(opened.start, opened)
                persist()

                return { workspaceId = opened.id }
            end)

            openingRemote[key] = nil
            render()

            if not created then
                if backend ~= nil then
                    backend.close():await()
                end

                error(result, 0)
            end

            return result
        end)
    end

    workpane.shell.navigate("editor")
    local opened = workpane.await(pending.future)

    return opened
end

function view.remotePathRequested(payload)
    if type(payload) ~= "table" or type(payload.path) ~= "string" or #payload.path == 0 or #payload.path > 4096 or payload.path:find("%z") ~= nil or utf8.len(payload.path) == nil then
        error({ code = "code_editor_request_invalid", message = "A remote path request requires valid bounded text", detail = "" }, 0)
    end

    for key, value in pairs(payload) do
        if (key == "line" or key == "column") and (math.type(value) ~= "integer" or value < 1 or value > 2147483647) or key ~= "profile" and key ~= "session" and key ~= "path" and key ~= "line" and key ~= "column" then
            error({ code = "code_editor_request_invalid", message = "A remote path request carries an invalid field", detail = key }, 0)
        end
    end

    local selected = view.remoteRequested({ profile = payload.profile, session = payload.session })
    local opened = find(selected.workspaceId)

    if opened == nil or not opened.files.available then
        error({ code = "code_editor_file_unavailable", message = "The remote workspace is unavailable", detail = "" }, 0)
    end

    local canonical = workpane.await(opened.files.canonical(payload.path))
    local info = workpane.await(opened.files.stat(canonical))

    if info == nil or not info.isFile and not info.isDir then
        error({ code = "code_editor_file_unavailable", message = "The requested remote path is unavailable", detail = "" }, 0)
    end

    if info.isFile then
        opened:open(canonical, { line = payload.line or 1, column = payload.column or 1 })

        if opened:find(canonical) == nil then
            error({ code = "code_editor_file_unavailable", message = "The requested remote file could not be opened", detail = "" }, 0)
        end
    else
        opened.tree:reveal(canonical)
    end

    return { workspaceId = opened.id, path = canonical }
end

function view.fileRequested(payload)
    if type(payload) ~= "table" or type(payload.path) ~= "string" then
        error({ code = "code_editor_request_invalid", message = "A file request requires its path", detail = "" }, 0)
    end

    for key, value in pairs(payload) do
        if (key == "line" or key == "column") and (math.type(value) ~= "integer" or value < 1) or key == "root" and type(value) ~= "string" or key ~= "path" and key ~= "root" and key ~= "line" and key ~= "column" then
            error({ code = "code_editor_request_invalid", message = "A file request carries an invalid field", detail = key }, 0)
        end
    end

    local canonical = workpane.await(workpane.files.canonical(payload.path))
    local info = fs.stat(canonical):await()

    if info == nil or not info.isFile then
        error({ code = "code_editor_file_unavailable", message = "The requested file is unavailable", detail = canonical }, 0)
    end

    local root = payload.root ~= nil and workpane.await(workpane.files.canonical(payload.root)) or nil

    if root ~= nil and not paths.inside(root, canonical) then
        error({ code = "code_editor_request_invalid", message = "The requested file is outside its folder", detail = canonical }, 0)
    end

    if root == nil then
        for _, opened in ipairs(workspaces) do
            if not opened.files.remote and paths.inside(opened.root, canonical) and (root == nil or #opened.root > #root) then
                root = opened.root
            end
        end
    end

    local id = openFolder(root or paths.parent(canonical))
    local opened = find(id)
    workpane.shell.navigate("editor")
    opened:start()
    opened:open(canonical, { line = payload.line or 1, column = payload.column or 1 })

    if opened:find(canonical) == nil then
        error({ code = "code_editor_file_unavailable", message = "The requested file could not be opened", detail = canonical }, 0)
    end

    return { workspaceId = id, path = canonical }
end

local function chooseFolder()
    local chosen = workpane.await(workpane.dialogs.selectFolder({ title = translate("code-editor.actions.open-folder"), initial = active ~= nil and not active.files.remote and active.root or workpane.system.home() }))

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
        end, active.files)
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
    stopping = true

    for _, pending in pairs(openingRemote) do
        pending.cancelled = true

        if pending.backend ~= nil then
            pending.backend.close()
        end
    end

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
        opening = ui.column({ visible = false }, {}),
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
        controls.opening,
        controls.tabs,
        controls.empty,
    })
end

return view
