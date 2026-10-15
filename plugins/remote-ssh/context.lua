local async = require("async")
local protocol = include("protocol")
local sftpFiles = include("sftp-files")

local context = {}

local Context = {}
Context.__index = Context

local function failed(code)
    error(protocol.failure(code), 0)
end

local function parent(path, platform)
    local result = path:match("^(.*)/[^/]+$")

    if result == "" then
        return "/"
    end

    if platform == "windows" and result ~= nil and result:match("^[A-Za-z]:$") ~= nil then
        return result .. "/"
    end

    if result == nil or platform == "windows" and path:match("^//[^/]+/[^/]+/?$") ~= nil then
        return path
    end

    return result
end

function context.new(client, owner, root, account)
    local resolve

    if owner.platform == "windows" then
        resolve = function(boundary, path, missing)
            return owner:resolve(boundary, path, missing, 10000)
        end
    end

    return setmetatable({ client = client, owner = owner, root = root, account = account, resolve = resolve }, Context)
end

function Context:prepare()
    local platform = self.owner.platform
    local home

    if platform == "windows" then
        home = self.owner:resolve(self.account.home, self.account.home, false, 10000)
    else
        home = sftpFiles.new(self.client, self.owner, "/", platform):realpath(self.account.home, false)
    end

    if self.owner:metadata(home, 10000).kind ~= "directory" then
        failed("remote_context_unavailable")
    end

    local folder = self.root
    local repository = self.root

    for _ = 1, 256 do
        local accepted, value = pcall(self.owner.metadata, self.owner, folder .. (folder:sub(-1) == "/" and "" or "/") .. ".git", 10000)

        if accepted and (value.kind == "file" or value.kind == "directory") then
            repository = folder
            break
        end

        if not accepted and (type(value) ~= "table" or value.code ~= "remote_path_unavailable") then
            error(value, 0)
        end

        local nextFolder = parent(folder, platform)

        if nextFolder == folder then
            break
        end

        folder = nextFolder
    end

    self.information = { account = self.account.account, home = home, repository = repository, root = self.root }
    self.files = { home = sftpFiles.new(self.client, self.owner, home, platform, self.resolve), repository = sftpFiles.new(self.client, self.owner, repository, platform, self.resolve) }
end

function Context:request(method, parameters)
    if self.preparing == nil then
        self.preparing = async.promise(function()
            local accepted, problem = pcall(self.prepare, self)
            self.failure = not accepted and problem or nil
        end)
    end

    self.preparing:await()

    if self.failure ~= nil then
        error(self.failure, 0)
    end

    if method == "context.info" then
        return { account = self.information.account, home = self.information.home, repository = self.information.repository, root = self.root }
    end

    local files = self.files[parameters.scope]

    if files == nil then
        failed("remote_context_scope_invalid")
    end

    if method == "context.stat" then
        return files:stat(parameters.path)
    end

    if method == "context.canonical" then
        return { path = files:resolve(parameters.path, true) }
    end

    if method == "context.list" then
        return files:list(parameters.path, parameters.maximum)
    end

    if method == "context.read-prefix" then
        return files:prefix(parameters.path, parameters.maximum)
    end

    failed("remote_method_unknown")
end

return context
