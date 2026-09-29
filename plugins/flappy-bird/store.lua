-- Keeps the ten best rounds with the moment each one was played.
local database = workpane.database

local store = {}

local kept = 10
local watchers = {}

local function changed()
    for _, handler in ipairs({ table.unpack(watchers) }) do
        handler()
    end
end

-- Runs the handler after every change of the best rounds, and answers the function that stops it.
function store.watch(handler)
    watchers[#watchers + 1] = handler

    return function()
        for index, existing in ipairs(watchers) do
            if existing == handler then
                table.remove(watchers, index)
                return
            end
        end
    end
end

-- The migrations of the tables, which the manager applies before the plugin starts.
store.migrations = {
    {
        "CREATE TABLE flappy_bird__rounds(id INTEGER PRIMARY KEY AUTOINCREMENT, score INTEGER NOT NULL CHECK(score >= 0), played_at TEXT NOT NULL) STRICT",
        "CREATE INDEX flappy_bird__rounds_best ON flappy_bird__rounds(score DESC, id)",
    },
}

-- A round joins the best ones and the rounds beyond the tenth leave in the same step.
function store.record(score)
    workpane.await(database.transaction({
        { sql = "INSERT INTO flappy_bird__rounds(score, played_at) VALUES(?, ?)", bindings = { score, workpane.time.now() } },
        { sql = "DELETE FROM flappy_bird__rounds WHERE id NOT IN (SELECT id FROM flappy_bird__rounds ORDER BY score DESC, id LIMIT ?)", bindings = { kept } },
    }))

    changed()
end

-- Answers the best rounds, the highest first and the earliest first among equal scores.
function store.best()
    local rows = workpane.await(database.query("SELECT score, played_at FROM flappy_bird__rounds ORDER BY score DESC, id LIMIT ?", { kept }))
    local rounds = {}

    for index, row in ipairs(rows) do
        if math.type(row.score) ~= "integer" or type(row.played_at) ~= "string" then
            database.invalid("flappy_bird__rounds")
        end

        rounds[index] = { score = row.score, playedAt = row.played_at }
    end

    return rounds
end

function store.clear()
    workpane.await(database.run("DELETE FROM flappy_bird__rounds"))
    changed()
end

return store
