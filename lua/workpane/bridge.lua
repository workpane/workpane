-- The native bridge of the SDK, which keeps the only reference to the host table so no plugin can call the host directly.
local async = require("async")

local bridge = {}

local host = rawget(_G, "host")
rawset(_G, "host", nil)

local pending = {}
local nextRequest = 0
local subscribers = {}

-- The types of the SDK lock their metatables, keep their methods where no object reaches them and are recognized through private sets, so no plugin changes how another one reads a failure or awaits a future.
local Failure = { __metatable = "workpane.failure" }

local failures = setmetatable({}, { __mode = "k" })
local futures = setmetatable({}, { __mode = "k" })

-- A structured error prints as its message followed by its code and its detail in quotes, which is what a log line or a failed assertion shows.
Failure.__tostring = function(failure)
    return failure.message .. " (code \"" .. failure.code .. "\"" .. (failure.detail ~= "" and (", detail \"" .. failure.detail .. "\"") or "") .. ")"
end

-- Builds the structured error a host refusal carries, or wraps a Lua error so every failure has the same shape.
function bridge.failure(value)
    if failures[value] then
        return value
    end

    local failure

    if type(value) == "table" and type(value.code) == "string" then
        failure = setmetatable({ code = value.code, message = tostring(value.message or ""), detail = tostring(value.detail or "") }, Failure)
    else
        failure = setmetatable({ code = "lua_error", message = tostring(value), detail = "" }, Failure)
    end

    failures[failure] = true

    return failure
end

function bridge.isFailure(value)
    return failures[value] == true
end

-- Raises a structured error at the code that called the function refusing its arguments, which is where a plugin author looks.
function bridge.raise(code, message, detail)
    error(bridge.failure({ code = code, message = message, detail = detail or "" }), 3)
end

-- Calls a host function and answers its value, raising the structured error it refused with.
function bridge.call(name, argument)
    if host == nil then
        error(bridge.failure({ code = "bridge_closed", message = "The product closed the bridge to the host", detail = tostring(name) }), 2)
    end

    local reply = host[name](argument or {})

    if type(reply) ~= "table" then
        bridge.raise("bridge_reply_invalid", "The host answered a value the runtime could not read", name)
    end

    if not reply.ok then
        error(bridge.failure(reply.error), 2)
    end

    return reply.value
end

-- A future settles once with a reply, which the Varn deferred cannot carry because its resolver takes no value, so the reply is kept here.
local Future = {}
local futureType = { __metatable = "workpane.future", __index = Future }

-- Waits for the reply and answers its value, or nil and the structured error it failed with.
function Future:await()
    local state = futures[self]

    if state == nil then
        bridge.raise("bridge_future_invalid", "A future was awaited through a value that is not a future", "")
    end

    state.settled:await()
    local reply = state.reply

    if reply == nil then
        return nil, bridge.failure({ code = "bridge_reply_lost", message = "A future settled without a reply", detail = "" })
    end

    if not reply.ok then
        return nil, bridge.failure(reply.error)
    end

    return reply.value, nil
end

local function future()
    local settled, resolve = async.deferred()
    local created = setmetatable({}, futureType)
    local state = { settled = settled }
    futures[created] = state

    local function settle(reply)
        state.reply = reply
        resolve()
    end

    return created, settle
end

-- Starts a protected function in its own coroutine, which the runtime resumes whenever what it awaits settles.
function bridge.spawn(work, ...)
    local arguments = table.pack(...)

    async.spawn(function()
        local succeeded, failure = pcall(work, table.unpack(arguments, 1, arguments.n))

        if not succeeded then
            bridge.report("workpane", "bridge", "A coroutine of the SDK failed", { error = tostring(failure) })
        end
    end)
end

-- Runs a function in its own coroutine and answers a future of its result, keeping a structured error intact where a Varn promise would flatten it.
function bridge.future(work, ...)
    local created, settle = future()

    bridge.spawn(function(...)
        local succeeded, value = pcall(work, ...)
        settle(succeeded and { ok = true, value = value } or { ok = false, error = bridge.failure(value) })
    end, ...)

    return created
end

-- Asks the host for asynchronous work and answers a future that settles with its value or with its structured error, carrying the number of its request.
function bridge.request(name, argument)
    nextRequest = nextRequest + 1
    local request = nextRequest
    local created, settle = future()
    created.request = request
    pending[request] = settle
    argument.request = request
    local accepted, refusal = pcall(bridge.call, name, argument)

    if not accepted then
        pending[request] = nil
        settle({ ok = false, error = bridge.failure(refusal) })
    end

    return created
end

-- Every subscriber of a native event runs in its own protected call, so one failing handler never silences the others.
function bridge.on(name, handler)
    local list = subscribers[name]

    if list == nil then
        if host == nil then
            bridge.raise("bridge_closed", "The product closed the bridge to the host", name)
        end

        list = {}
        subscribers[name] = list
        host.on(name, function(payload)
            if payload == nil then
                bridge.report("workpane", "bridge", "A native event arrived with a value the runtime could not read", { event = name })
                return
            end

            for _, subscriber in ipairs(list) do
                local succeeded, failure = pcall(subscriber, payload)

                if not succeeded then
                    bridge.report("workpane", "bridge", "A native event handler failed", { event = name, error = tostring(failure) })
                end
            end
        end)
    end

    list[#list + 1] = handler
end

-- Writes a failure of the SDK itself to the centralized log under the owner it happened for.
function bridge.report(owner, category, message, details)
    if host == nil then
        return
    end

    pcall(host.workpane_log, { plugin = owner, level = "error", category = category, message = message, details = details or {} })
end

bridge.on("workpane.reply", function(reply)
    local settle = pending[reply.request]

    if settle ~= nil then
        pending[reply.request] = nil
        settle(reply)
    end
end)

-- The product releases its host functions before the state of Lua closes, so the bridge lets go of them first and a finalizer that runs while the state closes meets a structured error.
bridge.on("workpane.close", function()
    host = nil
end)

-- A failure no caller receives, such as a promise that rejected while nothing awaited it, reaches the log instead of stopping the loop every plugin runs on.
async.onFailure(function(failure, traceback, _, kind)
    local message = kind == "rejection" and "A promise failed and nothing awaited it" or "A coroutine failed and nothing received its failure"
    bridge.report("workpane", "runtime", message, { error = tostring(failure), traceback = traceback })
end)

return bridge
