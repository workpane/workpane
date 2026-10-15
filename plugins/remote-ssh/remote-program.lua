local async = require("async")
local crypto = require("crypto")
local protocol = include("protocol")

local remoteProgram = {}

local Program = {}
Program.__index = Program

local function failed(code)
    error(protocol.failure(code), 0)
end

function remoteProgram.new(scope, identity, launch)
    local finished, finish = async.deferred()
    local created = setmetatable({ scope = scope, identity = identity, terminal = launch.terminal ~= nil, finished = finished, closed = false, chunks = {}, ended = false }, Program)
    created.id = workpane.process.start({ program = launch.program, arguments = launch.arguments, directory = launch.directory, variables = launch.variables, cleared = launch.cleared, input = launch.input, terminal = launch.terminal, pullOutput = true, onExit = function()
        created.nativeEnded = true
        finish()
    end })

    created.status = async.promise(function()
        created.statusAccepted = pcall(function()
            created.result = scope:wait(identity)
        end)
    end)

    created.completion = async.promise(function()
        async.race({ created.status, finished }):await()

        if created.nativeEnded and created.statusAccepted == nil then
            local _, expired = async.timeout(created.status, 10000):await()

            if expired ~= nil then
                created.failure = protocol.failure("ssh_program_failed")
                pcall(scope.release, scope, identity, 10000)
            end
        end

        created.status:await()
        local accepted = created.statusAccepted and pcall(scope.release, scope, identity, 10000)
        created.released = accepted == true

        if not accepted and created.cancelled then
            created.result = { code = -1, failed = true }
        end

        if not accepted and not created.closed and not created.cancelled then
            created.failure = protocol.failure("ssh_program_failed")
            pcall(workpane.process.stop, created.id)
        end
    end)

    return created
end

function Program:read(maximum)
    if self.reading or math.type(maximum) ~= "integer" or maximum < 1 or maximum > 65536 then
        failed("ssh_program_request_invalid")
    end

    self.reading = true
    local operation <close> = setmetatable({}, { __close = function()
        self.reading = false
    end })

    while #self.chunks == 0 and not self.ended do
        if self.closed then
            failed("ssh_connection_closed")
        end

        local value, problem = workpane.process.read(self.id, maximum):await()

        if problem ~= nil then
            error(problem, 0)
        end

        for _, chunk in ipairs(value.chunks) do
            local bytes = chunk.text

            if #bytes > 0 then
                self.chunks[#self.chunks + 1] = { stream = chunk.stream == "output" and "stdout" or "stderr", bytes = bytes }
            end
        end

        if value.ended then
            self.completion:await()

            if self.failure ~= nil or self.result == nil then
                error(self.failure or protocol.failure("ssh_program_failed"), 0)
            end

            self.code = self.result.code
            self.failed = self.result.failed
            self.ended = true
        end
    end

    local chunks = {}
    local remaining = maximum

    while #self.chunks > 0 and remaining > 0 do
        local chunk = self.chunks[1]
        local count = math.min(remaining, #chunk.bytes)
        chunks[#chunks + 1] = { stream = chunk.stream, content = crypto.base64Encode(chunk.bytes:sub(1, count)) }
        remaining = remaining - count

        if count == #chunk.bytes then
            table.remove(self.chunks, 1)
        else
            chunk.bytes = chunk.bytes:sub(count + 1)
        end
    end

    return { chunks = chunks, ended = self.ended and #self.chunks == 0, code = self.code or 0, failed = self.failed or false }
end

function Program:write(bytes)
    if self.closed or self.ended then
        failed("ssh_connection_closed")
    end

    workpane.process.write(self.id, bytes)
end

function Program:resize(columns, rows)
    if self.closed or self.ended or not self.terminal then
        failed("ssh_program_request_invalid")
    end

    workpane.process.resize(self.id, columns, rows)
end

function Program:stop()
    if self.stopping ~= nil then
        local _, problem = self.stopping:await()

        if problem ~= nil then
            error(problem, 0)
        end

        return
    end

    self.cancelled = true
    self.stopping = async.promise(function()
        if not self.released and not self.scope.closed then
            self.scope:release(self.identity, 10000)
            self.released = true
        end

        if not self.nativeEnded then
            workpane.process.stop(self.id)
        end

        self.completion:await()
    end)

    local _, problem = self.stopping:await()

    if problem ~= nil then
        error(problem, 0)
    end
end

function Program:close()
    if self.closing ~= nil then
        return self.closing
    end

    self.closed = true
    self.closing = async.promise(function()
        local accepted = pcall(function()
            if not self.released and not self.scope.closed then
                self.scope:release(self.identity, 10000)
                self.released = true
            end
        end)

        if not self.nativeEnded and not self.ended then
            workpane.process.stop(self.id)
        end

        self.finished:await()
        self.completion:await()

        if self.authentication ~= nil then
            local _, problem = self.authentication:close():await()
            accepted = accepted and problem == nil
            self.authentication = nil
        end

        self.chunks = {}

        return accepted and { value = true } or { failure = protocol.failure("ssh_cleanup_failed") }
    end)

    return self.closing
end

return remoteProgram
