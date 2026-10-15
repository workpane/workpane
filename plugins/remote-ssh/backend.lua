local async = require("async")
local crypto = require("crypto")
local context = include("context")
local launch = include("launch")
local protocol = include("protocol")
local sftpFiles = include("sftp-files")
local shell = include("shell")

local backend = {}

local Backend = {}
Backend.__index = Backend

local rules = {
    path = { kind = "string", maximum = 4096 },
    group = { kind = "string", maximum = 64 },
    scope = { kind = "string", maximum = 16 },
    process = { kind = "number", maximum = 2147483647 },
    limit = { kind = "number", maximum = 65536, default = 65536 },
    listing = { kind = "number", maximum = 2000, default = 2000 },
    prefix = { kind = "number", maximum = 4194304 },
    walking = { kind = "number", maximum = 20000, default = 20000 },
    matches = { kind = "number", maximum = 1000, default = 1000 },
    fileBytes = { kind = "number", maximum = 4194304, default = 4194304 },
    bytes = { kind = "string", maximum = 5592408, empty = true },
    revision = { kind = "string", maximum = 64, empty = true },
    skip = { kind = "table", default = {} },
    arguments = { kind = "table", default = {} },
    input = { kind = "string", maximum = 5592408, empty = true, optional = true },
    recursive = { kind = "boolean", default = false },
    history = { kind = "string", maximum = 32 },
    columns = { kind = "number", maximum = 1024, default = 80 },
    rows = { kind = "number", maximum = 1024, default = 24 },
}

local schemas = {
    ["session.ping"] = {},
    ["terminal.start"] = { directory = rules.path, history = rules.history, columns = rules.columns, rows = rules.rows },
    ["terminal.read"] = { terminal = rules.process, limit = rules.limit },
    ["terminal.write"] = { terminal = rules.process, content = rules.bytes },
    ["terminal.resize"] = { terminal = rules.process, columns = rules.columns, rows = rules.rows },
    ["terminal.close"] = { terminal = rules.process },
    ["terminal.history.close"] = { history = rules.history },
    ["context.info"] = {},
    ["context.stat"] = { scope = rules.scope, path = rules.path },
    ["context.canonical"] = { scope = rules.scope, path = rules.path },
    ["context.list"] = { scope = rules.scope, path = rules.path, maximum = rules.listing },
    ["context.read-prefix"] = { scope = rules.scope, path = rules.path, maximum = rules.prefix },
    ["files.stat"] = { path = rules.path },
    ["files.list"] = { path = rules.path, maximum = rules.listing },
    ["files.canonical"] = { path = rules.path },
    ["files.read"] = { path = rules.path },
    ["files.read-prefix"] = { path = rules.path, maximum = rules.prefix },
    ["files.write"] = { path = rules.path, content = rules.bytes, revision = rules.revision },
    ["files.mkdir"] = { path = rules.path },
    ["files.rename"] = { path = rules.path, destination = rules.path },
    ["files.remove"] = { path = rules.path, recursive = rules.recursive },
    ["files.walk"] = { path = rules.path, maximum = rules.walking, skip = rules.skip },
    ["files.search"] = { path = rules.path, text = rules.path, maximum = rules.matches, maximumFileBytes = rules.fileBytes, skip = rules.skip },
    ["process.group.open"] = { group = rules.group },
    ["process.group.close"] = { group = rules.group },
    ["process.find"] = { program = rules.path, directories = rules.arguments },
    ["process.start"] = { group = rules.group, program = rules.path, directory = rules.path, arguments = rules.arguments, variables = rules.arguments, cleared = rules.arguments, input = rules.input },
    ["process.read"] = { group = rules.group, process = rules.process, limit = rules.limit },
    ["process.write"] = { group = rules.group, process = rules.process, content = rules.bytes },
    ["process.stop"] = { group = rules.group, process = rules.process },
}

local function failed(code)
    error(protocol.failure(code), 0)
end

