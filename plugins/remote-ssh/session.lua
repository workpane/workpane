local async = require("async")
local crypto = require("crypto")
local socket = require("socket")
local backend = include("backend")
local authentication = include("authentication")
local credentials = include("credentials")
local discovery = include("discovery")
local forward = include("forward")
local profiles = include("profiles")
local program = include("program")
local protocol = include("protocol")
local requests = include("requests")
local scope = include("scope")
local sftp = include("sftp")
local sftpFiles = include("sftp-files")
local source = include("source")
local terminal = include("terminal")

local session = {}

local Session = {}
Session.__index = Session

local function raised(code)
    error(protocol.failure(code), 0)
end

local function failure(problem)
    local code = type(problem) == "table" and problem.code or nil

    if type(code) ~= "string" or #code > 128 or code:match("^ssh_[a-z_]+$") == nil then
        code = "ssh_connection_failed"
    end

    return protocol.failure(code)
end

function session.new(profile, onChange)
    local accepted, problem = profiles.validate(profile)

    if accepted == nil then
        return nil, protocol.failure(problem)
    end

    local finished, finish = async.deferred()

    return setmetatable({ id = crypto.uuidV4(), profile = accepted, changed = onChange, state = "preparing", closed = false, terminals = {}, forwards = {}, finished = finished, finish = finish, nonce = crypto.hexEncode(crypto.randomBytes(16)) }, Session)
end

function Session:stateChanged(state)
    self.state = state
    self.changed()
end

function Session:check()
    if self.closed then
        raised("ssh_connection_closed")
    end
end

function Session:await(future)
    local value, problem = future:await()
    self:check()

    if problem ~= nil then
        error(failure(problem), 0)
    end

    return value
end

