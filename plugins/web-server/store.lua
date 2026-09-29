-- Keeps the configured servers in the table of the plugin, and trusts a stored row only when it follows the rules the form enforces.
local address = include("address")

local store = {}

-- The migrations of the tables, which the manager applies before the plugin starts.
store.migrations = {
    {
        "CREATE TABLE web_server__configurations(id TEXT PRIMARY KEY, name TEXT NOT NULL, root TEXT NOT NULL, bind_host TEXT NOT NULL, port INTEGER NOT NULL CHECK(port BETWEEN 1 AND 65535), terminal_id TEXT) STRICT",
        "CREATE UNIQUE INDEX web_server__configurations_terminal_index ON web_server__configurations(terminal_id) WHERE terminal_id IS NOT NULL",
    },
}

local function invalid(detail)
    workpane.database.invalid(detail)
end

-- Every row is checked before any of them is used, so one broken configuration keeps the plugin from starting instead of misbehaving later.
function store.load()
    local rows = workpane.await(workpane.database.query("SELECT id, name, root, bind_host, port, terminal_id FROM web_server__configurations ORDER BY name, id", {}))
    local configurations = {}
    local identities = {}
    local terminals = {}

    for _, row in ipairs(rows) do
        local trimmed = type(row.name) == "string" and row.name:match("^%s*(.-)%s*$") or ""

        if type(row.id) ~= "string" or row.id == "" or identities[row.id] then
            invalid(tostring(row.id))
        end

        if trimmed == "" or trimmed ~= row.name or not workpane.files.absolute(row.root) or not address.numeric(row.bind_host) then
            invalid(row.id)
        end

        if math.type(row.port) ~= "integer" or row.port < 1 or row.port > 65535 then
            invalid(row.id)
        end

        if row.terminal_id ~= nil and terminals[row.terminal_id] then
            invalid(row.id)
        end

        identities[row.id] = true

        if row.terminal_id ~= nil then
            terminals[row.terminal_id] = true
        end

        configurations[#configurations + 1] = { id = row.id, name = row.name, root = row.root, host = row.bind_host, port = row.port, terminalId = row.terminal_id }
    end

    return configurations
end

function store.save(configuration)
    local terminal = configuration.terminalId ~= nil and configuration.terminalId ~= "" and configuration.terminalId or workpane.database.null
    return workpane.database.run("INSERT INTO web_server__configurations(id, name, root, bind_host, port, terminal_id) VALUES(?, ?, ?, ?, ?, ?) ON CONFLICT(id) DO UPDATE SET name = excluded.name, root = excluded.root, bind_host = excluded.bind_host, port = excluded.port, terminal_id = excluded.terminal_id", { configuration.id, configuration.name, configuration.root, configuration.host, configuration.port, terminal })
end

function store.remove(id)
    return workpane.database.run("DELETE FROM web_server__configurations WHERE id = ?", { id })
end

return store
