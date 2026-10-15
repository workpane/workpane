-- The plugins the product discovered, offered to every plugin through the application without reaching the manager that starts them.
local bridge = require("workpane.bridge")
local lifecycle = require("workpane.lifecycle")
local task = require("workpane.task")

local directory = {}
local watchers = {}

local lister = function()
    return {}
end

function directory.install(list)
    lister = list
end

-- Answers each discovered plugin with its identifier, its catalog keys, whether it is bundled and its state, as copies of plain data.
function directory.list()
    local listed = {}

    for index, plugin in ipairs(lister()) do
        listed[index] = { id = plugin.id, titleKey = plugin.titleKey, descriptionKey = plugin.descriptionKey, bundled = plugin.bundled, state = plugin.state }
    end

    return listed
end

function directory.watch(owner, handler)
    lifecycle.check(owner)

    if type(handler) ~= "function" then
        bridge.raise("plugin_watcher_invalid", "A plugin directory watcher is a function", "")
    end

    local watcher = { owner = owner, handler = handler }
    watchers[watcher] = true

    return function()
        watchers[watcher] = nil
    end
end

function directory.announce()
    for watcher in pairs(watchers) do
        task.run(watcher.owner, "plugins", watcher.handler, directory.list())
    end
end

function directory.forget(owner)
    for watcher in pairs(watchers) do
        if watcher.owner == owner then
            watchers[watcher] = nil
        end
    end
end

return directory
