-- Runs asynchronous work in protected coroutines, because an error escaping a Varn coroutine would end the whole runtime.
local bridge = require("workpane.bridge")

local task = {}
local pending = {}

local function tracked(owner)
    local owners = type(owner) == "table" and owner or { owner }
    local operation = {}

    for _, identity in ipairs(owners) do
        local group = pending[identity] or {}
        group[operation] = true
        pending[identity] = group
    end

    local function finished()
        for _, identity in ipairs(owners) do
            local group = pending[identity]

            if group ~= nil then
                group[operation] = nil

                if next(group) == nil then
                    pending[identity] = nil
                end
            end
        end
    end

    return operation, finished
end

-- Starts a function in its own coroutine where it may await promises, and writes any failure to the log of its owner.
function task.run(owner, category, work, ...)
    local operation, finished = tracked(owner)
    local running = bridge.spawn(function(...)
        local succeeded, failure = pcall(work, ...)
        finished()

        if not succeeded then
            bridge.report(owner, category, "An asynchronous task of the plugin failed", { error = tostring(failure) })
        end
    end, ...)
    operation.cancel = function()
        finished()
        running.cancel()
    end
end

function task.future(owner, work, ...)
    local operation, finished = tracked(owner)
    local future, cancel = bridge.future(function(...)
        local succeeded, value = pcall(work, ...)
        finished()

        if not succeeded then
            error(value, 0)
        end

        return value
    end, ...)
    operation.cancel = function()
        finished()
        cancel()
    end

    return future
end

function task.forget(owner)
    local group = pending[owner] or {}
    pending[owner] = nil
    bridge.forget(owner)

    for operation in pairs(group) do
        operation.cancel()
    end
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
