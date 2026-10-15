local bridge = require("workpane.bridge")
local crypto = require("crypto")
local http = require("http")
local lifecycle = require("workpane.lifecycle")
local paths = require("workpane.paths")
local task = require("workpane.task")

local askpass = {}
local owners = {}
local sessions = setmetatable({}, { __mode = "k" })

local Session = {}
local sessionType = { __index = Session, __metatable = "Workpane askpass session" }

local function close(session)
    local state = sessions[session]

    if state == nil then
        bridge.raise("askpass_session_invalid", "The credential session is invalid", "")
    end

    if state.closed then
        return
    end

    state.closed = true
    owners[state.owner][session] = nil

    if state.cancel ~= nil then
        local cancelled = pcall(state.cancel)

        if not cancelled then
            bridge.report(state.owner, "process", "A credential prompt could not be cancelled", { code = "askpass_cancel_failed" })
        end
    end

    return state.server:close()
end

function Session:close()
    local state = sessions[self]

    if state == nil then
        bridge.raise("askpass_session_invalid", "The credential session is invalid", "")
    end

    lifecycle.check(state.owner)
    local closing = close(self)

    return task.future(state.owner, function()
        if closing ~= nil then
            closing:await()
        end
    end)
end

function askpass.serve(owner, directory, platform, options)
    lifecycle.check(owner)

    if type(options) ~= "table" or type(options.onPrompt) ~= "function" or (options.onCancel ~= nil and type(options.onCancel) ~= "function") or not paths.absolute(platform, directory) then
        bridge.raise("askpass_options_invalid", "A credential session needs a prompt handler and an absolute executable directory", "")
    end

    for key in pairs(options) do
        if key ~= "onPrompt" and key ~= "onCancel" then
            bridge.raise("askpass_options_invalid", "The credential session option is unknown", key)
        end
    end

    local session = setmetatable({}, sessionType)
    local state = { owner = owner, token = crypto.hexEncode(crypto.randomBytes(32)), cancel = options.onCancel, closed = false, pending = false }

    local function answer(request, response)
        response:setHeader("Cache-Control", "no-store")

        if state.closed or request.method ~= "POST" or request.path ~= "/credential" or not crypto.equals(request.headers.Authorization or "", "Bearer " .. state.token) then
            response:status(403)
            response:finish()
            return
        end

        local kind = request.headers["X-Workpane-Prompt"]
        local prompt = request.body

        if (kind ~= "input" and kind ~= "confirm" and kind ~= "none") or type(prompt) ~= "string" or prompt == "" or #prompt > 16384 or prompt:find("\0", 1, true) ~= nil or utf8.len(prompt) == nil then
            response:status(400)
            response:finish()
            return
        end

        if state.pending then
            response:status(409)
            response:finish()
            return
        end

        state.pending = true
        local operation = task.future(owner, options.onPrompt, prompt, kind)
        local value, failure = operation:await()
        state.pending = false

        if state.closed or failure ~= nil or value == nil then
            response:status(403)
            response:finish()
            return
        end

        if type(value) ~= "string" or #value > 1023 or value:find("[%z\r\n]") ~= nil or (kind == "confirm" and value ~= "yes" and value ~= "no") then
            response:status(400)
            response:finish()
            return
        end

        response:setHeader("Content-Type", "text/plain; charset=utf-8")
        response:finish(value)
    end

    state.server = http.createServer(answer):listen({ host = "127.0.0.1", port = 0, requestTimeoutMs = 120000, maxRequestBodyBytes = 16384, maxQueued = 8, compress = false })
    sessions[session] = state
    owners[owner] = owners[owner] or {}
    owners[owner][session] = true
    session.variables = { SSH_ASKPASS = directory .. "/WorkpaneAskpass" .. (platform == "windows" and ".exe" or ""), SSH_ASKPASS_REQUIRE = "force", WORKPANE_ASKPASS_PORT = tostring(state.server.port), WORKPANE_ASKPASS_TOKEN = state.token }

    return session
end

function askpass.forget(owner)
    local active = owners[owner] or {}

    for session in pairs(active) do
        close(session)
    end

    owners[owner] = nil
end

return askpass
