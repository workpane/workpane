-- A client of one Model Context Protocol server, over the standard streams of a program or over streamable HTTP, whose tools, resources and prompts the agent may reach.
local async = require("async")
local http = require("http")
local codec = include("codec")
local connections = include("connections")

local mcp = {}

local Client = {}
Client.__index = Client

local protocolVersion = "2025-06-18"
local maximumMessageBytes = 8 * 1024 * 1024
local maximumToolPages = 64
local methodNotFound = -32601
local internalError = -32603
-- The post that carries a request lasts past the deadline of that request by this grace, so the deadline decides first and withdraws the request at the server.
local postGraceMs = 1000

function mcp.new(descriptor, startTimeoutMs, handlers)
    return setmetatable({ descriptor = descriptor, startTimeoutMs = startTimeoutMs, handlers = handlers, pending = {}, nextId = 0, tools = {}, ready = false, running = false, stopping = false, renewing = false, buffer = "" }, Client)
end

function Client:failure(code, message)
    return { code = code, message = message, detail = self.descriptor.id }
end

function Client:report(failure)
    if not self.stopping then
        self.handlers.failed(failure)
    end
end

-- A request answered with a failure of its own leaves every other request waiting for its answer.
function Client:complete(id, failure)
    local waiting = id ~= nil and self.pending[id] or nil

    if waiting == nil then
        return
    end

    self.pending[id] = nil
    waiting.failure = failure
    waiting.resolve()
end

-- Every waiting request is answered with the failure, so no caller waits for a server that is gone.
function Client:completeAll(failure)
    local pending = self.pending
    self.pending = {}

    for _, waiting in pairs(pending) do
        waiting.failure = failure
        waiting.resolve()
    end
end

function Client:send(message, timeoutMs)
    if self.descriptor.transport == "http" then
        self:post(message, timeoutMs)
        return
    end

    if self.process ~= nil then
        pcall(workpane.process.write, self.process, codec.encode(message) .. "\n")
    end
end

function Client:notify(method, params)
    self:send({ jsonrpc = "2.0", method = method, params = params or {} })
end

function Client:respond(id, result)
    self:send({ jsonrpc = "2.0", id = id, result = result })
end

function Client:respondWithError(id, code, message)
    self:send({ jsonrpc = "2.0", id = id, error = { code = code, message = message } })
end

-- Sends a request and waits for its answer within its deadline, raising the failure the server or the transport reported.
-- A request that outlives its deadline or that its caller cancels through the handle given to watch is withdrawn at the server, which stops spending work on it.
function Client:request(method, params, timeoutMs, watch)
    if not self.running then
        error(self:failure("ai_mcp_unavailable", "The MCP server is not running"), 0)
    end

    self.nextId = self.nextId + 1
    local id = self.nextId
    local answered, resolve = async.deferred()
    local waiting = { resolve = resolve }
    self.pending[id] = waiting

    if watch ~= nil then
        watch({ cancel = function()
            self:withdraw(id, self:failure("ai_mcp_cancelled", "The request was cancelled"))
        end })
    end

    self:send({ jsonrpc = "2.0", id = id, method = method, params = params or {} }, timeoutMs)
    async.timeout(answered, timeoutMs or self.startTimeoutMs):await()

    if self.pending[id] == waiting then
        self:withdraw(id, self:failure("ai_mcp_timeout", "The MCP server did not answer within its deadline"))
    end

    if waiting.failure ~= nil then
        error(waiting.failure, 0)
    end

    return waiting.result
end

function Client:withdraw(id, failure)
    if self.pending[id] == nil then
        return
    end

    self:notify("notifications/cancelled", { requestId = id, reason = failure.message })
    self:complete(id, failure)
end

