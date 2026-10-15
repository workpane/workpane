local async = require("async")
local crypto = require("crypto")
local json = require("json")
local diagnostics = include("diagnostics")
local preferences = include("preferences")
local protocol = include("protocol")
local session = include("session")
local credentials = include("credentials")

local connections = {}

local sessions = {}
local previews = {}
local watchers = {}
local stopping = false
local published
local unwatchProfiles

function connections.list(payload)
    if type(payload) ~= "table" or next(payload) ~= nil then
        error(protocol.failure("ssh_connection_request_invalid"), 0)
    end

    local profiles = {}

    for index, profile in ipairs(preferences.list()) do
        local current = sessions[profile.id]
        local connected = current ~= nil and not current.closed and current.state == "connected"
        profiles[index] = { profile = profile.id, name = profile.name, root = connected and current.identity.root or profile.root, platform = current ~= nil and current.identity ~= nil and current.identity.platform or nil, state = current ~= nil and current.state or "disconnected", session = connected and current.id or nil }
    end

    return { profiles = profiles }
end

local function changed()
    if stopping then
        return
    end

    for handler in pairs(watchers) do
        workpane.task(handler)
    end

    local value = connections.list({})
    local encoded = json.encode(value)

    if encoded ~= published then
        published = encoded
        workpane.events.publish("remote-ssh.connections.changed", value)
    end
end

function connections.start()
    stopping = false
    published = nil
    unwatchProfiles = preferences.watch(changed)
end

function connections.status(id)
    local current = sessions[id]

    if current == nil then
        return "disconnected"
    end

    return current.state, current.problem
end

function connections.active(id)
    local state = connections.status(id)

    return state ~= "disconnected" and state ~= "failed"
end

function connections.current(id)
    local current = sessions[id] or previews[id]

    return current ~= nil and not current.closed and current.state == "connected" and current or nil
end

function connections.closePreview(id)
    return workpane.future(function()
        local current = previews[id]

        if current ~= nil then
            local outcome = workpane.await(current:close())
            previews[id] = nil

            if outcome.failure ~= nil then
                error(outcome.failure, 0)
            end
        end
    end)
end

function connections.createPreview(profile)
    local count = 0

    for _ in pairs(previews) do
        count = count + 1
    end

    if stopping or count >= 8 then
        error(protocol.failure("ssh_connection_busy"), 0)
    end

    local copied = {}

    for key, value in pairs(profile) do
        copied[key] = value
    end

    copied.id = crypto.uuidV4()
    local current, failure = session.new(copied, changed)

    if current == nil then
        error(failure, 0)
    end

    current.browsing = true
    current.credentialName = credentials.name(profile)
    previews[current.profile.id] = current

    return current
end

function connections.connect(id)
    if stopping or connections.active(id) then
        return nil, protocol.failure("ssh_connection_busy")
    end

    local profile = preferences.find(id)

    if profile == nil then
        return nil, protocol.failure("ssh_profile_invalid")
    end

    local created
    local problem
    created, problem = session.new(profile, function()
        if not stopping and created.state == "failed" and created.identity ~= nil and not created.reported then
            created.reported = true
            diagnostics.notify(created.problem)
        end

        changed()
    end)

    if created == nil then
        return nil, problem
    end

    sessions[id] = created
    changed()

    return created:start()
end

function connections.disconnect(id)
    local current = sessions[id]

    if current == nil then
        return async.promise(function()
            return {}
        end)
    end

    return current:close()
end

function connections.watch(handler)
    watchers[handler] = true

    return function()
        watchers[handler] = nil
    end
end

function connections.stop()
    stopping = true
    unwatchProfiles()
    watchers = {}
    local closing = {}

    for _, current in pairs(sessions) do
        closing[#closing + 1] = current:close()
    end

    for _, current in pairs(previews) do
        closing[#closing + 1] = current:close()
    end

    async.allSettled(closing):await()
    sessions = {}
    previews = {}
end

return connections
