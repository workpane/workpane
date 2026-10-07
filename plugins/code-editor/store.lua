-- Keeps the open folders and their documents, with their order, the active ones and the cursors, checked whole when read and rewritten a moment after each change.
local async = require("async")
local paths = include("paths")

local translate = workpane.i18n.translate
local database = workpane.database

local store = {}

store.migrations = {
    {
        "CREATE TABLE code_editor__workspaces(id TEXT PRIMARY KEY NOT NULL, root_path TEXT NOT NULL UNIQUE, position INTEGER NOT NULL UNIQUE CHECK(position >= 0), active INTEGER NOT NULL CHECK(active IN (0, 1)), created_at_utc TEXT NOT NULL, updated_at_utc TEXT NOT NULL) STRICT",
        "CREATE TABLE code_editor__documents(workspace_id TEXT NOT NULL REFERENCES code_editor__workspaces(id) ON DELETE CASCADE, path TEXT NOT NULL, position INTEGER NOT NULL CHECK(position >= 0), cursor_line INTEGER NOT NULL CHECK(cursor_line >= 1), cursor_column INTEGER NOT NULL CHECK(cursor_column >= 1), active INTEGER NOT NULL CHECK(active IN (0, 1)), PRIMARY KEY(workspace_id, path), UNIQUE(workspace_id, position)) STRICT",
        "CREATE UNIQUE INDEX code_editor__active_workspace ON code_editor__workspaces(active) WHERE active = 1",
        "CREATE UNIQUE INDEX code_editor__active_document ON code_editor__documents(workspace_id, active) WHERE active = 1",
    },
    {
        "ALTER TABLE code_editor__workspaces DROP COLUMN created_at_utc",
        "ALTER TABLE code_editor__workspaces DROP COLUMN updated_at_utc",
    },
}

local saveDelaySeconds = 0.25
local saving = false
local pending

local function invalid(detail)
    workpane.database.invalid(detail)
end

-- Every folder is absolute, in order and unique, one of them is active, and every document lies inside its folder with one active document per folder.
function store.load()
    local workspaces = {}
    local byId = {}
    local roots = {}
    local rows = workpane.await(database.query("SELECT id, root_path, position, active FROM code_editor__workspaces ORDER BY position", {}))
    local active = 0

    for index, row in ipairs(rows) do
        if row.id == "" or not workpane.files.absolute(row.root_path) or roots[row.root_path] or row.position ~= index - 1 then
            invalid(tostring(row.id))
        end

        roots[row.root_path] = true
        active = active + row.active
        workspaces[index] = { id = row.id, root = row.root_path, active = row.active == 1, documents = {} }
        byId[row.id] = workspaces[index]
    end

    if #workspaces > 0 and active ~= 1 then
        invalid("active")
    end

    local documents = workpane.await(database.query("SELECT workspace_id, path, position, cursor_line, cursor_column, active FROM code_editor__documents ORDER BY workspace_id, position", {}))

    for _, row in ipairs(documents) do
        local workspace = byId[row.workspace_id]

        if workspace == nil or not workpane.files.absolute(row.path) or row.path == workspace.root or not paths.inside(workspace.root, row.path) or row.position ~= #workspace.documents then
            invalid(tostring(row.path))
        end

        workspace.documents[#workspace.documents + 1] = { path = row.path, line = row.cursor_line, column = row.cursor_column, active = row.active == 1 }
    end

    for _, workspace in ipairs(workspaces) do
        local activeDocuments = 0

        for _, document in ipairs(workspace.documents) do
            activeDocuments = activeDocuments + (document.active and 1 or 0)
        end

        if #workspace.documents > 0 and activeDocuments ~= 1 then
            invalid(workspace.id)
        end
    end

    return workspaces
end

local function statements(workspaces)
    local list = { { sql = "DELETE FROM code_editor__documents", bindings = {} }, { sql = "DELETE FROM code_editor__workspaces", bindings = {} } }

    for position, workspace in ipairs(workspaces) do
        list[#list + 1] = { sql = "INSERT INTO code_editor__workspaces(id, root_path, position, active) VALUES(?, ?, ?, ?)", bindings = { workspace.id, workspace.root, position - 1, workspace.active and 1 or 0 } }

        for index, document in ipairs(workspace.documents) do
            list[#list + 1] = { sql = "INSERT INTO code_editor__documents(workspace_id, path, position, cursor_line, cursor_column, active) VALUES(?, ?, ?, ?, ?, ?)", bindings = { workspace.id, document.path, index - 1, document.line, document.column, document.active and 1 or 0 } }
        end
    end

    return list
end

-- A change waits a quarter of a second so a moving cursor writes once, and only one write runs at a time with the newest state winning.
-- A failed write is told only when no newer state is waiting, because that state replaces what could not be written.
function store.save(snapshot)
    pending = snapshot

    if saving then
        return
    end

    saving = true

    workpane.task(function()
        async.sleep(math.floor(saveDelaySeconds * 1000)):await()

        while pending ~= nil do
            local written = pending
            pending = nil
            local _, failure = database.transaction(statements(written)):await()

            if failure ~= nil and pending == nil then
                workpane.log.error("persistence", "The code editor state could not be saved", { code = failure.code, detail = failure.detail })
                workpane.notify.error(translate("code-editor.error.title"), translate("code-editor.error.operation"))
            end
        end

        saving = false
    end)
end

-- What changed in the last moment before the plugin stops is written before the stop ends.
function store.drain()
    while saving do
        async.sleep(10):await()
    end
end

return store
