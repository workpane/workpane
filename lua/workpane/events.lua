-- The in-process bus between plugins: events broadcast to every other plugin, and capabilities answered by exactly one provider.
local bridge = require("workpane.bridge")
local lifecycle = require("workpane.lifecycle")
local plain = require("workpane.plain")
local task = require("workpane.task")

local events = {}

local subscriptions = {}
local providers = {}
local watchers = {}

-- A topic and a capability share the grammar of a translation key, three lowercase components of letters, numbers and hyphens.
local function validName(name)
    return type(name) == "string" and name:match("^[a-z0-9][a-z0-9%-]*%.[a-z0-9][a-z0-9%-]*%.[a-z0-9][a-z0-9%-]*$") ~= nil
end

-- A value crosses between plugins as a copy of its plain data, so no plugin changes what another one holds or reaches a function of another one.
local function copied(value, code, path, seen)
    local kind = type(value)

    if kind == "nil" or kind == "boolean" or kind == "number" or kind == "string" then
        return value
    end

    if not plain.table(value) then
        bridge.raise(code, "A value that crosses between plugins holds only text, numbers, booleans and tables of them", path)
    end

    if seen[value] then
        bridge.raise(code, "A value that crosses between plugins holds no cycle", path)
    end

    seen[value] = true
    local copy = {}

    for key, item in pairs(value) do
        if type(key) ~= "string" and type(key) ~= "number" then
            bridge.raise(code, "A value that crosses between plugins is keyed by text or numbers", path)
        end

        copy[key] = copied(item, code, path .. "." .. tostring(key), seen)
    end

    seen[value] = nil

    return copy
end

-- Delivers a payload to every other plugin subscribed to the topic, each in its own task, and never back to the sender.
function events.publish(sender, topic, payload)
    lifecycle.check(sender)

    if not validName(topic) then
        bridge.raise("event_topic_invalid", "An event topic is three lowercase components", tostring(topic))
    end

    if topic:sub(1, #sender + 1) ~= sender .. "." then
        bridge.raise("event_topic_foreign", "A plugin publishes only topics under its own identifier", topic)
    end

    local published = copied(payload, "event_payload_invalid", "payload", {})
    local list = subscriptions[topic] or {}

    -- The subscribers are taken before any of them runs, so a subscription made while an event is delivered starts with the next one.
    for _, subscription in ipairs(table.move(list, 1, #list, 1, {})) do
        if subscription.owner ~= sender then
            task.run(subscription.owner, "events", subscription.handler, copied(published, "event_payload_invalid", "payload", {}), sender)
        end
    end
end

function events.subscribe(owner, topic, handler)
    lifecycle.check(owner)

    if not validName(topic) then
        bridge.raise("event_topic_invalid", "An event topic is three lowercase components", tostring(topic))
    end

    if type(handler) ~= "function" then
        bridge.raise("event_handler_invalid", "An event is handled by a function", topic)
    end

    local list = subscriptions[topic] or {}
    list[#list + 1] = { owner = owner, handler = handler }
    subscriptions[topic] = list
end

-- Tells every plugin watching a capability whether it has a provider now, each in its own task, from the watchers taken before any of them runs.
local function announce(name)
    local list = watchers[name] or {}

    for _, watcher in ipairs(table.move(list, 1, #list, 1, {})) do
        task.run(watcher.owner, "capabilities", watcher.handler, providers[name] ~= nil)
    end
end

-- A second provider of a capability is refused by name, so a request always has exactly one answer.
-- A capability declares its contract, a sentence of the catalog of its provider, the schema of its payload and whether an agent may ask for it, so a plugin or an agent learns what it answers before asking.
function events.provide(owner, name, handler, contract)
    lifecycle.check(owner)

    if not validName(name) then
        bridge.raise("capability_name_invalid", "A capability name is three lowercase components", tostring(name))
    end

    if type(handler) ~= "function" then
        bridge.raise("capability_handler_invalid", "A capability is answered by a function", name)
    end

    if type(contract) ~= "table" or not validName(contract.summaryKey) or contract.summaryKey:match("^[^.]+") ~= owner or type(contract.payload) ~= "table" or contract.payload.type ~= "object" or (contract.agents ~= nil and type(contract.agents) ~= "boolean") then
        bridge.raise("capability_contract_invalid", "A capability declares a summary key of its provider, an object schema of its payload and whether agents may ask for it as a boolean", name)
    end

    if providers[name] ~= nil then
        error(bridge.failure({ code = "capability_provided", message = "A capability already has a provider", detail = name }), 2)
    end

    providers[name] = { owner = owner, handler = handler, summaryKey = contract.summaryKey, payload = copied(contract.payload, "capability_contract_invalid", "payload", {}), agents = contract.agents == true }
    announce(name)
end

-- Runs a handler with whether a capability has a provider each time one appears or leaves, and answers the function that stops it.
function events.watch(owner, name, handler)
    lifecycle.check(owner)

    if not validName(name) then
        bridge.raise("capability_name_invalid", "A capability name is three lowercase components", tostring(name))
    end

    if type(handler) ~= "function" then
        bridge.raise("capability_handler_invalid", "A capability is watched by a function", name)
    end

    local watcher = { owner = owner, handler = handler }
    local list = watchers[name] or {}
    list[#list + 1] = watcher
    watchers[name] = list

    return function()
        for index, candidate in ipairs(watchers[name] or {}) do
            if candidate == watcher then
                table.remove(watchers[name], index)
                return
            end
        end
    end
end

-- A capability is available while the plugin providing it runs, which is how a plugin offers an action only when something answers it.
function events.available(name)
    return providers[name] ~= nil
end

-- Answers every capability a running plugin provides with its provider and its contract, ordered by name.
function events.catalog()
    local names = {}

    for name in pairs(providers) do
        names[#names + 1] = name
    end

    table.sort(names)

    local listed = {}

    for index, name in ipairs(names) do
        local provider = providers[name]
        listed[index] = { name = name, provider = provider.owner, summaryKey = provider.summaryKey, payload = copied(provider.payload, "capability_contract_invalid", "payload", {}), agents = provider.agents }
    end

    return listed
end

-- Asks for a capability by name and answers a promise, refusing at once a name nobody provides instead of waiting for it.
function events.request(caller, name, payload)
    lifecycle.check(caller)
    local provider = providers[name]
    local asked = copied(payload, "capability_value_invalid", "payload", {})

    return bridge.future(function()
        if provider == nil then
            error(bridge.failure({ code = "capability_unavailable", message = "No plugin provides the capability", detail = name }), 0)
        end

        return copied(provider.handler(asked, caller), "capability_value_invalid", "answer", {})
    end)
end

-- A plugin that stops takes its subscriptions, its watchers and its capabilities with it, and the watchers of those capabilities learn they are gone.
function events.forget(owner)
    for topic, list in pairs(subscriptions) do
        local kept = {}

        for _, subscription in ipairs(list) do
            if subscription.owner ~= owner then
                kept[#kept + 1] = subscription
            end
        end

        subscriptions[topic] = kept
    end

    for name, list in pairs(watchers) do
        local kept = {}

        for _, watcher in ipairs(list) do
            if watcher.owner ~= owner then
                kept[#kept + 1] = watcher
            end
        end

        watchers[name] = kept
    end

    for name, provider in pairs(providers) do
        if provider.owner == owner then
            providers[name] = nil
            announce(name)
        end
    end
end

return events