function Client:dispatch(message)
    if type(message) ~= "table" or message.jsonrpc ~= "2.0" then
        self:report(self:failure("ai_mcp_message_invalid", "The MCP server returned an invalid JSON-RPC version"))
        self:stop()
        return
    end

    local hasId = message.id ~= nil and message.id ~= codec.null

    if type(message.method) == "string" and hasId then
        workpane.task(function()
            self:serverRequest(message)
        end)

        return
    end

    if type(message.method) == "string" then
        self:notification(message.method, type(message.params) == "table" and message.params or {})
        return
    end

    local waiting = hasId and self.pending[math.tointeger(message.id)] or nil

    if waiting == nil then
        return
    end

    self.pending[math.tointeger(message.id)] = nil

    if type(message.error) == "table" then
        waiting.failure = { code = "ai_mcp_error", message = type(message.error.message) == "string" and message.error.message or "The MCP server reported an error", detail = tostring(message.error.code or 0) }
    else
        waiting.result = type(message.result) == "table" and message.result or {}
    end

    waiting.resolve()
end

-- A server may call back into the client, every method the client does not implement is answered as such, and a request that fails is answered with an internal error.
function Client:serverRequest(message)
    local answered, failure = pcall(self.answer, self, message)

    if not answered then
        self:respondWithError(message.id, internalError, type(failure) == "table" and tostring(failure.message) or tostring(failure))
    end
end

function Client:answer(message)
    local method = message.method

    if method == "ping" then
        self:respond(message.id, {})
    elseif method == "roots/list" then
        local roots = codec.list()

        for index, root in ipairs(self.descriptor.roots) do
            roots[index] = { uri = workpane.files.uri(root), name = root:match("[^/\\]+$") or root }
        end

        self:respond(message.id, { roots = roots })
    elseif method == "sampling/createMessage" and self.descriptor.samplingEnabled then
        local answered, result = pcall(self.handlers.sampling, type(message.params) == "table" and message.params or {}, self.descriptor.samplingMaximumTokens)

        if answered then
            self:respond(message.id, result)
        else
            self:respondWithError(message.id, internalError, type(result) == "table" and result.message or tostring(result))
        end
    else
        self:respondWithError(message.id, methodNotFound, "The client does not implement \"" .. tostring(method) .. "\"")
    end
end

function Client:notification(method, params)
    if method == "notifications/tools/list_changed" then
        workpane.task(function()
            self:refreshTools()
        end)
    elseif method == "notifications/progress" then
        self.handlers.progress(params)
    end
end

