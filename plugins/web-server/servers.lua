-- Runs the configured servers: it validates a configuration in the order the reader fixes it, starts and stops the servers and keeps the requests each one answered.
local fs = require("fs")
local address = include("address")
local store = include("store")

local translate = workpane.i18n.translate

local servers = {}

local maximumRequests = 500
local refusals = { http_root_invalid = "web-server.error.root-invalid", http_host_invalid = "web-server.error.host-invalid", http_port_invalid = "web-server.error.port-range" }

local configurations = {}
local running = {}
local pending = {}
local cancelled = {}
local requests = {}
local listeners = {}
local nextRequest = 0
local terminals = { active = "", list = {} }

-- Every listener hears which server changed and whether its state or only its requests did.
local function changed(id, what)
    for listener in pairs(listeners) do
        workpane.task(listener, id, what)
    end
end

local function trim(value)
    return type(value) == "string" and value:match("^%s*(.-)%s*$") or ""
end

-- A start the reader can act on is told in a notification and answered as a failure the form shows beside its fields.
local function startFailure(key, ...)
    local message = translate(key, ...)
    workpane.notify.error(translate("web-server.error.start-title"), message)

    return false, { code = "web_server_start_failed", message = message }
end

local function saveFailure()
    local message = translate("web-server.error.save-message")
    workpane.notify.error(translate("web-server.plugin.title"), message)

    return false, { code = "web_server_save_failed", message = message }
end

