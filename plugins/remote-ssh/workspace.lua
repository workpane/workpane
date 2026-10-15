local async = require("async")
local connections = include("connections")
local protocol = include("protocol")

local workspace = {}

local scopes = {}
local stopping = false
local unwatch
local unwatchPlugins

local methods = {
    ["context.info"] = true,
    ["context.stat"] = true,
    ["context.canonical"] = true,
    ["context.list"] = true,
    ["context.read-prefix"] = true,
    ["files.stat"] = true,
    ["files.list"] = true,
    ["files.canonical"] = true,
    ["files.walk"] = true,
    ["files.search"] = true,
    ["files.read"] = true,
    ["files.read-prefix"] = true,
    ["files.write"] = true,
    ["files.mkdir"] = true,
    ["files.rename"] = true,
    ["files.remove"] = true,
    ["process.find"] = true,
    ["process.start"] = true,
    ["process.read"] = true,
    ["process.write"] = true,
    ["process.stop"] = true,
}

local function invalid()
    error(protocol.failure("ssh_workspace_request_invalid"), 0)
end

local function declaration(payload, fields)
    if type(payload) ~= "table" then
        invalid()
    end

    for key in pairs(payload) do
        if fields[key] == nil then
            invalid()
        end
    end

    for key, kind in pairs(fields) do
        local value = payload[key]

        if type(value) ~= kind or kind == "string" and (#value == 0 or #value > 128 or value:find("%z") ~= nil or utf8.len(value) == nil) then
            invalid()
        end
    end
end

local function changed(scope)
    workpane.events.publish("remote-ssh.workspace.changed", { workspace = scope.id, connected = not scope.closed })
end

local function close(scope)
    if scope.closed then
        return scope.finished
    end

    scope.closed = true
    changed(scope)
    workpane.task(function()
        local accepted = pcall(function()
            local transport = scope.session.requests

            if transport ~= nil and not transport.closed then
                local _, failure = transport:request("process.group.close", { group = scope.id }, 10000)

                if failure ~= nil and not scope.session.closed then
                    scope.session.problem = protocol.failure("ssh_cleanup_failed")
                    scope.session:close():await()
                    error(protocol.failure("ssh_cleanup_failed"), 0)
                end
            end

            scope.opening:await()
            local pending = {}

            for request in pairs(scope.pending) do
                pending[#pending + 1] = request.provider or request.future
            end

            async.allSettled(pending):await()
        end)

        if not accepted and not scope.session.closed then
            scope.session.problem = protocol.failure("ssh_cleanup_failed")
            scope.session:close():await()
        end

        scopes[scope.id] = nil
        scope.result = not accepted and protocol.failure("ssh_cleanup_failed") or nil
        scope.finish()
    end)

    return scope.finished
end

function workspace.open(payload, caller)
    declaration(payload, { profile = "string", session = "string" })
    local session = connections.current(payload.profile)

    if stopping or session == nil or session.id ~= payload.session then
        error(protocol.failure("ssh_connection_closed"), 0)
    end

    local count = 0

    for _ in pairs(scopes) do
        count = count + 1
    end

    if count >= 32 then
        error(protocol.failure("ssh_workspace_limit"), 0)
    end

    local finished, finish = async.deferred()
    local scope = { id = session:reserveGroup(), session = session, caller = caller, closed = false, pending = {}, finish = finish }
    scopes[scope.id] = scope
    scope.finished = workpane.future(function()
        finished:await()

        if scope.result ~= nil then
            error(scope.result, 0)
        end

        return {}
    end)

    local opening, opened = async.deferred()
    scope.opening = workpane.future(function()
        opening:await()

        if scope.openResult.failure ~= nil then
            error(scope.openResult.failure, 0)
        end

        return scope.openResult.value
    end)

    workpane.task(function()
        local accepted, value, problem = pcall(session.request, session, "process.group.open", { group = scope.id }, 10000)
        scope.openResult = accepted and { value = value, failure = problem } or { failure = protocol.failure("ssh_transport_failed") }
        opened()
    end)

    local _, problem = scope.opening:await()

    if problem ~= nil or scope.closed or session.closed then
        close(scope):await()
        error(problem or protocol.failure("ssh_connection_closed"), 0)
    end

    changed(scope)

    return { workspace = scope.id, profile = session.profile.id, session = session.id, root = session.identity.root, platform = session.identity.platform, name = session.profile.name }
end

local function owned(payload, caller)
    local scope = scopes[payload.workspace]

    if scope == nil or scope.caller ~= caller then
        error(protocol.failure("ssh_workspace_unavailable"), 0)
    end

    return scope
end

function workspace.request(payload, caller)
    declaration(payload, { workspace = "string", method = "string", parameters = "table" })
    local scope = owned(payload, caller)

    if scope.closed or scope.session.closed or stopping then
        error(protocol.failure("ssh_connection_closed"), 0)
    end

    if methods[payload.method] ~= true or payload.parameters.group ~= nil then
        invalid()
    end

    if payload.method:sub(1, #"process.") == "process." and payload.method ~= "process.find" then
        payload.parameters.group = scope.id
    end

    local pending = { owner = scope }
    scope.pending[pending] = true
    pending.future = workpane.future(function()
        local completed, result = pcall(function()
            local value, problem = scope.session:request(payload.method, payload.parameters, 10000, pending)

            if problem ~= nil then
                error(problem, 0)
            end

            return value
        end)

        if pending.provider ~= nil then
            workpane.task(function()
                pending.provider:await()
                scope.pending[pending] = nil
            end)
        else
            scope.pending[pending] = nil
        end

        if not completed then
            error(result, 0)
        end

        if scope.closed and not (payload.method == "files.write" and pending.accepted) then
            error(protocol.failure("ssh_connection_closed"), 0)
        end

        return result
    end)

    return workpane.await(pending.future)
end

function workspace.close(payload, caller)
    declaration(payload, { workspace = "string" })
    local scope = owned(payload, caller)

    return workpane.await(close(scope))
end

local function connectionsChanged()
    for _, scope in pairs(scopes) do
        if connections.current(scope.session.profile.id) ~= scope.session then
            close(scope)
        end
    end
end

local function pluginsChanged(plugins)
    local active = {}

    for _, plugin in ipairs(plugins) do
        active[plugin.id] = plugin.state == "running" or plugin.state == "starting" or plugin.state == "stopping"
    end

    for _, scope in pairs(scopes) do
        if not active[scope.caller] then
            close(scope)
        end
    end
end

function workspace.start()
    stopping = false
    unwatch = connections.watch(connectionsChanged)
    unwatchPlugins = workpane.app.watchPlugins(pluginsChanged)
end

function workspace.stop()
    stopping = true
    unwatch()
    unwatchPlugins()
    local closing = {}

    for _, scope in pairs(scopes) do
        closing[#closing + 1] = close(scope)
    end

    async.allSettled(closing):await()
end

return workspace