function Session:connect()
    local executable = self:await(workpane.process.find("ssh"))

    if executable == nil then
        raised("ssh_client_unavailable")
    end

    local savedPassword = self.suppliedPassword
    self.suppliedPassword = nil
    local credentialFailure

    if savedPassword == nil and self.profile.savePassword then
        savedPassword, credentialFailure = workpane.credentials.read(self.credentialName or credentials.name(self.profile)):await()
    end

    self:check()
    self.authentication = authentication.new(function()
        self:close()
    end, savedPassword, credentialFailure ~= nil)
    savedPassword = nil
    local arguments, invalid = profiles.arguments(self.profile)

    if arguments == nil then
        raised(invalid)
    end

    self:stateChanged("detecting")
    local detecting = discovery.start(executable, arguments, self.profile.host, self.authentication, self.nonce)
    self.discovery = detecting
    local platform = detecting:read(self.profile.timeoutSeconds * 1000, self)
    detecting:close()
    self.discovery = nil
    self:check()
    local controller

    if platform == "windows" then
        controller = source.prepare(platform, source.asset("ssh-jobs-windows.ps1") .. source.asset("ssh-paths-windows.ps1") .. source.asset("ssh-events-windows.ps1") .. source.asset("ssh-scope-windows.ps1"), { self.nonce, "60000" })
    else
        controller = source.prepare("linux", source.asset("ssh-scope-posix.sh"), { "60", source.asset("ssh-session-macos.js") })
    end

    self:check()
    local listener, problem = socket.tcp.listen("127.0.0.1", 0):await()

    if problem ~= nil then
        raised("ssh_transport_failed")
    end

    local proxyPort = listener.port
    listener:close():await()
    self:check()
    local control = { table.unpack(arguments) }
    control[#control + 1] = "-D"
    control[#control + 1] = "127.0.0.1:" .. tostring(proxyPort)
    control[#control + 1] = self.profile.host
    control[#control + 1] = controller.command
    self:stateChanged("connecting")
    self.connection = program.new(executable, control, self.authentication:channel().variables, nil, function()
        if not self.closed and self.state == "connected" then
            self.problem = protocol.failure("ssh_connection_lost")
            self:close()
        end
    end)

    if controller.input ~= "" then
        self.connection:write(controller.input)
    end

    if platform == "posix" then
        local detected = self.connection:line("PLATFORM ", self.profile.timeoutSeconds * 1000, self)
        platform = detected:match("^PLATFORM (.+)$")

        if platform ~= "linux" and platform ~= "macos" then
            raised("ssh_platform_unavailable")
        end
    end

    local root = workpane.files.paths(platform).normalizeRoot(self.profile.root)
    local announced = self.connection:line("SCOPE ", self.profile.timeoutSeconds * 1000, self)
    local directory, owner = announced:match("^SCOPE (.+) ([0-9]+)$")
    self.scope = scope.new(self.connection, platform, directory, tonumber(owner))

    if root ~= "" and not profiles.absolute(platform, root) then
        raised("ssh_profile_root_invalid")
    end

    self:stateChanged("transferring")
    local account = self.scope:account(10000)
    self:check()
    local transfer = { table.unpack(arguments) }
    transfer[#transfer + 1] = "-s"
    transfer[#transfer + 1] = self.profile.host
    transfer[#transfer + 1] = "sftp"
    self.client = sftp.new({ program = executable, arguments = transfer, directory = workpane.app.dataDirectory, variables = self.authentication:channel().variables, onExit = function()
        if not self.closed and self.state == "connected" then
            self.problem = protocol.failure("ssh_connection_lost")
            self:close()
        end
    end })

    local accepted, unavailable = self.client:start(self.profile.timeoutSeconds * 1000)

    if not accepted then
        error(unavailable, 0)
    end

    self:check()
    root = root ~= "" and root or account.home

    if self.browsing then
        root = platform == "windows" and (account.home:match("^[A-Za-z]:/") or account.home:match("^//[^/]+/[^/]+")) or "/"
    end

    if platform == "windows" then
        root = self.scope:resolve(root, root, false, 10000)
    else
        root = sftpFiles.new(self.client, self.scope, "/", platform):realpath(root, false)
    end

    self:check()
    self.backend = backend.new(self.scope, self.client, executable, arguments, self.profile.host, self.authentication, root, account)
    self.requests = requests.new(self.backend)
    local inspected, inaccessible = self.requests:request("files.stat", { path = "." }, 10000)
    self:check()

    if inaccessible ~= nil or inspected.kind ~= "directory" then
        raised("ssh_workspace_invalid")
    end

    self.identity = { root = root, platform = platform }
    self.channel = { port = proxyPort }
    self:stateChanged("connected")
    workpane.task(function()
        while not self.closed do
            async.timeout(self.finished, 20000):await()

            if self.closed then
                return
            end

            local _, failure = self.requests:ping(10000)

            if failure ~= nil then
                self.problem = protocol.failure("ssh_connection_lost")
                self:close()
                return
            end
        end
    end)
end

function Session:reserveGroup()
    if self.closed or self.state ~= "connected" then
        error(protocol.failure("ssh_connection_closed"), 0)
    end

    return self.backend:reserveGroup()
end

function Session:request(method, parameters, timeoutMs, context)
    if self.closed or self.state ~= "connected" then
        return nil, protocol.failure("ssh_connection_closed")
    end

    return self.requests:request(method, parameters, timeoutMs, context)
end

function Session:openTerminal(onFailure, onLoading)
    if self.closed or self.state ~= "connected" then
        return nil, protocol.failure("ssh_connection_closed")
    end

    local count = 0

    for _ in pairs(self.terminals) do
        count = count + 1
    end

    if count >= 16 then
        return nil, protocol.failure("ssh_terminal_limit")
    end

    local created = terminal.new(self, onFailure, onLoading)
    self.terminals[created] = true

    return created
end

function Session:start()
    if self.starting ~= nil or self.closed then
        return nil, protocol.failure("ssh_connection_closed")
    end

    self.starting = async.promise(function()
        local accepted, problem = pcall(self.connect, self)

        if not accepted and not self.closed then
            self.problem = failure(problem)
            self:close():await()
        end

        return accepted and { value = true } or { failure = self.problem or protocol.failure("ssh_connection_closed") }
    end)

    return self.starting
end

function Session:openForward(remotePort, localPort)
    local count = 0

    for _ in pairs(self.forwards) do
        count = count + 1
    end

    if count >= 16 then
        return nil, protocol.failure("ssh_forward_limit")
    end

    local created, problem = forward.new(self, remotePort, localPort, self.changed)

    if created == nil then
        return nil, problem
    end

    self.forwards[created.id] = created

    local outcome = workpane.await(created:start())

    if outcome.failure ~= nil then
        self.forwards[created.id] = nil
        return nil, outcome.failure
    end

    return outcome.value
end

function Session:closeForward(id)
    local current = self.forwards[id]

    if current == nil then
        raised("ssh_forward_unavailable")
    end

    workpane.await(current:close())
    self.forwards[id] = nil
    self.changed()

    return {}
end

function Session:close()
    if self.closing ~= nil then
        return self.closing
    end

    self.closed = true
    self.suppliedPassword = nil
    self.finish()
    self:stateChanged("closing")
    local terminalClosures = {}
    local forwardClosures = {}

    for current in pairs(self.terminals) do
        terminalClosures[#terminalClosures + 1] = current:close()
    end

    for _, current in pairs(self.forwards) do
        forwardClosures[#forwardClosures + 1] = current:close()
    end

    local programs = self.backend ~= nil and self.backend:stop() or nil
    local operations = self.requests ~= nil and self.requests:close() or nil
    self.closing = async.promise(function()
        local clean = true

        local function release(resource, operation)
            if resource == nil then
                return
            end

            local accepted, value, problem = pcall(operation, resource)
            clean = clean and accepted and problem == nil and (type(value) ~= "table" or value.failure == nil)
        end

        release(self.discovery, function(resource)
            resource:close()
        end)

        self.discovery = nil

        if operations ~= nil then
            local _, expired = async.timeout(operations, 30000):await()

            if expired ~= nil then
                clean = false
                self.scope:stop()
                self.client:abort("ssh_connection_closed")
            end

            operations:await()
        end

        release(programs, function(future)
            return future:await()
        end)

        async.allSettled(terminalClosures):await()
        local outcomes = async.allSettled(forwardClosures):await()

        for _, result in ipairs(outcomes) do
            clean = clean and result.ok and (type(result.value) ~= "table" or result.value.failure == nil)
        end

        release(self.client, function(resource)
            return resource:close()
        end)

        release(self.scope, function(resource)
            return resource:close():await()
        end)

        if self.scope == nil then
            release(self.connection, function(resource)
                resource:close()
            end)
        end

        release(self.authentication, function(resource)
            return resource:close():await()
        end)

        self.forwards = {}
        self.channel = nil
        self.authentication = nil

        if not clean then
            self.problem = protocol.failure("ssh_cleanup_failed")
        end

        self:stateChanged(self.problem ~= nil and "failed" or "disconnected")

        return { failure = not clean and self.problem or nil }
    end)

    return self.closing
end

return session