-- A batch keeps only its newest requests that fit, each presented once, ahead of the ones already kept and newest first.
local function recordRequests(id, answered)
    local list = requests[id]

    if list == nil then
        return
    end

    local first = math.max(1, #answered - maximumRequests + 1)
    local merged = {}

    for index = first, #answered do
        nextRequest = nextRequest + 1
        answered[index].id = tostring(nextRequest)
        answered[index].presented = workpane.time.localPresentation(answered[index].timestamp)
    end

    for index = #answered, first, -1 do
        merged[#merged + 1] = answered[index]
    end

    for index = 1, math.min(#list, maximumRequests - #merged) do
        merged[#merged + 1] = list[index]
    end

    requests[id] = merged
    changed(id, "requests")
end

local function serve(configuration)
    local id = configuration.id

    return workpane.http.serve({ host = configuration.host, port = configuration.port, root = configuration.root, onRequest = function(answered)
        recordRequests(id, answered)
    end }):await()
end

-- The operation of a server ends here, where a start the reader stopped meanwhile releases what it bound and a bound server starts keeping its requests.
local function settle(configuration, reply, failure)
    local id = configuration.id
    local stopped = cancelled[id]
    pending[id] = nil
    cancelled[id] = nil

    if stopped and reply ~= nil then
        workpane.http.stop(reply.server)
    end

    if not stopped and failure == nil then
        running[id] = { server = reply.server }
        requests[id] = {}
    end

    changed(id, "servers")

    if stopped then
        return false, { code = "web_server_start_cancelled", message = "The Web Server start was cancelled" }
    end

    if failure ~= nil and failure.code == "http_bind_failed" then
        return startFailure("web-server.error.bind", configuration.host, configuration.port)
    end

    if failure ~= nil and refusals[failure.code] ~= nil then
        return startFailure(refusals[failure.code])
    end

    if failure ~= nil then
        error(failure, 0)
    end

    workpane.notify.success(translate("web-server.notification.started"), translate("web-server.notification.serving", configuration.root, configuration.host, configuration.port))

    return true, nil
end

function servers.load(stored)
    configurations = stored
end

-- Answers the function that stops the listener, which a form calls when it closes.
function servers.subscribe(listener)
    listeners[listener] = true

    return function()
        listeners[listener] = nil
    end
end

-- Servers are listed by name the way a reader sorts them, ignoring case.
function servers.list()
    local listed = {}

    for _, configuration in ipairs(configurations) do
        listed[#listed + 1] = configuration
    end

    table.sort(listed, function(left, right)
        local leftName, rightName = left.name:lower(), right.name:lower()

        if leftName == rightName then
            return left.id < right.id
        end

        return leftName < rightName
    end)

    return listed
end

function servers.find(id)
    for _, configuration in ipairs(configurations) do
        if configuration.id == id then
            return configuration
        end
    end

    return nil
end

function servers.state(id)
    if running[id] ~= nil then
        return "running"
    end

    return pending[id] and "working" or "stopped"
end

function servers.runningCount()
    local count = 0

    for _ in pairs(running) do
        count = count + 1
    end

    return count
end

function servers.requests(id)
    return requests[id] or {}
end

function servers.clearRequests(id)
    if requests[id] == nil then
        return
    end

    requests[id] = {}
    changed(id, "requests")
end

-- A new server is offered the first port from 8080 that no other configuration took, so it can start at once.
function servers.availablePort()
    local taken = {}

    for _, configuration in ipairs(configurations) do
        taken[configuration.port] = true
    end

    local port = 8080

    while port < 65535 and taken[port] do
        port = port + 1
    end

    return port
end

-- A request of another plugin carries exactly the path of a readable folder, and answers that folder by its canonical path with the server that already serves it, reached by any path.
function servers.requested(payload)
    local keys = 0

    for _ in pairs(type(payload) == "table" and payload or {}) do
        keys = keys + 1
    end

    if type(payload) ~= "table" or type(payload.path) ~= "string" or keys ~= 1 then
        error({ code = "web_server_request_invalid", message = "A request to serve a folder carries exactly its path", detail = "" }, 0)
    end

    -- A root keeps its separator, since a drive named without one is the folder that drive last stood in.
    local path = payload.path:match("^(.-)[/\\]*$")

    if path == "" or path:match("^%a:$") then
        path = path .. payload.path:sub(#path + 1, #path + 1)
    end

    local info = fs.stat(path):await()

    if info == nil or not info.isDir then
        error({ code = "web_server_root_invalid", message = "The folder to serve is not a readable folder", detail = payload.path }, 0)
    end

    -- Stored roots are canonical, so the folder is compared by its canonical path and a link to it finds its server.
    local canonical, unresolved = workpane.files.canonical(path):await()

    if unresolved ~= nil then
        error({ code = "web_server_root_invalid", message = "The folder to serve is not a readable folder", detail = payload.path }, 0)
    end

    for _, configuration in ipairs(configurations) do
        if configuration.root == canonical then
            return canonical, configuration
        end
    end

    return canonical, nil
end

function servers.forTerminal(terminalId)
    if terminalId == nil or terminalId == "" then
        return nil
    end

    for _, configuration in ipairs(configurations) do
        if configuration.terminalId == terminalId then
            return configuration
        end
    end

    return nil
end

function servers.terminal(terminalId)
    for _, terminal in ipairs(terminals.list) do
        if terminal.id == terminalId then
            return terminal
        end
    end

    return nil
end

function servers.activeTerminal()
    return terminals.active
end

-- A link to a terminal that closed is dropped and saved, and the server it belonged to keeps running.
function servers.unlinkTerminal(terminalId)
    local kept = {}

    for _, terminal in ipairs(terminals.list) do
        if terminal.id ~= terminalId then
            kept[#kept + 1] = terminal
        end
    end

    terminals.list = kept

    if terminals.active == terminalId then
        terminals.active = ""
    end

    local unlinked = false

    for _, configuration in ipairs(configurations) do
        if configuration.terminalId == terminalId then
            configuration.terminalId = nil
            unlinked = true
            local _, failure = store.save(configuration):await()

            if failure ~= nil then
                saveFailure()
            end
        end
    end

    if unlinked then
        changed(nil, "servers")
    end
end

-- A snapshot replaces the terminals the plugin knows, and a server linked to a terminal the snapshot no longer holds loses its link, which is the only change the servers show.
function servers.setTerminals(active, list)
    terminals = { active = active, list = list }
    local unavailable = {}

    for _, configuration in ipairs(configurations) do
        if configuration.terminalId ~= nil and servers.terminal(configuration.terminalId) == nil then
            unavailable[#unavailable + 1] = configuration.terminalId
        end
    end

    for _, terminalId in ipairs(unavailable) do
        servers.unlinkTerminal(terminalId)
    end
end

-- The checks run in the order a reader fixes them, and a configuration whose port is taken is still saved so what was typed is kept.
function servers.configureAndStart(fields)
    local id = fields.id
    local name = trim(fields.name)
    local host = trim(fields.host)
    local port = fields.port
    -- A form opened from a terminal that closed meanwhile starts a server of its own, since a link to that terminal is no link.
    local terminalId = fields.terminalId ~= "" and servers.terminal(fields.terminalId) ~= nil and fields.terminalId or nil

    if pending[id] then
        return startFailure("web-server.error.operation-pending")
    end

    if running[id] ~= nil then
        return startFailure("web-server.error.stop-before-change")
    end

    if type(id) ~= "string" or id == "" or name == "" then
        return startFailure("web-server.error.name-invalid")
    end

    if math.type(port) ~= "integer" or port < 1 or port > 65535 then
        return startFailure("web-server.error.port-range")
    end

    if not address.numeric(host) then
        return startFailure("web-server.error.host-invalid")
    end

    local linked = servers.forTerminal(terminalId)

    if linked ~= nil and linked.id ~= id then
        return startFailure("web-server.error.configuration-unavailable")
    end

    pending[id] = true
    changed(id, "servers")
    local candidate = { id = id, name = name, root = trim(fields.root), host = host, port = port, terminalId = terminalId }
    local reply, failure = serve(candidate)

    if failure ~= nil and failure.code ~= "http_bind_failed" then
        return settle(candidate, reply, failure)
    end

    -- A server that could not bind is still saved with the canonical root the running one would have, and a root that no longer resolves is refused.
    if reply ~= nil then
        candidate.root = reply.root
    else
        local canonical, unresolved = workpane.files.canonical(candidate.root):await()

        if unresolved ~= nil then
            return settle(candidate, reply, { code = "http_root_invalid", message = "The document root is not a readable directory", detail = candidate.root })
        end

        candidate.root = canonical
    end

    local _, storeFailure = store.save(candidate):await()

    if storeFailure ~= nil then
        pending[id] = nil
        cancelled[id] = nil

        if reply ~= nil then
            workpane.http.stop(reply.server)
        end

        changed(id, "servers")
        return saveFailure()
    end

    local previous = servers.find(id)

    if previous == nil then
        configurations[#configurations + 1] = candidate
    else
        previous.name, previous.root, previous.host, previous.port, previous.terminalId = candidate.name, candidate.root, candidate.host, candidate.port, candidate.terminalId
    end

    return settle(candidate, reply, failure)
end

function servers.start(id)
    local configuration = servers.find(id)

    if configuration == nil then
        return startFailure("web-server.error.configure-first")
    end

    if running[id] ~= nil then
        return true, nil
    end

    if pending[id] then
        return startFailure("web-server.error.operation-pending")
    end

    pending[id] = true
    changed(id, "servers")
    local reply, failure = serve(configuration)

    return settle(configuration, reply, failure)
end

-- Stopping a server that is still starting cancels the start, and stopping one that runs releases its port at once.
function servers.stop(id)
    if pending[id] then
        cancelled[id] = true
    end

    local instance = running[id]

    if instance == nil then
        return
    end

    workpane.http.stop(instance.server)
    running[id] = nil
    requests[id] = nil
    changed(id, "servers")
    changed(id, "requests")
    workpane.notify.success(translate("web-server.notification.stopped"), translate("web-server.notification.stopped-detail", servers.find(id).name))
end

-- A removal the reader confirmed and that did not happen is told in a notification, because an action that does nothing reads as a broken product.
function servers.remove(id)
    if pending[id] then
        workpane.notify.error(translate("web-server.plugin.title"), translate("web-server.error.operation-pending"))
        return false
    end

    local position

    for index, configuration in ipairs(configurations) do
        if configuration.id == id then
            position = index
        end
    end

    if position == nil then
        workpane.notify.error(translate("web-server.plugin.title"), translate("web-server.error.configuration-unavailable"))
        return false
    end

    servers.stop(id)
    pending[id] = true
    local removed = table.remove(configurations, position)
    changed(id, "servers")
    local _, failure = store.remove(id):await()
    pending[id] = nil

    if failure ~= nil then
        table.insert(configurations, position, removed)
        changed(id, "servers")
        workpane.notify.error(translate("web-server.plugin.title"), translate("web-server.error.remove-message"))
        return false
    end

    changed(id, "servers")

    return true
end

-- A server has an address only while it runs, because that is the only time the address answers.
function servers.url(id)
    local configuration = servers.find(id)

    if configuration == nil or running[id] == nil then
        return nil
    end

    return address.url(configuration.host, configuration.port)
end

return servers
