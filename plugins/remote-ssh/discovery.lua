local async = require("async")
local program = include("program")
local protocol = include("protocol")

local discovery = {}

local Discovery = {}
Discovery.__index = Discovery

function discovery.command(nonce)
    return "echo WORKPANE_" .. nonce .. ":%OS%:$env:OS"
end

function discovery.platform(line, nonce)
    local marker = "WORKPANE_" .. nonce

    if line == marker .. ":Windows_NT:$env:OS" or line == marker .. ":%OS%:Windows_NT" then
        return "windows"
    end

    if line:sub(1, #marker + 6) == marker .. ":%OS%:" and line:sub(-3) == ":OS" then
        return "posix"
    end

    error(protocol.failure("ssh_platform_unavailable"), 0)
end

function discovery.start(executable, arguments, host, authentication, nonce)
    local command = { table.unpack(arguments) }
    command[#command + 1] = host
    command[#command + 1] = discovery.command(nonce)
    local channel = authentication:channel()
    local accepted, connection = pcall(program.new, executable, command, channel.variables, "")

    if not accepted then
        channel:close():await()
        error(connection, 0)
    end

    return setmetatable({ connection = connection, channel = channel, nonce = nonce }, Discovery)
end

function Discovery:read(timeout, cancellation)
    local line = self.connection:line("WORKPANE_" .. self.nonce .. ":", timeout, cancellation)

    return discovery.platform(line, self.nonce)
end

function Discovery:close()
    if self.closing == nil then
        self.closing = async.promise(function()
            local accepted, failure = pcall(self.connection.close, self.connection)
            local _, channelFailure = self.channel:close():await()

            if not accepted then
                error(failure, 0)
            end

            if channelFailure ~= nil then
                error(channelFailure, 0)
            end
        end)
    end

    local _, failure = self.closing:await()

    if failure ~= nil then
        error(failure, 0)
    end
end

return discovery
