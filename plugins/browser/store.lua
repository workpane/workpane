-- Keeps the open tabs and the bookmarks of the browser in the tables of the plugin, each list written whole in one transaction.
local database = workpane.database

local store = {}

store.migrations = {
    {
        "CREATE TABLE browser__tabs(id TEXT PRIMARY KEY, position INTEGER NOT NULL UNIQUE CHECK(position >= 0), title TEXT NOT NULL, url TEXT NOT NULL, created_at_utc TEXT NOT NULL, updated_at_utc TEXT NOT NULL, active INTEGER NOT NULL CHECK(active IN (0, 1))) STRICT",
        "CREATE UNIQUE INDEX browser__tabs_active_index ON browser__tabs(active) WHERE active = 1",
        "CREATE TABLE browser__bookmark_groups(id TEXT PRIMARY KEY, position INTEGER NOT NULL UNIQUE CHECK(position >= 0), name TEXT NOT NULL, created_at_utc TEXT NOT NULL, updated_at_utc TEXT NOT NULL) STRICT",
        "CREATE TABLE browser__bookmarks(id TEXT PRIMARY KEY, group_id TEXT REFERENCES browser__bookmark_groups(id) ON DELETE RESTRICT, position INTEGER NOT NULL CHECK(position >= 0), name TEXT NOT NULL, url TEXT NOT NULL, created_at_utc TEXT NOT NULL, updated_at_utc TEXT NOT NULL) STRICT",
        "CREATE UNIQUE INDEX browser__bookmarks_group_position_index ON browser__bookmarks(COALESCE(group_id, ''), position)",
    },
}

function store.tabs()
    return workpane.await(database.query("SELECT id, position, title, url, created_at_utc, updated_at_utc, active FROM browser__tabs ORDER BY position", {}))
end

-- Answers a future of the write that replaces every stored tab with the given ones in their order.
function store.saveTabs(tabs)
    local statements = { { sql = "DELETE FROM browser__tabs", bindings = {} } }

    for position, tab in ipairs(tabs) do
        statements[#statements + 1] = { sql = "INSERT INTO browser__tabs(id, position, title, url, created_at_utc, updated_at_utc, active) VALUES(?, ?, ?, ?, ?, ?, ?)", bindings = { tab.id, position - 1, tab.title, tab.url, tab.createdAt, tab.updatedAt, tab.active and 1 or 0 } }
    end

    return database.transaction(statements)
end

function store.groups()
    return workpane.await(database.query("SELECT id, position, name, created_at_utc, updated_at_utc FROM browser__bookmark_groups ORDER BY position", {}))
end

-- The ungrouped bookmarks come first, then the bookmarks of each group, each collection in its order.
function store.bookmarks()
    return workpane.await(database.query("SELECT id, group_id, position, name, url, created_at_utc, updated_at_utc FROM browser__bookmarks ORDER BY CASE WHEN group_id IS NULL THEN 0 ELSE 1 END, group_id, position", {}))
end

-- Answers a future of the write that replaces every group and bookmark, numbering the bookmarks of each collection from zero so none has a gap.
function store.saveBookmarks(groups, entries)
    local statements = { { sql = "DELETE FROM browser__bookmarks", bindings = {} }, { sql = "DELETE FROM browser__bookmark_groups", bindings = {} } }
    local positions = {}

    for position, group in ipairs(groups) do
        statements[#statements + 1] = { sql = "INSERT INTO browser__bookmark_groups(id, position, name, created_at_utc, updated_at_utc) VALUES(?, ?, ?, ?, ?)", bindings = { group.id, position - 1, group.name, group.createdAt, group.updatedAt } }
    end

    for _, entry in ipairs(entries) do
        local container = entry.groupId or ""
        local position = positions[container] or 0
        positions[container] = position + 1
        statements[#statements + 1] = { sql = "INSERT INTO browser__bookmarks(id, group_id, position, name, url, created_at_utc, updated_at_utc) VALUES(?, ?, ?, ?, ?, ?, ?)", bindings = { entry.id, entry.groupId or database.null, position, entry.name, entry.url, entry.createdAt, entry.updatedAt } }
    end

    return database.transaction(statements)
end

return store
