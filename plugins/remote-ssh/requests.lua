local async = require("async")
local protocol = include("protocol")

local requests = {}

local Requests = {}
Requests.__index = Requests

local independent = { ["session.ping"] = true, ["process.read"] = true, ["process.write"] = true, ["process.stop"] = true, ["process.group.close"] = true, ["terminal.read"] = true, ["terminal.write"] = true, ["terminal.resize"] = true, ["terminal.close"] = true }
local readers = { ["process.read"] = true, ["terminal.read"] = true }

local function cancel(operation)
    if operation.closed or operation.released then
        return
    end

    operation.closed = true
    operation.finish()
    local owner = operation.owner

    for index, waiting in ipairs(owner.waiting) do
        if waiting == operation then
            table.remove(owner.waiting, index)
            break
        end
    end

    if operation.admitted then
        owner.transport:cancel(operation)
    end

    owner:admit()
end

local operationGuard = { __close = cancel }

function requests.new(transport)
    local finished, finish = async.deferred()

    return setmetatable({ transport = transport, waiting = {}, operations = {}, active = 0, count = 0, bytes = 0, heartbeat = false, closed = false, finished = finished, finish = finish }, Requests)
end

function Requests:admit()
    while not self.closed and self.active < 3 and #self.waiting > 0 do
        local operation = table.remove(self.waiting, 1)

        if not operation.closed then
            operation.admitted = true
            self.active = self.active + 1
            operation.ready()
        end
    end
end

function Requests:request(method, parameters, timeoutMs, context)
    if self.closed then
        return nil, protocol.failure("ssh_connection_closed")
    end

    if math.type(timeoutMs) ~= "integer" or timeoutMs < 1 or timeoutMs > 120000 then
        return nil, protocol.failure("ssh_deadline_invalid")
    end

    local body, invalid = protocol.request(method, parameters)

    if invalid ~= nil then
        return nil, invalid
    end

    local declaration = protocol.decode(body)
    local heartbeat = method == "session.ping"
    local direct = independent[method] == true

    if heartbeat and self.heartbeat or self.count >= 128 or #self.waiting >= 64 and not direct or #body > 64 * 1024 * 1024 - self.bytes then
        return nil, protocol.failure("ssh_channel_full")
    end

    local finished, finish = async.deferred()
    local admitted, ready = async.deferred()
    local operation <close> = setmetatable({ owner = self, finished = finished, finish = finish, ready = ready, closed = false, admitted = direct }, operationGuard)
    self.operations[operation] = true
    self.count = self.count + 1
    self.bytes = self.bytes + #body

    if direct then
        self.heartbeat = self.heartbeat or heartbeat
        ready()
    else
        self.waiting[#self.waiting + 1] = operation
        self:admit()
    end

    local outcome
    operation.pending = async.promise(function()
        local accepted, value = pcall(function()
            async.race({ admitted, finished, self.finished }):await()

            if self.closed or operation.closed or context ~= nil and context.owner.closed then
                error(protocol.failure("ssh_connection_closed"), 0)
            end

            operation.started = true

            if context ~= nil then
                context.accepted = true
            end

            return self.transport:request(method, declaration.parameters, operation)
        end)

        outcome = accepted and { value = value } or { failure = type(value) == "table" and value or protocol.failure("ssh_transport_failed") }

        if operation.cancelling ~= nil then
            local _, problem = operation.cancelling:await()

            if problem ~= nil then
                outcome = { failure = protocol.failure("ssh_cleanup_failed") }
            end
        end

        if heartbeat then
            self.heartbeat = false
        end

        if operation.admitted and not direct then
            self.active = self.active - 1
        end

        self.count = self.count - 1
        self.bytes = self.bytes - #body
        self.operations[operation] = nil
        self:admit()
    end)

    if context ~= nil then
        context.provider = operation.pending
    end

    local waiting = async.race({ operation.pending, self.finished })
    local _, expired

    if readers[method] then
        waiting:await()
    else
        _, expired = async.timeout(waiting, timeoutMs):await()
    end

    local committing = method == "files.write" and operation.started

    if self.closed and committing then
        operation.pending:await()
    end

    if self.closed and not committing then
        return nil, protocol.failure("ssh_connection_closed")
    end

    if expired ~= nil then
        return nil, protocol.failure("ssh_request_timeout")
    end

    operation.released = true

    return outcome.value, outcome.failure
end

function Requests:ping(timeoutMs)
    return self:request("session.ping", {}, timeoutMs)
end

function Requests:close()
    if self.closing ~= nil then
        return self.closing
    end

    self.closed = true
    self.finish()
    local pending = {}

    for operation in pairs(self.operations) do
        cancel(operation)
        pending[#pending + 1] = operation.pending
    end

    self.closing = async.allSettled(pending)

    return self.closing
end

return requests
