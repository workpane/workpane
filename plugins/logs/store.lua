-- Keeps the newest fifty thousand entries of the centralized log in a table of its own, writing the entries that arrive together in one transaction and reading them newest first.
local async = require("async")
local json = require("json")

local database = workpane.database
local translate = workpane.i18n.translate

local store = {}

local flushDelayMilliseconds = 100
local keptEntries = 50000
local largestPage = 100
local levels = { debug = true, info = true, warning = true, error = true }

-- The migrations of the tables, which the manager applies before the plugin starts.
store.migrations = {
    {
        "CREATE TABLE logs__entries(sequence INTEGER PRIMARY KEY AUTOINCREMENT, timestamp_utc TEXT NOT NULL, source TEXT NOT NULL, level TEXT NOT NULL CHECK(level IN ('debug', 'info', 'warning', 'error')), category TEXT NOT NULL, message TEXT NOT NULL, details_json TEXT NOT NULL) STRICT",
        "CREATE INDEX logs__entries_source_index ON logs__entries(source, sequence DESC)",
    },
    {
        "DROP INDEX logs__entries_source_index",
    },
}

local queue = {}
local flushing = false
local migrated = false
local failureShown = false
local listeners = {}

-- A listener learns whether entries were added or the log was emptied, so it reads only what is new after an addition.
local function changed(kind)
    for _, listener in ipairs(listeners) do
        workpane.task(listener, kind)
    end
end

-- A write that fails is told to the reader once and never written to the log, since that entry would fail the same way again.
local function reportWriteFailure()
    if failureShown then
        return
    end

    failureShown = true
    workpane.notify.error(translate("logs.plugin.title"), translate("logs.error.write-message"))
end

-- Only a write that stored entries is announced, so a failing disk never makes the viewer read the log again.
local function flush(delay)
    local written = false
    flushing = true

    if delay > 0 then
        async.sleep(delay):await()
    end

    while #queue > 0 do
        local statements = {}

        for index, entry in ipairs(queue) do
            statements[index] = { sql = "INSERT INTO logs__entries(timestamp_utc, source, level, category, message, details_json) VALUES(?, ?, ?, ?, ?, ?)", bindings = { entry.timestampUtc, entry.source, entry.level, entry.category, entry.message, json.encode(entry.details) } }
        end

        -- The log keeps its newest entries only, so a program that writes all day never fills the disk.
        statements[#statements + 1] = { sql = "DELETE FROM logs__entries WHERE sequence <= (SELECT MAX(sequence) FROM logs__entries) - ?", bindings = { keptEntries } }
        queue = {}
        local _, failure = database.transaction(statements):await()

        if failure ~= nil then
            reportWriteFailure()
        else
            failureShown = false
            written = true
        end
    end

    flushing = false

    if written then
        changed("added")
    end
end

local function valid(entry)
    return type(entry.timestampUtc) == "string" and type(entry.source) == "string" and levels[entry.level] ~= nil and type(entry.category) == "string" and entry.category ~= "" and type(entry.message) == "string" and entry.message ~= "" and type(entry.details) == "table"
end

-- A listener runs after every change of the stored entries, whatever made it.
function store.listen(listener)
    listeners[#listeners + 1] = listener
end

-- Entries that arrive together are written after a short delay in one transaction, and what arrives before the table exists waits for it.
function store.append(entry)
    if not valid(entry) then
        return
    end

    queue[#queue + 1] = entry

    if migrated and not flushing then
        flush(flushDelayMilliseconds)
    end
end

-- The tables are ready once the plugin starts, and the entries that arrived before are written then.
function store.open()
    migrated = true

    if #queue > 0 and not flushing then
        flush(0)
    end
end

-- What arrived in the last moment before the product closes is written before the database closes with it.
function store.drain()
    while flushing do
        async.sleep(10):await()
    end

    if migrated and #queue > 0 then
        flush(0)
    end
end

-- A future of at most the limit of entries, newest first, before a sequence or from the newest entry when the sequence is zero.
function store.page(beforeSequence, limit)
    local sql = "SELECT sequence, timestamp_utc, source, level, category, message, details_json FROM logs__entries" .. (beforeSequence == 0 and "" or " WHERE sequence < ?") .. " ORDER BY sequence DESC LIMIT ?"
    return database.query(sql, beforeSequence == 0 and { limit } or { beforeSequence, limit })
end

-- A future of at most the limit of entries written after a sequence, newest first.
function store.since(afterSequence, limit)
    return database.query("SELECT sequence, timestamp_utc, source, level, category, message, details_json FROM logs__entries WHERE sequence > ? ORDER BY sequence DESC LIMIT ?", { afterSequence, limit })
end

function store.clear()
    local _, failure = database.run("DELETE FROM logs__entries"):await()

    if failure == nil then
        changed("emptied")
    end

    return failure
end

-- Another plugin reads the log a page at a time, newest first, from before a sequence or from the newest entry when the sequence is zero.
function store.pageRequested(payload)
    local keys = 0

    for _ in pairs(type(payload) == "table" and payload or {}) do
        keys = keys + 1
    end

    if type(payload) ~= "table" or keys ~= 2 or math.type(payload.beforeSequence) ~= "integer" or payload.beforeSequence < 0 or math.type(payload.limit) ~= "integer" or payload.limit < 1 or payload.limit > largestPage then
        error({ code = "logs_page_invalid", message = "A page of the log names the sequence it starts before and at most a hundred entries", detail = "" }, 0)
    end

    local rows = workpane.await(store.page(payload.beforeSequence, payload.limit))
    local entries = {}

    for index, stored in ipairs(rows) do
        entries[index] = { sequence = stored.sequence, timestampUtc = stored.timestamp_utc, source = stored.source, level = stored.level, category = stored.category, message = stored.message, details = json.decode(stored.details_json) }
    end

    return { entries = entries }
end

-- Another plugin empties the log on purpose, so it is not asked the confirmation the reader sees.
function store.clearRequested(payload)
    if type(payload) ~= "table" or next(payload) ~= nil then
        error({ code = "logs_clear_invalid", message = "Clearing the log takes no values", detail = "" }, 0)
    end

    local failure = store.clear()

    if failure ~= nil then
        error(failure, 0)
    end

    return {}
end

return store
