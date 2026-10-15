local async = require("async")
local crypto = require("crypto")
local protocol = include("protocol")
local source = include("source")

local shell = {}

local Shell = {}
Shell.__index = Shell

local function failed(code)
    error(protocol.failure(code), 0)
end

function shell.new(launcher, account, root)
    return setmetatable({ launcher = launcher, account = account, root = root, histories = {}, count = 0 }, Shell)
end

function Shell:history(identity)
    if type(identity) ~= "string" or #identity ~= 32 or identity:match("^[0-9a-f]+$") == nil then
        failed("remote_terminal_history_invalid")
    end

    local history = self.histories[identity]

    if history ~= nil then
        history.preparing:await()

        if history.failure ~= nil or history.closed then
            error(history.failure or protocol.failure("remote_terminal_history_invalid"), 0)
        end

        return history
    end

    if self.count >= 16 then
        failed("remote_terminal_limit")
    end

    history = { users = 0, closed = false }
    self.histories[identity] = history
    self.count = self.count + 1
    history.preparing = async.promise(function()
        local accepted, problem = pcall(function()
            local files = self.launcher.files
            history.stage = self.launcher.owner:stage(self.launcher.owner.path, 10000)
            history.directory = history.stage.path
            local assets = { [".zshenv"] = "ssh-zshenv.sh", [".zprofile"] = "ssh-zprofile.sh", [".zshrc"] = "ssh-zshrc.sh", bashrc = "ssh-bashrc.sh" }

            if self.launcher.owner.platform ~= "windows" then
                for filename, asset in pairs(assets) do
                    files:upload(history.directory .. "/" .. filename, source.asset(asset), 384)
                end
            end
        end)

        history.failure = not accepted and problem or nil
    end)

    history.preparing:await()

    if history.failure ~= nil then
        error(history.failure, 0)
    end

    return history
end

function Shell:release(identity)
    local history = self.histories[identity]

    if history == nil then
        return {}
    end

    if history.users ~= 0 then
        failed("remote_terminal_history_invalid")
    end

    history.closed = true
    history.preparing:await()
    self.launcher.owner:unstage(history.stage, 10000)
    self.histories[identity] = nil
    self.count = self.count - 1

    return {}
end

function Shell:start(directory, historyId, columns, rows, reserved, cancelled)
    local history = self:history(historyId)
    local owner = self.launcher.owner
    local declaration = { program = self.account.shell, arguments = {}, directory = directory, variables = { TERM = "xterm-256color", COLORTERM = "truecolor", TERM_PROGRAM = workpane.app.name }, cleared = {}, terminal = { columns = columns, rows = rows } }

    if owner.platform == "windows" then
        local script = source.asset("ssh-shell-windows.ps1")
        local encoded = script:gsub(".", function(byte)
            return byte .. "\0"
        end)

        declaration.arguments = { "-NoLogo", "-NoExit", "-EncodedCommand", crypto.base64Encode(encoded) }
        declaration.variables.WORKPANE_HISTORY_FILE = crypto.base64Encode(history.directory .. "/commands.history")
    else
        declaration.program = "/bin/sh"
        declaration.arguments = { "-c", source.asset("ssh-shell-posix.sh"), "workpane", self.account.shell, history.directory, owner.platform }
    end

    history.users = history.users + 1
    local accepted, running = pcall(self.launcher.start, self.launcher, declaration, reserved, cancelled)

    if not accepted then
        history.users = history.users - 1
        error(running, 0)
    end

    return { running = running, history = history, directory = directory, pending = "" }
end

function Shell:directory(terminal, bytes)
    local pending = terminal.pending .. bytes
    local position = 1

    while true do
        local start = pending:find("\27]7;", position, true)

        if start == nil then
            terminal.pending = pending:sub(-4)
            return
        end

        local bell = pending:find("\7", start + 4, true)
        local escape = pending:find("\27\\", start + 4, true)
        local finish = bell ~= nil and (escape == nil or bell < escape) and bell or escape

        if finish == nil then
            terminal.pending = #pending - start < 8192 and pending:sub(start) or ""
            return
        end

        local uri = pending:sub(start + 4, finish - 1)
        position = finish + (finish == escape and 2 or 1)

        if #uri <= 8192 and uri:find("%z") == nil and utf8.len(uri) ~= nil then
            local accepted, value = pcall(workpane.files.paths(self.launcher.owner.platform).path, uri)

            if accepted and type(value) == "string" and #value <= 4096 then
                terminal.directory = value
            end
        end
    end
end

function Shell:read(terminal, limit)
    local value = terminal.running:read(limit)
    local bytes = {}

    for _, chunk in ipairs(value.chunks) do
        local content = crypto.base64Decode(chunk.content)
        self:directory(terminal, content)
        bytes[#bytes + 1] = content
    end

    return { content = crypto.base64Encode(table.concat(bytes)), ended = value.ended, code = value.code, directory = terminal.directory }
end

function Shell:close(terminal)
    if terminal.closing == nil then
        terminal.closing = async.promise(function()
            local result, problem = terminal.running:close():await()
            terminal.history.users = terminal.history.users - 1

            if problem ~= nil or result.failure ~= nil then
                error(problem or result.failure, 0)
            end
        end)
    end

    local _, problem = terminal.closing:await()

    if problem ~= nil then
        error(problem, 0)
    end
end

return shell
