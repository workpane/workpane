local async = require("async")
local crypto = require("crypto")
local json = require("json")
local protocol = include("protocol")

local scope = {}

local expectedErrors = { remote_path_unavailable = true, remote_path_inspection_failed = true, remote_path_outside_workspace = true, remote_file_write_failed = true }

local Scope = {}
Scope.__index = Scope

local function failed(code)
    error(protocol.failure(code), 0)
end

local function release(operation)
    operation.closed = true

    if operation.accepted and not operation.released then
        operation.owner:stop()
    end
end

local operationGuard = { __close = release }

function scope.new(connection, platform, path, owner)
    if type(path) ~= "string" or #path > 4096 or path:find("[%z\r\n]") ~= nil or utf8.len(path) == nil or math.type(owner) ~= "integer" or owner < 2 or owner > 2147483647 or platform ~= "windows" and platform ~= "macos" and platform ~= "linux" then
        failed("ssh_scope_invalid")
    end

    local finished, finish = async.deferred()
    local ready, resolve = async.deferred()
    resolve()

    local created = setmetatable({ connection = connection, platform = platform, path = path, owner = owner, nextIdentity = 0, nextMetadata = 0, active = {}, count = 0, waiting = 0, closed = false, finished = finished, finish = finish, pending = ready, admitted = {} }, Scope)
    created.dispatch = async.promise(function()
        local accepted, problem = pcall(function()
            while not created.closed do
                created:receive(connection:line("", 120000, created))
            end
        end)

        if not accepted then
            if not created.closed then
                created.failure = type(problem) == "table" and problem or protocol.failure("ssh_scope_response_invalid")
            end

            created:stop()
        end
    end)

    return created
end

function Scope:receive(line)
    local identity, code, unsuccessful = line:match("^DONE ([0-9]+) (-?[0-9]+) ([01])$")

    if identity ~= nil then
        identity = tonumber(identity)
        code = tonumber(code)
        local minimum = self.platform == "windows" and -2147483648 or 0
        local maximum = self.platform == "windows" and 2147483647 or 255

        if math.type(identity) ~= "integer" or identity < 1 or identity > self.nextIdentity or math.type(code) ~= "integer" or code < minimum or code > maximum or (code ~= 0) ~= (unsuccessful == "1") then
            failed("ssh_scope_response_invalid")
        end

        local operation = self.active[identity]

        if operation == nil then
            return
        end

        if operation.result ~= nil then
            failed("ssh_scope_response_invalid")
        end

        operation.result = { code = code, failed = unsuccessful == "1" }
        operation.resolve()
        return
    end

    local operation = self.current

    if operation == nil or operation.line ~= nil then
        failed("ssh_scope_response_invalid")
    end

    operation.line = line
    operation.reply()
end

