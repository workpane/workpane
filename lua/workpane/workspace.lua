local crypto = require("crypto")
local workspaceProcess = require("workpane.workspace-process")
local bridge = require("workpane.bridge")
local lifecycle = require("workpane.lifecycle")
local plain = require("workpane.plain")
local task = require("workpane.task")

local workspace = {}

local maximumFileBytes = 4 * 1024 * 1024

local function failure(code)
    return { code = code, message = "The remote workspace operation could not be completed", detail = "" }
end

local function copied(value, depth, seen)
    local kind = type(value)

    if kind == "string" then
        if value:find("%z") ~= nil or utf8.len(value) == nil then
            error(failure("workspace_request_invalid"), 0)
        end

        return value
    end

    if kind == "number" then
        if value ~= value or value == math.huge or value == -math.huge then
            error(failure("workspace_request_invalid"), 0)
        end

        return value
    end

    if kind == "nil" or kind == "boolean" then
        return value
    end

    if depth >= 28 or not plain.table(value) or seen[value] then
        error(failure("workspace_request_invalid"), 0)
    end

    seen[value] = true
    local result = {}

    for key, entry in pairs(value) do
        if type(key) ~= "string" and type(key) ~= "number" then
            error(failure("workspace_request_invalid"), 0)
        end

        result[copied(key, depth + 1, seen)] = copied(entry, depth + 1, seen)
    end

    seen[value] = nil

    return result
end

local function text(value, maximum, empty)
    return type(value) == "string" and #value <= maximum and (empty or #value > 0) and value:find("%z") == nil and utf8.len(value) ~= nil
end

local function token(value)
    return type(value) == "string" and #value == 64 and value:match("^[0-9a-f]+$") ~= nil
end

local function shape(value, fields)
    if type(value) ~= "table" then
        error(failure("workspace_response_invalid"), 0)
    end

    for key in pairs(value) do
        if fields[key] == nil then
            error(failure("workspace_response_invalid"), 0)
        end
    end

    for key, accepts in pairs(fields) do
        if not accepts(value[key]) then
            error(failure("workspace_response_invalid"), 0)
        end
    end

    return value
end

local function boolean(value)
    return type(value) == "boolean"
end

local function path(value)
    return text(value, 4096)
end

local function optionalPath(value)
    return text(value, 4096, true)
end

local function lineNumber(value)
    return math.type(value) == "integer" and value >= 1
end

local function searchText(value)
    return text(value, maximumFileBytes, true)
end

local function list(value, maximum, accepts)
    if type(value) ~= "table" or #value > maximum then
        return false
    end

    local count = 0

    for key, entry in pairs(value) do
        if math.type(key) ~= "integer" or key < 1 or key > #value or not accepts(entry) then
            return false
        end

        count = count + 1
    end

    return count == #value
end

local function wantedPath(value)
    if not path(value) then
        bridge.raise("workspace_request_invalid", "A workspace path is valid bounded text", "")
    end

    return value
end

local function bounded(value, maximum)
    if math.type(value) ~= "integer" or value < 1 then
        bridge.raise("workspace_request_invalid", "A workspace limit is a positive integer", "")
    end

    return math.min(value, maximum)
end

local function skipped(value)
    if not list(value, 64, path) then
        bridge.raise("workspace_request_invalid", "A workspace traversal declares a bounded list of skipped names", "")
    end

    return copied(value, 0, {})
end

local function declared(value, fields)
    if not plain.table(value) then
        bridge.raise("workspace_request_invalid", "A workspace operation declares its options in a table", "")
    end

    for key in pairs(value) do
        if not fields[key] then
            bridge.raise("workspace_request_invalid", "A workspace operation declares only its documented options", "")
        end
    end
end

local function metadata(value)
    return shape(value, {
        path = path,
        name = function(name) return text(name, 4096, true) end,
        kind = function(kind) return kind == "file" or kind == "directory" or kind == "other" end,
        symlink = boolean,
        size = function(size) return math.type(size) == "integer" and size >= 0 end,
        modified = function(stamp) return text(stamp, 64) end,
        recent = boolean,
        readable = boolean,
        writable = boolean,
    })
