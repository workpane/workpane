-- Serves folders over HTTP for plugins and hands each plugin the requests its servers answered.
local bridge = require("workpane.bridge")
local task = require("workpane.task")

local http = {}

local handlers = {}

-- A server is known by the request that started it, so its handler is in place before the first request it answers can arrive.
function http.serve(owner, options)
    if type(options) ~= "table" or (options.onRequest ~= nil and type(options.onRequest) ~= "function") then
        bridge.raise("http_options_invalid", "A server is started with a table whose request handler is a function", "")
    end

    local started = bridge.request("workpane_http_serve", { plugin = owner, host = options.host, port = options.port, root = options.root })
    local server = started.request
    handlers[server] = { owner = owner, onRequest = options.onRequest }

    return bridge.future(function()
        local reply, failure = started:await()

        if failure ~= nil then
            handlers[server] = nil
            error(failure, 0)
        end

        return reply
    end)
end

function http.stop(owner, server)
    bridge.call("workpane_http_stop", { plugin = owner, server = server })
    handlers[server] = nil
end

function http.forget(owner)
    for server, handler in pairs(handlers) do
        if handler.owner == owner then
            handlers[server] = nil
        end
    end

    bridge.call("workpane_http_forget", { plugin = owner })
end

bridge.on("workpane.http.requests", function(payload)
    local handler = handlers[payload.server]

    if handler ~= nil and handler.owner == payload.plugin and handler.onRequest ~= nil then
        task.run(handler.owner, "http", handler.onRequest, payload.requests)
    end
end)

return http