function Scope:command(method, identity, timeoutMs, parameters)
    if math.type(timeoutMs) ~= "integer" or timeoutMs < 1 or timeoutMs > 120000 then
        failed("ssh_deadline_invalid")
    end

    if self.closed then
        failed("ssh_connection_closed")
    end

    if self.waiting >= 64 then
        failed("ssh_channel_full")
    end

    local expected = { find = "FOUND ", account = "ACCOUNT", ping = "PONG", open = "OPEN ", stop = "STOPPED ", info = "INFO ", resolve = "RESOLVED ", stage = "STAGED ", unstage = "UNSTAGED " }

    if expected[method] == nil or method ~= "ping" and method ~= "account" and (math.type(identity) ~= "integer" or identity < 1 or identity > 2147483647) then
        failed("ssh_scope_invalid")
    end

    local previous = self.pending
    local operation <close> = setmetatable({ owner = self }, operationGuard)
    local result
    local outcome
    self.waiting = self.waiting + 1
    local future = async.promise(function()
        local accepted, value = pcall(function()
            previous:await()

            if self.closed or operation.closed then
                failed("ssh_connection_closed")
            end

            operation.accepted = true
            local waiting, reply = async.deferred()
            operation.reply = reply
            self.current = operation
            self.connection:write(method .. (identity ~= nil and " " .. tostring(identity) or "") .. (parameters ~= nil and " " .. parameters or "") .. "\n")
            async.race({ waiting, self.finished }):await()
            self.current = nil

            if self.closed then
                error(self.failure or protocol.failure("ssh_connection_closed"), 0)
            end

            local line = operation.line

            if method == "open" and line == "REFUSED " .. tostring(identity) then
                failed("ssh_program_limit")
            end

            local wanted = expected[method] .. (identity ~= nil and tostring(identity) or "")

            if method == "find" and line == "ABSENT " .. tostring(identity) then
                return false
            end

            local code = line:match("^ERROR " .. tostring(identity) .. " ([a-z_]+)$")

            if expectedErrors[code] and (method == "info" or method == "resolve" or method == "stage") then
                failed(code)
            end

            if method == "info" or method == "resolve" or method == "account" or method == "find" then
                if line:sub(1, #wanted + 1) ~= wanted .. " " then
                    failed("ssh_scope_response_invalid")
                end

                return line:sub(#wanted + 2)
            end

            if line ~= wanted then
                failed("ssh_scope_response_invalid")
            end

            return true
        end)

        if accepted then
            result = value
        else
            outcome = value
        end

        self.waiting = self.waiting - 1
    end)

    self.pending = future
    self.admitted[future] = true
    local _, expired = async.timeout(async.race({ future, self.finished }), timeoutMs):await()

    if expired ~= nil or self.closed then
        self:stop()
        error(self.failure or protocol.failure(expired ~= nil and "ssh_request_timeout" or "ssh_connection_closed"), 0)
    end

    self.admitted[future] = nil
    operation.released = true

    if outcome ~= nil then
        if type(outcome) ~= "table" or outcome.code ~= "ssh_program_limit" and not expectedErrors[outcome.code] then
            self:stop()
        end

        error(outcome, 0)
    end

    return result
end

function Scope:find(program, directories, timeoutMs)
    local parameters = { crypto.base64Encode(program) }

    for _, directory in ipairs(directories) do
        parameters[#parameters + 1] = crypto.base64Encode(directory)
    end

    local encoded = table.concat(parameters, " ")

    if #encoded > 16300 or self.nextMetadata >= 2147483647 then
        failed("remote_process_invalid")
    end

    self.nextMetadata = self.nextMetadata + 1
    local response = self:command("find", self.nextMetadata, timeoutMs, encoded)

    if response == false then
        return { found = false, path = "" }
    end

    local accepted, value = pcall(crypto.base64Decode, response)

    if not accepted or type(value) ~= "string" or #value == 0 or #value > 4096 or value:find("%z") ~= nil or utf8.len(value) == nil or crypto.base64Encode(value) ~= response or not workpane.files.paths(self.platform).absolute(value) then
        self:stop()
        failed("ssh_scope_response_invalid")
    end

    return { found = true, path = value }
end

function Scope:account(timeoutMs)
    local response = self:command("account", nil, timeoutMs)
    local account, home, shell = response:match("^([^ ]+) ([^ ]+) ([^ ]+)$")
    local values = {}

    for index, encoded in ipairs({ account or false, home or false, shell or false }) do
        local accepted, value = pcall(crypto.base64Decode, encoded)

        if not accepted or type(value) ~= "string" or #value == 0 or #value > 4096 or value:find("%z") ~= nil or utf8.len(value) == nil or crypto.base64Encode(value) ~= encoded then
            self:stop()
            failed("ssh_scope_response_invalid")
        end

        values[index] = value
    end

    local paths = workpane.files.paths(self.platform)

    if not paths.absolute(values[2]) or not paths.absolute(values[3]) then
        self:stop()
        failed("ssh_scope_response_invalid")
    end

    return { account = values[1], home = values[2]:gsub("\\", self.platform == "windows" and "/" or "\\"), shell = values[3]:gsub("\\", self.platform == "windows" and "/" or "\\") }
end

function Scope:resolve(root, path, missing, timeoutMs)
    if self.platform ~= "windows" or type(root) ~= "string" or type(path) ~= "string" or #root > 4096 or #path > 4096 or root:find("%z") ~= nil or path:find("%z") ~= nil or utf8.len(root) == nil or utf8.len(path) == nil or type(missing) ~= "boolean" or self.nextMetadata >= 2147483647 then
        failed("remote_path_invalid")
    end

    self.nextMetadata = self.nextMetadata + 1
    local encoded = crypto.base64Encode(json.encode({ root = root, path = path, missing = missing }))
    local response = self:command("resolve", self.nextMetadata, timeoutMs, encoded)
    local accepted, value = pcall(crypto.base64Decode, response)

    if not accepted or type(value) ~= "string" or #value == 0 or #value > 4096 or value:find("%z") ~= nil or utf8.len(value) == nil or crypto.base64Encode(value) ~= response then
        self:stop()
        failed("ssh_scope_response_invalid")
    end

    return value
end

function Scope:metadata(path, timeoutMs)
    if type(path) ~= "string" or #path == 0 or #path > 4096 or path:find("%z") ~= nil or utf8.len(path) == nil or self.nextMetadata == 2147483647 then
        failed("remote_path_invalid")
    end

    self.nextMetadata = self.nextMetadata + 1
    local value = self:command("info", self.nextMetadata, timeoutMs, crypto.base64Encode(path))
    local kind, size, stamp, recent, readable, writable = value:match("^(%a+) (%d+) ([^%s]+) ([01]) ([01]) ([01])$")
    size = tonumber(size)

    if kind ~= "file" and kind ~= "directory" and kind ~= "other" or math.type(size) ~= "integer" or size < 0 or type(stamp) ~= "string" or #stamp > 64 then
        self:stop()
        failed("ssh_scope_response_invalid")
    end

    return { kind = kind, size = size, modified = stamp, recent = recent == "1", readable = readable == "1", writable = writable == "1" }
end

function Scope:stage(parent, timeoutMs)
    if self.closed or type(parent) ~= "string" or #parent == 0 or parent:find("%z") ~= nil or utf8.len(parent) == nil or self.nextMetadata >= 2147483647 then
        failed("remote_path_invalid")
    end

    self.nextMetadata = self.nextMetadata + 1
    local identity = self.nextMetadata
    local path = parent .. "/.workpane-stage-" .. crypto.hexEncode(crypto.randomBytes(16))

    if #path > 4096 then
        failed("remote_path_invalid")
    end

    self:command("stage", identity, timeoutMs, crypto.base64Encode(path))

    return { identity = identity, path = path }
end

function Scope:unstage(stage, timeoutMs)
    self:command("unstage", stage.identity, timeoutMs)
end

function Scope:open(timeoutMs)
    if self.count >= 32 or self.nextIdentity == 2147483647 then
        failed("ssh_program_limit")
    end

    self.nextIdentity = self.nextIdentity + 1
    local identity = self.nextIdentity
    local completed, resolve = async.deferred()
    self.active[identity] = { completed = completed, resolve = resolve }
    self.count = self.count + 1
    local accepted, result = pcall(self.command, self, "open", identity, timeoutMs)

    if not accepted then
        if self.active[identity] ~= nil then
            self.active[identity] = nil
            self.count = self.count - 1
        end

        error(result, 0)
    end

    return identity
end

function Scope:wait(identity)
    local operation = self.active[identity]

    if operation == nil then
        failed("ssh_program_request_invalid")
    end

    async.race({ operation.completed, self.finished }):await()

    if self.closed or operation.result == nil then
        failed("ssh_connection_closed")
    end

    return operation.result
end

function Scope:release(identity, timeoutMs)
    local operation = self.active[identity]

    if operation == nil then
        return
    end

    if operation.closing == nil then
        operation.closing = async.promise(function()
            local accepted, problem = pcall(self.command, self, "stop", identity, timeoutMs)
            operation.failure = not accepted and problem or nil

            if self.active[identity] == operation then
                self.active[identity] = nil
                self.count = self.count - 1
            end

            operation.resolve()
        end)
    end

    operation.closing:await()

    if operation.failure ~= nil then
        error(operation.failure, 0)
    end
end

function Scope:stop()
    if self.closed then
        return
    end

    self.closed = true
    self.finish()
    self.connection:stop()
end

function Scope:close()
    if self.closing ~= nil then
        return self.closing
    end

    local graceful = not self.closed
    self.closed = true
    self.finish()
    self.closing = async.promise(function()
        local clean = graceful and pcall(self.connection.write, self.connection, "quit\n")

        if clean then
            local _, expired = async.timeout(self.connection.finished, 3000):await()
            clean = expired == nil and self.connection.code == 0 and not self.connection.failed
        end

        self.connection:close()
        local waiting = {}

        for pending in pairs(self.admitted) do
            waiting[#waiting + 1] = pending
        end

        async.allSettled(waiting):await()
        self.dispatch:await()
        self.admitted = {}
        self.active = {}
        self.count = 0

        if not clean then
            return { failure = protocol.failure("ssh_cleanup_failed") }
        end

        return { value = true }
    end)

    return self.closing
end

return scope