-- The tools arrive page after page until the server names no further cursor, within a bounded number of pages, and a tool a server declares read-only may run beside another call of the same turn.
function Client:refreshTools()
    local tools = {}
    local cursor = nil

    for _ = 1, maximumToolPages do
        local answered, result = pcall(self.request, self, "tools/list", cursor ~= nil and { cursor = cursor } or {})

        if not answered then
            self:report(result)
            return false
        end

        for _, tool in ipairs(type(result.tools) == "table" and result.tools or {}) do
            if type(tool) == "table" and type(tool.name) == "string" and tool.name ~= "" then
                local annotations = type(tool.annotations) == "table" and tool.annotations or {}
                tools[#tools + 1] = { serverId = self.descriptor.id, name = tool.name, description = type(tool.description) == "string" and tool.description or "", inputSchema = type(tool.inputSchema) == "table" and tool.inputSchema or { type = "object", properties = {} }, readOnly = annotations.readOnlyHint == true }
            end
        end

        cursor = type(result.nextCursor) == "string" and result.nextCursor ~= "" and result.nextCursor or nil

        if cursor == nil then
            break
        end
    end

    if cursor ~= nil then
        workpane.log.warning("agent.mcp", "An MCP server listed more pages of tools than the client reads, and the rest were left out", { detail = self.descriptor.id })
    end

    self.tools = tools
    self.handlers.toolsChanged()

    return true
end

function Client:receive(chunks)
    for _, chunk in ipairs(chunks) do
        if chunk.stream == "output" then
            self.buffer = self.buffer .. chunk.text

            if #self.buffer > maximumMessageBytes then
                self:report(self:failure("ai_mcp_message_too_large", "The MCP server sent a message larger than the permitted size"))
                self:stop()
                return
            end

            -- Lines are cut at each newline found from where the last one ended, so one long answer costs its length once instead of its square.
            local start = 1
            local newline = self.buffer:find("\n", start, true)

            while newline ~= nil do
                local line = self.buffer:sub(start, newline - 1)

                if line:find("%S") ~= nil then
                    local message = codec.read(line, true)

                    if message == nil then
                        self:report(self:failure("ai_mcp_message_invalid", "The MCP server sent invalid JSON"))
                        self:stop()
                        return
                    end

                    self:dispatch(message)
                end

                start = newline + 1
                newline = self.buffer:find("\n", start, true)
            end

            self.buffer = self.buffer:sub(start)
        end
    end
end

-- The streamable transport posts every message to one address within the deadline of its request and a grace after it, and reads either one answer or an event stream.
-- A post that fails answers only its own request, and a key written as a reference to the environment is read from it.
function Client:post(message, timeoutMs)
    if self.stopping then
        return
    end

    local headers = { ["Content-Type"] = "application/json", accept = "application/json, text/event-stream", ["mcp-protocol-version"] = protocolVersion }
    local id = message.method ~= nil and message.id or nil

    if self.sessionId ~= nil then
        headers["mcp-session-id"] = self.sessionId
    end

    local resolved, key = pcall(connections.secret, self.descriptor.apiKey)

    if not resolved then
        local problem = self:failure(type(key) == "table" and key.code or "ai_mcp_failed", type(key) == "table" and key.message or tostring(key))
        self:report(problem)
        self:complete(id, problem)
        return
    end

    if key ~= "" then
        headers.authorization = "Bearer " .. key
    end

    workpane.task(function()
        local response, failure = http.client.requestRaw({ url = self.descriptor.url, method = "POST", headers = headers, body = codec.encode(message), timeoutSeconds = ((timeoutMs or self.startTimeoutMs) + postGraceMs) / 1000, maxResponseBytes = maximumMessageBytes }):await()

        -- A post whose request already ended, withdrawn at its deadline or cancelled, has nothing left to report.
        if self.stopping or (id ~= nil and self.pending[id] == nil) then
            return
        end

        if failure ~= nil then
            local problem = self:failure("ai_mcp_failed", tostring(failure))
            self:report(problem)
            self:complete(id, problem)
            return
        end

        -- A terminated session is started again from a fresh initialization rather than retried with the dead identity.
        if response.status == 404 and headers["mcp-session-id"] ~= nil then
            local expired = self:failure("ai_mcp_session_expired", "The MCP session is no longer valid")
            self:report(expired)
            self:complete(id, expired)
            self:renew()
            return
        end

        if type(response.headers["mcp-session-id"]) == "string" then
            self.sessionId = response.headers["mcp-session-id"]
        end

        if response.status >= 400 then
            local problem = self:failure("ai_mcp_failed", "HTTP " .. response.status)
            self:report(problem)
            self:complete(id, problem)
            return
        end

        local contentType = response.headers["content-type"] or ""

        if contentType:find("text/event-stream", 1, true) then
            for line in (response.body .. "\n"):gmatch("([^\n]*)\n") do
                local data = line:match("^%s*data:%s*(.-)%s*$")
                local decoded = data ~= nil and codec.read(data, true) or nil

                if decoded ~= nil then
                    self:dispatch(decoded)
                end
            end
        elseif response.body ~= "" then
            local decoded = codec.read(response.body, true)

            if decoded ~= nil then
                self:dispatch(decoded)
            end
        end
    end)
end

-- A server starts from its program or its address and has the time the catalog allows to finish its initialization.
function Client:start()
    if self.running then
        return
    end

    self.stopping = false
    self.ready = false
    self.sessionId = nil

    if self.descriptor.transport == "stdio" then
        local program = workpane.await(workpane.process.find(self.descriptor.command, {}))

        if program == nil and workpane.files.absolute(self.descriptor.command) then
            program = self.descriptor.command
        end

        if program == nil then
            self:report(self:failure("ai_mcp_failed", "The MCP server program was not found"))
            return
        end

        local started, identity = pcall(workpane.process.start, { program = program, arguments = self.descriptor.arguments, directory = self.descriptor.workdir ~= "" and self.descriptor.workdir or workpane.system.home(), onOutput = function(chunks)
            self:receive(chunks)
        end, onExit = function(code)
            self.running = false
            self.process = nil

            if not self.stopping then
                self.ready = false
                local failure = self:failure("ai_mcp_failed", "The MCP server exited with code " .. code)
                self:report(failure)
                self:completeAll(failure)
            end
        end })

        if not started then
            self:report(self:failure("ai_mcp_failed", type(identity) == "table" and identity.message or tostring(identity)))
            return
        end

        self.process = identity
    end

    self.running = true
    self:handshake()
end

-- A session the server ended is started once more from a fresh initialization, and the requests made meanwhile wait for none of it.
function Client:renew()
    if self.renewing or self.stopping then
        return
    end

    self.renewing = true
    self.ready = false
    self.sessionId = nil

    workpane.task(function()
        self:handshake()
        self.renewing = false
    end)
end

-- Initialization negotiates the protocol within the time the catalog allows a start, declares the roots of the server, which change only by starting it again, and sampling when allowed, and the server counts as ready once its tools arrived.
-- A server that does not complete it in time is stopped, so no run waits for it.
function Client:handshake()
    local capabilities = { roots = {} }

    if self.descriptor.samplingEnabled then
        capabilities.sampling = {}
    end

    local answered, result = pcall(self.request, self, "initialize", { protocolVersion = protocolVersion, capabilities = capabilities, clientInfo = { name = "Workpane", version = workpane.app.version } })

    if not answered then
        self:report(result)
        self:stop()
        return
    end

    if type(result.protocolVersion) ~= "string" or result.protocolVersion == "" then
        self:report(self:failure("ai_mcp_protocol_invalid", "The MCP server did not negotiate a protocol version"))
        self:stop()
        return
    end

    self:notify("notifications/initialized", {})

    if not self:refreshTools() then
        self:stop()
        return
    end

    self.ready = self.running and not self.stopping
end

-- Stopping cancels at the server what is pending, so it stops spending work nobody will read, then ends an HTTP session and the program.
function Client:stop()
    if not self.stopping then
        for id in pairs(self.pending) do
            self:notify("notifications/cancelled", { requestId = id, reason = "The client stopped the execution" })
        end
    end

    if self.descriptor.transport == "http" and self.sessionId ~= nil then
        local headers = { ["mcp-session-id"] = self.sessionId, ["mcp-protocol-version"] = protocolVersion }
        local url = self.descriptor.url
        self.sessionId = nil

        workpane.task(function()
            http.client.requestRaw({ url = url, method = "DELETE", headers = headers, timeoutSeconds = 5 }):await()
        end)
    end

    self.stopping = true
    self.ready = false
    self.running = false
    self:completeAll(self:failure("ai_mcp_stopped", "The MCP server was stopped"))

    if self.process ~= nil then
        pcall(workpane.process.stop, self.process)
        self.process = nil
    end
end

function Client:callTool(name, arguments, timeoutMs, watch)
    return self:request("tools/call", { name = name, arguments = arguments }, timeoutMs, watch)
end

function Client:listResources()
    return self:request("resources/list", {})
end

function Client:readResource(uri)
    return self:request("resources/read", { uri = uri })
end

function Client:listPrompts()
    return self:request("prompts/list", {})
end

function Client:getPrompt(name, arguments)
    return self:request("prompts/get", { name = name, arguments = arguments or {} })
end

return mcp
