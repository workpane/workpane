-- Runs asynchronous work in protected coroutines, because an error escaping a Varn coroutine would end the whole runtime.
local bridge = require("workpane.bridge")

local task = {}

-- Starts a function in its own coroutine where it may await promises, and writes any failure to the log of its owner.
function task.run(owner, category, work, ...)
    bridge.spawn(function(...)
        local succeeded, failure = pcall(work, ...)

        if not succeeded then
            bridge.report(owner, category, "An asynchronous task of the plugin failed", { error = tostring(failure) })
        end
    end, ...)
end

-- Awaits a promise and raises its failure, so sequential code reads like the synchronous code it replaces.
function task.await(promise)
    local value, failure = promise:await()

    if failure ~= nil then
        error(bridge.failure(failure), 2)
    end

    return value
end

return task
