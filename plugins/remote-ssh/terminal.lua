local async = require("async")
local crypto = require("crypto")
local protocol = include("protocol")

local terminal = {}

local Terminal = {}
Terminal.__index = Terminal

local function cancel(operation)
    if operation.closed then
        return
    end

    operation.closed = true
    operation.input = {}
    operation.bytes = 0
    operation.finish()
    operation.wake()

    if operation.id ~= nil and not operation.owner.session.closed then
        operation.cancelling = async.promise(function()
            pcall(operation.owner.request, operation.owner, "terminal.close", { terminal = operation.id })
        end)
    end
end

local function decoded(content, maximum)
    if type(content) ~= "string" or #content > math.ceil(maximum / 3) * 4 then
        return nil
    end

    local accepted, bytes = pcall(crypto.base64Decode, content)

    if not accepted or type(bytes) ~= "string" or #bytes > maximum or crypto.base64Encode(bytes) ~= content then
        return nil
    end

    return bytes
end

local function response(value)
    if type(value) ~= "table" then
        return false
    end

    for key in pairs(value) do
        if key ~= "content" and key ~= "ended" and key ~= "code" and key ~= "directory" then
            return false
        end
    end

    return decoded(value.content, 65536) ~= nil and type(value.ended) == "boolean" and math.type(value.code) == "integer" and value.code >= -2147483648 and value.code <= 2147483647 and type(value.directory) == "string" and #value.directory <= 4096 and value.directory:find("%z") == nil and utf8.len(value.directory) ~= nil
end

function terminal.new(session, onFailure, onLoading)
    local created = setmetatable({ session = session, failed = onFailure, loading = onLoading or function() end, operations = {}, closed = false, history = crypto.hexEncode(crypto.randomBytes(16)) }, Terminal)
    created.node = workpane.ui.terminal({ transport = "stream", grow = 1, height = 0 })
    created.node:on("open-stream", function(event)
        created:open(event)
    end)

    created.node:on("close-stream", function(event)
        local operation = created.current

        if operation ~= nil and operation.generation == event.generation then
            cancel(operation)
        end
    end)

    created.node:on("input-stream", function(event)
        created:input(event)
    end)

    created.node:on("resize-stream", function(event)
        local operation = created.current

        if operation ~= nil and not operation.closed and operation.generation == event.generation then
            operation.size = { columns = event.columns, rows = event.rows }
            operation.wake()
        end
    end)

    return created
end

function Terminal:request(method, parameters)
    local value, failure = self.session:request(method, parameters, 10000)

    if failure ~= nil then
        error(failure, 0)
    end

    return value
end

function Terminal:command(operation, name, arguments)
    arguments.generation = operation.generation

    while not operation.closed and not self.closed do
        local accepted, failure = pcall(operation.node.command, operation.node, name, arguments)

        if accepted then
            return true
        end

        if type(failure) ~= "table" or failure.code ~= "terminal_stream_full" then
            error(failure, 0)
        end

        async.timeout(operation.finished, 10):await()
    end

    return false
end

function Terminal:output(operation)
    while not operation.closed do
        local value = self:request("terminal.read", { terminal = operation.id, limit = 65536 })

        if operation.closed then
            return
        end

        if not response(value) then
            error(protocol.failure("ssh_terminal_response_invalid"), 0)
        end

        if value.content ~= "" or value.directory ~= operation.directory then
            if not self:command(operation, "feed", { content = value.content, directory = value.directory }) then
                return
            end

            operation.directory = value.directory

            if not operation.ready and value.content ~= "" then
                operation.ready = true
                self.loading(false)
            end
        end

        if value.ended then
            operation.id = nil
            self.loading(false)
            self:command(operation, "end-stream", { code = value.code })
            cancel(operation)
            return
        end
    end
end