local function parameters(method, supplied)
    local schema = schemas[method]

    if schema == nil then
        failed("remote_method_unknown")
    end

    if type(supplied) ~= "table" then
        failed("ssh_request_invalid")
    end

    for key in pairs(supplied) do
        if schema[key] == nil then
            failed("ssh_request_invalid")
        end
    end

    local result = {}

    for key, rule in pairs(schema) do
        local value = supplied[key]

        if value == nil then
            value = rule.default
        end

        if value == nil and rule.optional then
            result[key] = nil
        else
            if type(value) ~= rule.kind or rule.kind == "number" and (math.type(value) ~= "integer" or value < 1 or value > rule.maximum) or rule.kind == "string" and (#value > rule.maximum or not rule.empty and #value == 0 or value:find("%z") ~= nil or utf8.len(value) == nil) then
                failed("ssh_request_invalid")
            end

            result[key] = value
        end
    end

    if result.group ~= nil and (#result.group ~= 64 or result.group:match("^[0-9a-f]+$") == nil) then
        failed("remote_process_group_invalid")
    end

    return result
end

local function binary(encoded, maximum)
    if type(encoded) ~= "string" or #encoded > math.ceil(maximum / 3) * 4 then
        failed("remote_input_invalid")
    end

    local accepted, value = pcall(crypto.base64Decode, encoded)

    if not accepted or type(value) ~= "string" or #value > maximum or crypto.base64Encode(value) ~= encoded then
        failed("remote_input_invalid")
    end

    return value
end

function backend.new(owner, client, executable, arguments, host, authentication, root, account)
    local created = setmetatable({ owner = owner, client = client, root = root, account = account, groups = {}, groupPrefix = crypto.hexEncode(crypto.randomBytes(16)), nextGroup = 0, activeGroups = 0, jobs = {}, terminals = {}, terminalPreparing = {}, terminalCount = 0, closed = false }, Backend)
    created.launcher = launch.new(owner, client, executable, arguments, host, authentication)
    local resolve

    if owner.platform == "windows" then
        resolve = function(boundary, path, missing)
            return owner:resolve(boundary, path, missing, 10000)
        end
    end

    created.files = sftpFiles.new(client, owner, root, owner.platform, resolve, function(script, values)
        return created:execute(script, values, 0)
    end)

    created.context = context.new(client, owner, root, account)
    created.shell = shell.new(created.launcher, account, root)

    return created
end

function Backend:execute(script, values, maximum)
    local running = self.launcher:execute(script, values, self.root, self.account.shell)

    return self.launcher:capture(running, maximum)
end

function Backend:find(program, directories)
    if #directories > 256 then
        failed("remote_process_invalid")
    end

    local count = 0
    local paths = workpane.files.paths(self.owner.platform)

    for index, directory in pairs(directories) do
        if math.type(index) ~= "integer" or index < 1 or index > #directories or type(directory) ~= "string" or #directory > 4096 or directory:find("%z") ~= nil or utf8.len(directory) == nil or not paths.absolute(directory) then
            failed("remote_process_invalid")
        end

        count = count + 1
    end

    if count ~= #directories then
        failed("remote_process_invalid")
    end

    return self.owner:find(program, directories, 10000)
end

function Backend:reserveGroup()
    if self.closed then
        failed("ssh_connection_closed")
    end

    if self.activeGroups >= 32 or self.nextGroup >= math.maxinteger then
        failed("remote_process_group_limit")
    end

    self.nextGroup = self.nextGroup + 1
    local identity = self.groupPrefix .. string.format("%032x", self.nextGroup)
    self.groups[identity] = { closed = false, opened = false, jobs = {} }
    self.activeGroups = self.activeGroups + 1

    return identity
end

function Backend:group(identity, opening)
    local group = self.groups[identity]

    if opening then
        if group == nil or group.closed then
            failed("remote_process_group_closed")
        end

        group.opened = true

        return {}
    end

    if group == nil then
        return {}
    end

    if group.closing == nil then
        group.closed = true
        group.closing = async.promise(function()
            local accepted, problem = pcall(self.release, self, group.jobs)
            group.failure = not accepted and problem or nil

            if accepted then
                self.groups[identity] = nil
                self.activeGroups = self.activeGroups - 1
            end
        end)
    end

    group.closing:await()

    if group.failure ~= nil then
        error(group.failure, 0)
    end

    return {}
end

function Backend:release(jobs)
    local releases = {}
    local accepted = {}

    for job in pairs(jobs) do
        if job.identity ~= nil then
            releases[#releases + 1] = async.promise(function()
                self.owner:release(job.identity, 10000)
            end)
        end

        accepted[#accepted + 1] = job
    end

    local outcomes = async.allSettled(releases):await()
    local clean = true

    for _, result in ipairs(outcomes) do
        clean = clean and result.ok
    end

    for _, job in ipairs(accepted) do
        job.opening:await()

        if job.terminal ~= nil then
            clean = pcall(self.shell.close, self.shell, job.terminal) and clean
        elseif job.running ~= nil then
            local outcome, problem = job.running:close():await()
            clean = clean and problem == nil and outcome.failure == nil
        end

        self:retire(job)
        jobs[job] = nil
    end

    if not clean then
        failed("ssh_cleanup_failed")
    end
end

function Backend:start(value, operation)
    local group = self.groups[value.group]

    if group == nil or group.closed or not group.opened then
        failed("remote_process_group_closed")
    end

    local job = { group = group }

    if operation ~= nil then
        operation.job = job
    end

    group.jobs[job] = true
    job.opening = async.promise(function()
        local accepted, problem = pcall(function()
            local declaration = { program = value.program, arguments = value.arguments, directory = self.files:resolve(value.directory, false), variables = value.variables, cleared = value.cleared, input = value.input ~= nil and binary(value.input, 4194304) or nil }

            if not workpane.files.paths(self.owner.platform).absolute(declaration.program) then
                local found = self:find(declaration.program, {})

                if not found.found then
                    failed("remote_program_missing")
                end

                declaration.program = found.path
            end

            job.running = self.launcher:start(declaration, function(identity)
                job.identity = identity
                self.jobs[identity] = job
            end, function()
                return self.closed or group.closed or operation ~= nil and operation.closed
            end)
        end)

        job.failure = not accepted and problem or nil
    end)

    job.opening:await()

    if job.failure ~= nil then
        self:retire(job)
        error(job.failure, 0)
    end

    if group.closed or self.closed then
        failed("remote_process_group_closed")
    end

    return { process = job.identity }
end

function Backend:process(method, value, operation)
    local group = self.groups[value.group]
    local job = self.jobs[value.process]

    if group == nil or group.closed or job == nil or group.jobs[job] ~= true or job.running == nil then
        failed("remote_process_missing")
    end

    if operation ~= nil then
        operation.job = job
    end

    if method == "process.read" then
        local result = job.running:read(value.limit)

        if result.ended then
            local outcome, problem = job.running:close():await()

            if problem ~= nil or outcome.failure ~= nil then
                error(problem or outcome.failure, 0)
            end

            self:retire(job)
        end

        return result
    end

    if method == "process.write" then
        job.running:write(binary(value.content, 65536))

        return {}
    end

    job.running:stop()

    return {}
end

function Backend:retire(job)
    if job.retired then
        return
    end

    job.retired = true

    if job.group ~= nil then
        job.group.jobs[job] = nil
    end

    if job.terminalKind then
        self.terminalPreparing[job] = nil
        self.terminalCount = self.terminalCount - 1
    end

    if job.identity ~= nil then
        self.jobs[job.identity] = nil
        self.terminals[job.identity] = nil
    end
end

function Backend:startTerminal(value, operation)
    if self.terminalCount >= 16 then
        failed("remote_terminal_limit")
    end

    local job = { terminalKind = true }

    if operation ~= nil then
        operation.job = job
    end

    self.terminalCount = self.terminalCount + 1
    self.terminalPreparing[job] = true
    job.opening = async.promise(function()
        local accepted, problem = pcall(function()
            local directory = self.files:resolve(value.directory, false)
            job.terminal = self.shell:start(directory, value.history, value.columns, value.rows, function(identity)
                job.identity = identity
                self.terminals[identity] = job
            end, function()
                return self.closed or operation ~= nil and operation.closed
            end)

            job.running = job.terminal.running
        end)

        job.failure = not accepted and problem or nil
    end)

    job.opening:await()

    if job.failure ~= nil then
        self:retire(job)
        error(job.failure, 0)
    end

    self.terminalPreparing[job] = nil

    if self.closed then
        failed("ssh_connection_closed")
    end

    return { terminal = job.identity }
end

function Backend:terminal(method, value, operation)
    local job = self.terminals[value.terminal]

    if job == nil or job.terminal == nil then
        failed("remote_terminal_missing")
    end

    if operation ~= nil then
        operation.job = job
    end

    if method == "terminal.read" then
        local result = self.shell:read(job.terminal, value.limit)

        if result.ended then
            self.shell:close(job.terminal)
            self:retire(job)
        end

        return result
    end

    if method == "terminal.close" then
        self.shell:close(job.terminal)
        self:retire(job)

        return {}
    end

    if method == "terminal.write" then
        job.running:write(binary(value.content, 65536))
    else
        job.running:resize(value.columns, value.rows)
    end

    return {}
end

function Backend:request(method, supplied, operation)
    local value = parameters(method, supplied)

    if self.closed then
        failed("ssh_connection_closed")
    end

    if method == "session.ping" then
        self.owner:command("ping", nil, 10000)

        return {}
    end

    if method == "terminal.history.close" then
        return self.shell:release(value.history)
    end

    if method == "terminal.start" then
        return self:startTerminal(value, operation)
    end

    if method:sub(1, 9) == "terminal." then
        return self:terminal(method, value, operation)
    end

    if method:sub(1, 8) == "context." then
        return self.context:request(method, value)
    end

    if method == "process.group.open" or method == "process.group.close" then
        return self:group(value.group, method == "process.group.open")
    end

    if method == "process.start" then
        return self:start(value, operation)
    end

    if method == "process.find" then
        return self:find(value.program, value.directories)
    end

    if method:sub(1, 8) == "process." then
        return self:process(method, value, operation)
    end

    if method == "files.stat" then
        return self.files:stat(value.path)
    end

    if method == "files.canonical" then
        return { path = self.files:resolve(value.path, true) }
    end

    if method == "files.list" then
        return self.files:list(value.path, value.maximum)
    end

    if method == "files.read" then
        return self.files:read(value.path)
    end

    if method == "files.read-prefix" then
        return self.files:prefix(value.path, value.maximum)
    end

    if method == "files.write" then
        return self.files:write(value.path, value.content, value.revision)
    end

    if method == "files.mkdir" then
        return self.files:mkdir(value.path)
    end

    if method == "files.rename" then
        return self.files:rename(value.path, value.destination)
    end

    if method == "files.remove" then
        return self.files:remove(value.path, value.recursive)
    end

    if method == "files.walk" then
        return self.files:walk(value.path, value.maximum, value.skip)
    end

    return self.files:search(value.path, value.text, value.maximum, value.maximumFileBytes, value.skip)
end

function Backend:cancel(operation)
    if operation.cancelling ~= nil or operation.job == nil then
        return
    end

    operation.cancelling = async.promise(function()
        local job = operation.job

        if job.identity ~= nil then
            self.owner:release(job.identity, 10000)
        end

        job.opening:await()

        if job.terminal ~= nil then
            self.shell:close(job.terminal)
        elseif job.running ~= nil then
            local value, problem = job.running:close():await()

            if problem ~= nil or value.failure ~= nil then
                error(problem or value.failure, 0)
            end
        end

        self:retire(job)
    end)
end

function Backend:stop()
    if self.closing ~= nil then
        return self.closing
    end

    self.closed = true

    for _, group in pairs(self.groups) do
        group.closed = true
    end

    self.closing = async.promise(function()
        local jobs = {}

        for _, job in pairs(self.jobs) do
            jobs[job] = true
        end

        for _, job in pairs(self.terminals) do
            jobs[job] = true
        end

        for job in pairs(self.terminalPreparing) do
            jobs[job] = true
        end

        for _, group in pairs(self.groups) do
            for job in pairs(group.jobs) do
                jobs[job] = true
            end
        end

        self:release(jobs)
        self.groups = {}
        self.activeGroups = 0
    end)

    return self.closing
end

return backend