end

local function inspection(request, workpane)
    local inspected = {}

    inspected.stat = function(wanted)
        wanted = wantedPath(wanted)

        return workpane.future(function()
            local inspected, result = pcall(request, "files.stat", { path = wanted })

            if not inspected then
                if type(result) == "table" and result.code == "remote_path_unavailable" then
                    return nil
                end

                error(result, 0)
            end

            local info = metadata(result)

            return { isFile = info.kind == "file", isDir = info.kind == "directory", isSymlink = info.symlink, size = info.size, mtime = info.modified, recent = info.recent, readable = info.readable, writable = info.writable }
        end)
    end

    inspected.readBounded = function(wanted, maximum)
        wanted = wantedPath(wanted)
        maximum = bounded(maximum, maximumFileBytes)

        return workpane.future(function()
            local result = shape(request("files.read-prefix", { path = wanted, maximum = maximum }), { content = function(value) return type(value) == "string" and #value <= math.ceil(maximum / 3) * 4 end, complete = boolean })
            local decoded, bytes = pcall(crypto.base64Decode, result.content)

            if not decoded or type(bytes) ~= "string" or #bytes > maximum or not result.complete and #bytes ~= maximum or crypto.base64Encode(bytes) ~= result.content then
                error(failure("workspace_response_invalid"), 0)
            end

            return { bytes = bytes, complete = result.complete }
        end)
    end

    inspected.canonical = function(wanted)
        wanted = wantedPath(wanted)

        return workpane.future(function() return shape(request("files.canonical", { path = wanted }), { path = path }).path end)
    end

    inspected.list = function(wanted, options)
        wanted = wantedPath(wanted)
        declared(options, { maximum = true })
        local maximum = bounded(options.maximum, 2000)

        return workpane.future(function()
            local result = shape(request("files.list", { path = wanted, maximum = maximum }), { incomplete = boolean, entries = function(value) return list(value, maximum, function(entry) metadata(entry) return true end) end })
            local entries = {}

            for index, entry in ipairs(result.entries) do
                entries[index] = { name = entry.name, kind = entry.symlink and "symlink" or entry.kind, size = entry.size }
            end

            return { entries = entries, complete = not result.incomplete }
        end)
    end

    return inspected
end

local function connect(options, workpane)
    local profile = options.profile
    local session = options.session
    local closedCodes = options.closedCodes
    local identity = workpane.await(workpane.capabilities.request(options.open, { profile = profile, session = session }))
    local accepted, paths = pcall(function()
        shape(identity, { workspace = token, profile = function(value) return value == profile end, session = function(value) return value == session end, root = path, platform = function(value) return value == "macos" or value == "linux" or value == "windows" end, name = function(value) return text(value, 128) end })
        local declaredPaths = workpane.files.paths(identity.platform)

        if not declaredPaths.absolute(identity.root) then
            error(failure("workspace_response_invalid"), 0)
        end

        return declaredPaths
    end)

    if not accepted then
        if type(identity) == "table" and token(identity.workspace) then
            workpane.capabilities.request(options.close, { workspace = identity.workspace }):await()
        end

        error(paths, 0)
    end

    local backend = { remote = true, available = true, identity = identity, platform = identity.platform, maximumFileBytes = maximumFileBytes }
    local unsubscribe
    local unwatch
    local closing

    local function invalidate(notify)
        if not backend.available then
            return
        end

        backend.available = false

        if backend.process ~= nil then
            backend.process.shutdown()
        end

        if unsubscribe ~= nil then
            unsubscribe()
            unsubscribe = nil
        end

        if unwatch ~= nil then
            unwatch()
            unwatch = nil
        end

        if notify and backend.disconnected ~= nil then
            backend.disconnected()
        end
    end

    local function request(method, parameters)
        if not backend.available then
            error(failure("workspace_closed"), 0)
        end

        if type(parameters) ~= "table" or not text(method, 128) then
            error(failure("workspace_request_invalid"), 0)
        end

        local result, problem = workpane.capabilities.request(options.request, { workspace = identity.workspace, method = method, parameters = copied(parameters, 0, {}) }):await()

        if problem ~= nil then
            if closedCodes[problem.code] then
                invalidate(true)
            end

            error(problem, 0)
        end

        if not backend.available and method ~= "files.write" then
            error(failure("workspace_closed"), 0)
        end

        return result
    end

    backend.paths = paths
    backend.uri = paths.uri
    backend.path = paths.path
    backend.recent = function(info) return info.recent end
    backend.request = request
    backend.process = workspaceProcess.new(backend, workpane)

    for key, operation in pairs(inspection(request, workpane)) do
        backend[key] = operation
    end

    local context

    local function sameRoot(value)
        return value == identity.root
    end

    backend.context = function()
        return workpane.future(function()
            if context == nil then
                local result = shape(request("context.info", {}), { account = path, home = path, repository = path, root = sameRoot })

                if not paths.absolute(result.home) or not paths.absolute(result.repository) then
                    error(failure("workspace_response_invalid"), 0)
                end

                local prefix = result.repository:sub(-1) == "/" and result.repository or result.repository .. "/"

                if result.root ~= result.repository and result.root:sub(1, #prefix) ~= prefix then
                    error(failure("workspace_response_invalid"), 0)
                end

                context = result
            end

            if not backend.available then
                error(failure("workspace_closed"), 0)
            end

            return copied(context, 0, {})
        end)
    end

    backend.inspect = function(scope)
        if scope ~= "home" and scope ~= "repository" then
            bridge.raise("workspace_request_invalid", "A workspace inspection names a declared read scope", "")
        end

        local scoped = inspection(function(method, parameters)
            parameters.scope = scope

            return request("context." .. method:sub(7), parameters)
        end, workpane)
        scoped.paths = paths

        return scoped
    end

    backend.close = function()
        if closing == nil then
            invalidate(false)
            closing = workpane.future(function()
                if not workpane.capabilities.available(options.close) then
                    return
                end

                local _, problem = workpane.capabilities.request(options.close, { workspace = identity.workspace }):await()

                if problem ~= nil and not closedCodes[problem.code] then
                    error(problem, 0)
                end
            end)
        end

        return closing
    end

    backend.read = function(wanted)
        wanted = wantedPath(wanted)

        return workpane.future(function()
            local result = shape(request("files.read", { path = wanted }), { path = path, revision = token, content = function(value) return type(value) == "string" and #value <= math.ceil(maximumFileBytes / 3) * 4 end })
            local decoded, bytes = pcall(crypto.base64Decode, result.content)

            if not decoded or type(bytes) ~= "string" or #bytes > maximumFileBytes or crypto.base64Encode(bytes) ~= result.content then
                error(failure("workspace_response_invalid"), 0)
            end

            return { bytes = bytes, revision = result.revision }
        end)
    end

    backend.readFile = function(wanted)
        return workpane.future(function() return workpane.await(backend.read(wanted)).bytes end)
    end

    backend.save = function(wanted, bytes, revision)
        wanted = wantedPath(wanted)

        return workpane.future(function()
            if type(bytes) ~= "string" or #bytes > maximumFileBytes or type(revision) ~= "string" or revision ~= "" and not token(revision) then
                error(failure("workspace_write_invalid"), 0)
            end

            return shape(request("files.write", { path = wanted, content = crypto.base64Encode(bytes), revision = revision }), { path = path, revision = token })
        end)
    end

    backend.writeFile = function(wanted, bytes) return backend.save(wanted, bytes, "") end
    backend.mkdir = function(wanted)
        wanted = wantedPath(wanted)

        return workpane.future(function() return metadata(request("files.mkdir", { path = wanted })) end)
    end

    backend.rename = function(source, destination)
        source = wantedPath(source)
        destination = wantedPath(destination)

        return workpane.future(function() return shape(request("files.rename", { path = source, destination = destination }), { path = path }) end)
    end

    backend.removeRecursive = function(wanted)
        wanted = wantedPath(wanted)

        return workpane.future(function() return shape(request("files.remove", { path = wanted, recursive = true }), {}) end)
    end

    backend.walk = function(wanted, options)
        wanted = wantedPath(wanted)
        declared(options, { maximum = true, skip = true })
        local maximum = bounded(options.maximum, 20000)
        local skip = skipped(options.skip)

        return workpane.future(function()
            return shape(request("files.walk", { path = wanted, maximum = maximum, skip = skip }), { complete = boolean, paths = function(value) return list(value, maximum, path) end })
        end)
    end

    backend.search = function(wanted, options)
        wanted = wantedPath(wanted)
        declared(options, { maximumMatches = true, maximumFileBytes = true, skip = true, text = true })
        local maximum = bounded(options.maximumMatches, 1000)
        local maximumBytes = bounded(options.maximumFileBytes, maximumFileBytes)
        local skip = skipped(options.skip)
        local query = options.text

        if not text(query, 4096) then
            bridge.raise("workspace_request_invalid", "A workspace search declares valid bounded text", "")
        end

        return workpane.future(function()
            return shape(request("files.search", { path = wanted, text = query, maximum = maximum, maximumFileBytes = maximumBytes, skip = skip }), { complete = boolean, matches = function(value)
                return list(value, maximum, function(entry)
                    shape(entry, { path = path, line = lineNumber, text = searchText })

                    return true
                end)
            end })
        end)
    end

    unsubscribe = workpane.events.subscribe(options.changed, function(event)
        if type(event) == "table" and event.workspace == identity.workspace and event.connected == false then
            invalidate(true)
        end
    end)

    unwatch = workpane.capabilities.watch(options.request, function(available)
        if not available then
            invalidate(true)
        end
    end)

    local inspected, info = pcall(function()
        return workpane.await(backend.stat(identity.root))
    end)

    if not inspected or info == nil or not info.isDir then
        backend.close():await()
        error(inspected and failure("workspace_root_invalid") or info, 0)
    end

    return backend
end

function workspace.open(owner, options, workpane)
    lifecycle.check(owner)

    if not plain.table(options) then
        bridge.raise("workspace_endpoint_invalid", "A workspace declares its endpoint in a table", "")
    end

    local fields = { open = true, request = true, close = true, changed = true, profile = true, session = true, closedCodes = true }
    local copied = {}

    for key in pairs(options) do
        if fields[key] == nil then
            bridge.raise("workspace_endpoint_invalid", "A workspace endpoint declares only its transport and connection identity", "")
        end
    end

    for _, key in ipairs({ "open", "request", "close", "changed", "profile", "session" }) do
        if not text(options[key], 128) then
            bridge.raise("workspace_endpoint_invalid", "A workspace endpoint declares valid bounded text", "")
        end

        copied[key] = options[key]
    end

    for _, key in ipairs({ "open", "request", "close", "changed" }) do
        if copied[key]:match("^[a-z0-9][a-z0-9%-]*%.[a-z0-9][a-z0-9%-]*%.[a-z0-9][a-z0-9%-]*$") == nil then
            bridge.raise("workspace_endpoint_invalid", "A workspace endpoint declares valid capability and event names", "")
        end
    end

    if not list(options.closedCodes, 16, function(code) return text(code, 128) end) then
        bridge.raise("workspace_endpoint_invalid", "A workspace endpoint declares its bounded list of closed transport failures", "")
    end

    copied.closedCodes = { capability_unavailable = true }

    for _, code in ipairs(options.closedCodes) do
        copied.closedCodes[code] = true
    end

    return task.future(owner, function()
        return connect(copied, workpane)
    end)
end

return workspace