function Terminal:write(operation)
    while not operation.closed do
        local waiting, wake = async.deferred()
        operation.wake = wake
        local size = operation.size
        operation.size = nil

        if size ~= nil then
            self:request("terminal.resize", { terminal = operation.id, columns = size.columns, rows = size.rows })
        end

        local bytes = table.remove(operation.input, 1)

        if bytes ~= nil then
            for start = 1, #bytes, 65536 do
                if operation.closed then
                    return
                end

                local part = bytes:sub(start, start + 65535)
                self:request("terminal.write", { terminal = operation.id, content = crypto.base64Encode(part) })

                if operation.closed then
                    return
                end

                self:command(operation, "acknowledge", { bytes = #part })
                operation.bytes = operation.bytes - #part
            end
        elseif operation.size == nil then
            async.race({ waiting, operation.finished }):await()
        end
    end
end

function Terminal:input(event)
    local operation = self.current

    if operation == nil or operation.closed or operation.generation ~= event.generation then
        return
    end

    local bytes = decoded(event.content, 256 * 1024)

    if bytes == nil or #bytes > 256 * 1024 - operation.bytes or #operation.input >= 4096 then
        self:fail(operation)
        return
    end

    if #bytes == 0 then
        return
    end

    operation.input[#operation.input + 1] = bytes
    operation.bytes = operation.bytes + #bytes
    operation.wake()
end

function Terminal:fail(operation)
    if operation.closed then
        return
    end

    cancel(operation)

    if not self.closed and not self.session.closed and self.current == operation then
        self.loading(false)
        pcall(operation.node.command, operation.node, "end-stream", { generation = operation.generation, code = 1 })
        self.failed(protocol.failure("ssh_terminal_failed"))
    end
end

function Terminal:open(event)
    if self.closed or self.session.closed then
        return
    end

    local previous = self.current

    if previous ~= nil then
        if not previous.closed and previous.generation == event.generation then
            return
        end

        cancel(previous)
    end

    local finished, finish = async.deferred()
    local operation = { owner = self, generation = event.generation, node = self.node, closed = false, input = {}, bytes = 0, finished = finished, finish = finish, wake = function() end }
    self.current = operation
    self.loading(true)
    self.operations[operation] = true
    operation.pending = async.promise(function()
        local accepted = pcall(function()
            if previous ~= nil then
                previous.pending:await()
            end

            if operation.closed then
                return
            end

            local value = self:request("terminal.start", { directory = ".", history = self.history, columns = event.columns, rows = event.rows })

            if type(value) ~= "table" or math.type(value.terminal) ~= "integer" or value.terminal < 1 or value.terminal > 2147483647 then
                error(protocol.failure("ssh_terminal_response_invalid"), 0)
            end

            for key in pairs(value) do
                if key ~= "terminal" then
                    error(protocol.failure("ssh_terminal_response_invalid"), 0)
                end
            end

            operation.id = value.terminal

            if operation.closed then
                return
            end

            local function worker(handler)
                return async.promise(function()
                    if not pcall(handler, self, operation) then
                        self:fail(operation)
                    end
                end)
            end

            async.allSettled({ worker(self.output), worker(self.write) }):await()
        end)

        if not accepted then
            self:fail(operation)
        end

        cancel(operation)

        if operation.cancelling ~= nil then
            operation.cancelling:await()
        elseif operation.id ~= nil and not self.session.closed then
            pcall(self.request, self, "terminal.close", { terminal = operation.id })
        end

        operation.id = nil
        self.operations[operation] = nil
    end)
end

function Terminal:close()
    if self.closing ~= nil then
        return self.closing
    end

    self.closed = true
    local pending = {}

    for operation in pairs(self.operations) do
        cancel(operation)
        pending[#pending + 1] = operation.pending
    end

    self.closing = async.promise(function()
        async.allSettled(pending):await()

        if not self.session.closed then
            self:request("terminal.history.close", { history = self.history })
        end

        self.session.terminals[self] = nil
    end)

    return self.closing
end

return terminal
