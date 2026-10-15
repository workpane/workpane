local async = require("async")
local packets = include("sftp-packets")
local program = include("program")

local sftp = {}

local Client = {}
Client.__index = Client

local types = packets.types
local replies = { [types.open] = types.handle, [types.close] = types.status, [types.read] = types.data, [types.write] = types.status, [types.lstat] = types.attrs, [types.fstat] = types.attrs, [types.setstat] = types.status, [types.fsetstat] = types.status, [types.opendir] = types.handle, [types.readdir] = types.name, [types.remove] = types.status, [types.mkdir] = types.status, [types.rmdir] = types.status, [types.realpath] = types.name, [types.stat] = types.attrs, [types.rename] = types.status, [types.readlink] = types.name, [types.extended] = types.status }

local function failure(code)
    return { code = code, message = "The SFTP operation could not be completed", detail = "" }
end

local function deadline(timeoutMs)
    return math.type(timeoutMs) == "integer" and timeoutMs > 0 and timeoutMs <= 120000
end

local function cancel(operation)
    if not operation.released and not operation.owner.closed then
        operation.owner:abort("ssh_sftp_cancelled")
    end
end

local operationGuard = { __close = cancel }

function sftp.new(launch)
    local ready, prepared = async.deferred()
    local finished, finish = async.deferred()
    local created = setmetatable({ ready = ready, prepared = prepared, finished = finished, finish = finish, decoder = packets.decoder(), pending = {}, count = 0, bytes = 0, nextId = 0, closed = false, ended = false, started = false }, Client)
    created.id = workpane.process.start({ program = launch.program, arguments = launch.arguments, directory = launch.directory, variables = launch.variables, cleared = launch.cleared, binaryOutput = true, onOutput = function(chunks)
        if created.closed then
            return
        end

        for _, chunk in ipairs(chunks) do
            if chunk.stream == "error" then
                created.diagnostic = ((created.diagnostic or "") .. chunk.text):sub(-16384)
                created.reason = program.reason(created.diagnostic) or created.reason
            else
                local accepted = pcall(created.decoder.feed, created.decoder, chunk.text, function(value)
                    created:receive(value)
                end)

                if not accepted then
                    created:abort("ssh_sftp_response_invalid")
                    return
                end
            end
        end
    end, onExit = function(code, unsuccessful)
        created.ended = true
        created.code = code
        created.failed = unsuccessful
        created.diagnostic = nil

        if not created.closed then
            created:abort(not created.decoder:complete() and "ssh_sftp_response_invalid" or created.reason or "ssh_connection_lost")
        end

        created.finish()

        if launch.onExit ~= nil then
            launch.onExit()
        end
    end })

    return created
end

function Client:abort(code, graceful)
    if self.closed then
        return
    end

    self.closed = true
    self.failure = failure(code)
    self.diagnostic = nil
    self.prepared()

    for _, operation in pairs(self.pending) do
        operation.failure = self.failure
        operation.resolve()
    end

    self.pending = {}
    self.count = 0
    self.bytes = 0

    if not self.ended then
        self.graceful = graceful == true
        local accepted, problem = pcall(graceful and workpane.process.endInput or workpane.process.stop, self.id)

        if not accepted and (type(problem) ~= "table" or problem.code ~= "process_unknown") then
            self.failure = failure("ssh_cleanup_failed")

            pcall(workpane.process.stop, self.id)
        end
    end
end

function Client:send(bytes)
    local accepted = pcall(workpane.process.write, self.id, bytes)

    if not accepted then
        self:abort("ssh_transport_failed")
        return false
    end

    return true
end

function Client:receive(value)
    if value.type == packets.types.version then
        if not self.started or self.extensions ~= nil then
            error(failure("ssh_sftp_response_invalid"), 0)
        end

        self.extensions = value.extensions
        self.prepared()
        return
    end

    local operation = self.pending[value.id]

    if self.extensions == nil or operation == nil or (value.type ~= operation.expected and value.type ~= packets.types.status) or (value.type == packets.types.status and value.status == 0 and operation.expected ~= packets.types.status) then
        error(failure("ssh_sftp_response_invalid"), 0)
    end

    if value.type == packets.types.data and #value.data > operation.maximumData then
        error(failure("ssh_sftp_response_invalid"), 0)
    end

    self.pending[value.id] = nil
    self.count = self.count - 1
    self.bytes = self.bytes - operation.bytes
    operation.value = value
    operation.resolve()
end

function Client:start(timeoutMs)
    if not deadline(timeoutMs) or self.started then
        return nil, failure("ssh_sftp_request_invalid")
    end

    if self.closed then
        return nil, self.failure
    end

    self.started = true
    local operation <close> = setmetatable({ owner = self, id = 0, resolve = self.prepared }, operationGuard)
    self.pending[0] = operation

    if not self:send(packets.frame(packets.types.init, packets.integer(3))) then
        self.pending[0] = nil
        return nil, self.failure
    end

    local _, expired = async.timeout(self.ready, timeoutMs):await()
    self.pending[0] = nil

    if expired ~= nil then
        self:abort("ssh_request_timeout")
        return nil, self.failure
    end

    if self.closed then
        return nil, self.failure
    end

    operation.released = true

    return true
end

function Client:request(kind, payload, expected, timeoutMs, maximumData)
    if self.closed then
        return nil, self.failure
    end

    maximumData = maximumData or packets.maximumData

    if not deadline(timeoutMs) or self.extensions == nil or replies[kind] ~= expected or expected == nil or type(payload) ~= "string" or #payload > packets.maximumPacket - 5 or math.type(maximumData) ~= "integer" or maximumData < 1 or maximumData > packets.maximumData then
        return nil, failure("ssh_sftp_request_invalid")
    end

    if self.count >= 64 or #payload > 4 * 1024 * 1024 - self.bytes or self.nextId == 0xffffffff then
        return nil, failure("ssh_channel_full")
    end

    local ready, resolve = async.deferred()
    self.nextId = self.nextId + 1
    local operation <close> = setmetatable({ owner = self, id = self.nextId, resolve = resolve, expected = expected, bytes = #payload, maximumData = maximumData }, operationGuard)
    self.pending[operation.id] = operation
    self.count = self.count + 1
    self.bytes = self.bytes + operation.bytes

    if not self:send(packets.request(kind, operation.id, payload)) then
        return nil, self.failure
    end

    local _, expired = async.timeout(ready, timeoutMs):await()

    if expired ~= nil then
        self:abort("ssh_request_timeout")
        return nil, self.failure
    end

    operation.released = true

    return operation.value, operation.failure
end

function Client:close()
    self:abort("ssh_connection_closed", true)

    local _, expired = async.timeout(self.finished, 10000):await()

    if expired ~= nil then
        self.failure = failure("ssh_cleanup_failed")
        pcall(workpane.process.stop, self.id)
        self.finished:await()
    end

    if self.failure.code == "ssh_cleanup_failed" or self.graceful and (self.code ~= 0 or self.failed) then
        return nil, failure("ssh_cleanup_failed")
    end

    return true
end

return sftp
