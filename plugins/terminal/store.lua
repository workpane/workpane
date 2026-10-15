-- Keeps the terminal workspace as one JSON document in the table of the plugin.
local database = workpane.database

local store = {}

store.migrations = {
    { "CREATE TABLE terminal__state(scope_id TEXT PRIMARY KEY CHECK(scope_id = 'workspace'), data_json TEXT NOT NULL) STRICT" },
}

-- Answers the stored document as text, or nil when nothing was stored yet.
function store.read()
    local rows = workpane.await(database.query("SELECT data_json FROM terminal__state WHERE scope_id = 'workspace'", {}))
    return rows[1] ~= nil and rows[1].data_json or nil
end

function store.write(text)
    return database.run("INSERT INTO terminal__state(scope_id, data_json) VALUES('workspace', ?) ON CONFLICT(scope_id) DO UPDATE SET data_json = excluded.data_json", { text })
end

return store
