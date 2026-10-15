local async = require("async")
local protocol = include("protocol")

local program = {}

local Program = {}
Program.__index = Program

local function failed(code)
    error(protocol.failure(code), 0)
end

function program.reason(source)
    local known = {
        { "REMOTE HOST IDENTIFICATION HAS CHANGED", "ssh_host_key_changed" },
        { "Host key verification failed", "ssh_host_key_changed" },
        { "Permission denied", "ssh_authentication_refused" },
        { "Could not resolve hostname", "ssh_host_unresolved" },
        { "Connection timed out", "ssh_connection_timeout" },
        { "Connection refused", "ssh_host_refused" },
        { "subsystem request failed", "ssh_transfer_unavailable" },
    }

    for _, entry in ipairs(known) do
        if source:find(entry[1], 1, true) ~= nil then
            return entry[2]
        end
    end
end

function program.new(executable, arguments, variables, input, onExit)
    local finished, finish = async.deferred()
    local changed, change = async.deferred()
    local created = setmetatable({ finished = finished, finish = finish, changed = changed, change = change, lines = {}, pending = "", bytes = 0, ended = false, stopping = false }, Program)

    created.id = workpane.process.start({ program = executable, arguments = arguments, directory = workpane.app.dataDirectory, variables = variables, input = input, onOutput = function(chunks)
        for _, chunk in ipairs(chunks) do
            if chunk.stream == "error" then
                created.diagnostic = ((created.diagnostic or "") .. chunk.text):sub(-16384)
                created.reason = program.reason(created.diagnostic) or created.reason
            end

            if chunk.stream == "output" and created.failure == nil then
                created.bytes = created.bytes + #chunk.text

                if created.bytes > 65536 then
                    created.failure = "ssh_output_invalid"
                    created:stop()
                    break
                end

                created.pending = created.pending .. chunk.text

                while true do
                    local boundary = created.pending:find("\n", 1, true)

                    if boundary == nil then
                        break
                    end

                    if #created.lines >= 64 then
                        created.failure = "ssh_output_invalid"
                        created:stop()
                        break
                    end

                    created.lines[#created.lines + 1] = { text = created.pending:sub(1, boundary - 1):gsub("\r$", ""), bytes = boundary }
                    created.pending = created.pending:sub(boundary + 1)
                end
            end
        end

        created.change()
        created.changed, created.change = async.deferred()
    end, onExit = function(code, unsuccessful)
        created.ended = true
        created.code = code
        created.failed = unsuccessful
        created.diagnostic = nil
        created.finish()
        created.change()

        if onExit ~= nil then
            onExit()
        end
    end })

    return created
end

function Program:line(prefix, timeoutMs, cancellation)
    local captured
    local waitFailed = false
    local cancelled, cancel = async.deferred()
    local operation <close> = setmetatable({ closed = false }, { __close = function(state)
        state.closed = true
        cancel()
    end })

    local pending = async.promise(function()
        while not cancellation.closed and not operation.closed do
            while #self.lines > 0 do
                local consumed = table.remove(self.lines, 1)
                self.bytes = self.bytes - consumed.bytes
                local line = consumed.text

                if line:sub(1, #prefix) == prefix then
                    captured = line
                    return
                end
            end

            if self.ended or self.failure ~= nil then
                return
            end

            local _, problem = async.race({ self.changed, cancellation.finished, cancelled }):await()

            if problem ~= nil then
                waitFailed = true
                return
            end
        end
    end)

    local _, expired = async.timeout(pending, timeoutMs):await()

    if cancellation.closed then
        failed("ssh_connection_closed")
    end

    if expired ~= nil then
        failed("ssh_connection_timeout")
    end

    if waitFailed or self.failure ~= nil or captured == nil then
        failed(self.failure or self.reason or "ssh_program_failed")
    end

    return captured
end

function Program:write(value)
    if self.ended or self.stopping then
        failed("ssh_connection_closed")
    end

    workpane.process.write(self.id, value)
end

function Program:close()
    self:stop()

    self.finished:await()
end

function Program:stop()
    if self.ended or self.stopping then
        return
    end

    self.stopping = true
    local accepted, failure = pcall(workpane.process.stop, self.id)

    if not accepted and (type(failure) ~= "table" or failure.code ~= "process_unknown") then
        failed("ssh_program_failed")
    end
end

return program
