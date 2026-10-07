-- Hands every entry of the centralized log to the plugins that subscribe, keeping what arrives before anyone listens.
local bridge = require("workpane.bridge")
local lifecycle = require("workpane.lifecycle")
local rules = require("workpane.rules")

local logs = {}

local backlogLimit = 1000
local backlog = {}
local subscribers = {}
local delivering = setmetatable({}, { __mode = "k" })

local function remove(gone)
    local kept = {}

    for _, subscriber in ipairs(subscribers) do
        if subscriber ~= gone then
            kept[#kept + 1] = subscriber
        end
    end

    subscribers = kept
end

-- Each subscriber receives its own copy of the entry, a subscriber that fails no longer receives entries and its failure reaches the others once, so a broken subscriber never feeds itself.
local function deliver(subscriber, entry)
    bridge.spawn(function()
        local current = coroutine.running()
        delivering[current] = true
        local succeeded, failure = pcall(subscriber.handler, rules.copy(entry))
        delivering[current] = nil

        if succeeded then
            return
        end

        remove(subscriber)
        bridge.report(subscriber.owner, "logs", "A log subscriber failed and no longer receives entries", { error = tostring(failure) })
    end)
end

-- Entries written while the product starts arrive before any plugin runs, so a bounded backlog waits for the first subscriber.
bridge.on("workpane.log.entries", function(entries)
    for _, entry in ipairs(entries) do
        if #subscribers == 0 then
            if #backlog == backlogLimit then
                table.remove(backlog, 1)
            end

            backlog[#backlog + 1] = entry
        end

        for _, subscriber in ipairs({ table.unpack(subscribers) }) do
            deliver(subscriber, entry)
        end
    end
end)

-- The first subscriber also receives, oldest first, what was written while nobody listened.
function logs.subscribe(owner, handler)
    lifecycle.check(owner)

    if type(handler) ~= "function" then
        bridge.raise("log_handler_invalid", "The log is read by a function", owner)
    end

    local subscriber = { owner = owner, handler = handler }
    subscribers[#subscribers + 1] = subscriber
    local waiting = backlog
    backlog = {}

    for _, entry in ipairs(waiting) do
        deliver(subscriber, entry)
    end
end

-- Answers whether the running code is a subscriber reading an entry, which may not write to the log it reads.
function logs.delivering()
    return delivering[coroutine.running()] == true
end

-- Answers whether a plugin reads the log, which the product stops after every other plugin so it still receives what their stops write.
function logs.reads(owner)
    for _, subscriber in ipairs(subscribers) do
        if subscriber.owner == owner then
            return true
        end
    end

    return false
end

function logs.forget(owner)
    local kept = {}

    for _, subscriber in ipairs(subscribers) do
        if subscriber.owner ~= owner then
            kept[#kept + 1] = subscriber
        end
    end

    subscribers = kept
end

return logs
