-- The choices of the reader that every plugin may follow, the language and the theme, told after each change.
local bridge = require("workpane.bridge")
local lifecycle = require("workpane.lifecycle")
local task = require("workpane.task")

local reader = {}

local watchers = {}

local function current()
    local info = bridge.call("workpane_app_info", {})
    return { language = info.language, theme = info.theme }
end

-- The watchers are taken before any of them runs, so one that stops itself never costs another its turn.
local function announce()
    local choices = current()

    for _, watcher in ipairs(table.move(watchers, 1, #watchers, 1, {})) do
        task.run(watcher.owner, "reader", watcher.handler, { language = choices.language, theme = choices.theme })
    end
end

bridge.on("workpane.language.changed", announce)
bridge.on("workpane.theme.changed", announce)

-- Runs a handler with the language and the theme after each change the reader makes, and answers the function that stops it.
function reader.watch(owner, handler)
    lifecycle.check(owner)

    if type(handler) ~= "function" then
        bridge.raise("reader_handler_invalid", "The choices of the reader are followed by a function", owner)
    end

    local watcher = { owner = owner, handler = handler }
    watchers[#watchers + 1] = watcher

    return function()
        for index, candidate in ipairs(watchers) do
            if candidate == watcher then
                table.remove(watchers, index)
                return
            end
        end
    end
end

function reader.forget(owner)
    local kept = {}

    for _, watcher in ipairs(watchers) do
        if watcher.owner ~= owner then
            kept[#kept + 1] = watcher
        end
    end

    watchers = kept
end

return reader
