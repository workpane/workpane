local crypto = require("crypto")
local json = require("json")
local packets = include("sftp-packets")
local protocol = include("protocol")
local source = include("source")

local sftpFiles = {}

local Files = {}
Files.__index = Files

local types = packets.types
local maximumBytes = 4 * 1024 * 1024

local function failed(code)
    error(protocol.failure(code), 0)
end

local function text(value)
    return type(value) == "string" and #value > 0 and #value <= 4096 and value:find("%z") == nil and utf8.len(value) ~= nil
end

local function basename(path)
    return path:match("([^/]+)$") or ""
end

local function parent(path)
    local result = path:match("^(.*)/[^/]+$")

    return result == "" and "/" or result
end

local function within(root, path)
    return root == "/" and path:sub(1, 1) == "/" or path == root or path:sub(1, #root + 1) == root .. "/"
end

function sftpFiles.new(client, scope, root, platform, resolve, execute)
    if not text(root) or not workpane.files.paths(platform).absolute(root) or platform == "windows" and type(resolve) ~= "function" then
        failed("ssh_workspace_invalid")
    end

    return setmetatable({ client = client, scope = scope, root = root, platform = platform, nativeResolve = resolve, execute = execute }, Files)
end

function Files:wire(path)
    if self.platform == "windows" and path:match("^[A-Za-z]:/") ~= nil then
        return "/" .. path
    end

    return path
end

function Files:named(path)
    if not text(path) then
        failed("remote_path_invalid")
    end

    if self.platform == "windows" then
        path = path:gsub("\\", "/")
    end

    if not workpane.files.paths(self.platform).absolute(path) then
        path = self.root .. "/" .. path
    end

    local prefix = "/"
    local rest = path

    if self.platform == "windows" then
        prefix = path:match("^[A-Za-z]:/") or path:match("^//[^/]+/[^/]+/")

        if prefix == nil then
            local share = path:match("^//[^/]+/[^/]+$")
            prefix = share ~= nil and share .. "/" or nil
        end

        if prefix == nil then
            failed("remote_path_invalid")
        end

        rest = path:sub(#prefix + 1)
    end

    local names = {}

    for name in rest:gmatch("[^/]+") do
        if name == ".." then
            if #names == 0 then
                failed("remote_path_outside_workspace")
            end

            table.remove(names)
        elseif name ~= "." then
            names[#names + 1] = name
        end
    end

    local normalized = prefix .. table.concat(names, "/")

    if self.platform ~= "windows" and not within(self.root, normalized) then
        failed("remote_path_outside_workspace")
    end

    return normalized
end

function Files:request(kind, payload, expected, missing, eof, maximumData)
    local value, problem = self.client:request(kind, payload, expected, 10000, maximumData)

    if problem ~= nil then
        error(problem, 0)
    end

    if value.type ~= types.status or value.status == 0 then
        return value
    end

    if missing and value.status == 2 or eof and value.status == 1 then
        return nil
    end

    failed(value.status == 2 and "remote_path_unavailable" or "remote_path_inspection_failed")
end

function Files:attributes(path, missing)
    local value = self:request(types.lstat, packets.string(self:wire(path)), types.attrs, missing)

    if value == nil then
        return nil
    end

    if math.type(value.attributes.permissions) ~= "integer" then
        failed("ssh_sftp_response_invalid")
    end

    return value.attributes
end

function Files:realpath(path, missing)
    local value = self:request(types.realpath, packets.string(self:wire(path)), types.name, missing)

    if value == nil then
        return nil
    end

    if #value.entries ~= 1 or not text(value.entries[1].name) then
        failed("ssh_sftp_response_invalid")
    end

    local resolved = value.entries[1].name

    if self.platform == "windows" and resolved:match("^/[A-Za-z]:/") ~= nil then
        resolved = resolved:sub(2)
    end

    if not workpane.files.paths(self.platform).absolute(resolved) then
        failed("ssh_sftp_response_invalid")
    end

    return resolved
end

function Files:resolve(path, missing)
    local named = self:named(path)

    if self.platform == "windows" then
        return self.nativeResolve(self.root, named, missing), named
    end

    local inspected = named
    local suffix = {}

    for _ = 1, 256 do
        local resolved = self:realpath(inspected, missing)

        if resolved ~= nil then
            if not within(self.root, resolved) then
                failed("remote_path_outside_workspace")
            end

            for _, name in ipairs(suffix) do
                resolved = (resolved == "/" and "" or resolved) .. "/" .. name
            end

            return resolved, named
        end

        if self:attributes(inspected, true) ~= nil then
            failed("remote_path_inspection_failed")
        end

        local directory = parent(inspected)

        if directory == nil or inspected == self.root or not within(self.root, directory) then
            failed("remote_path_unavailable")
        end

        table.insert(suffix, 1, basename(inspected))
        inspected = directory
    end

    failed("remote_path_inspection_failed")
end

function Files:stat(path)
    local resolved, named = self:resolve(path, false)
    local attributes = self:attributes(named, false)
    local metadata = self.scope:metadata(resolved, 10000)
    metadata.path = resolved
    metadata.name = basename(named)
    metadata.symlink = attributes.permissions & 0xf000 == 0xa000

    return metadata
end

function Files:list(path, maximum)
    if math.type(maximum) ~= "integer" or maximum < 1 or maximum > 2000 then
        failed("remote_listing_invalid")
    end

    local entries = {}
    local complete = self:visit(path, maximum, function(entry)
        entries[#entries + 1] = entry

        return true
    end)

    return { entries = entries, incomplete = not complete }
end

function Files:bytes(path, maximum, prefix)
    if math.type(maximum) ~= "integer" or maximum < 1 or maximum > maximumBytes then
        failed("remote_file_too_large")
    end

    local resolved = self:resolve(path, false)
    local before = self.scope:metadata(resolved, 10000)

    if before.kind ~= "file" then
        failed("remote_file_unavailable")
    end

    if not prefix and before.size > maximum then
        failed("remote_file_too_large")
    end

    local handle = self:request(types.open, packets.string(self:wire(resolved)) .. packets.integer(1) .. packets.integer(0), types.handle).handle
    local parts = {}
    local count = 0
    local accepted, problem = pcall(function()
        while count <= maximum do
            local limit = math.min(packets.maximumData, maximum + 1 - count)
            local value = self:request(types.read, packets.string(handle) .. packets.offset(count) .. packets.integer(limit), types.data, false, true, limit)

            if value == nil then
                return
            end

            if #value.data == 0 or #value.data > limit then
                failed("ssh_sftp_response_invalid")
            end

            parts[#parts + 1] = value.data
            count = count + #value.data
        end
    end)

    local closed, cleanup = pcall(self.request, self, types.close, packets.string(handle), types.status)

    if not accepted then
        error(problem, 0)
    end

    if not closed then
        error(cleanup, 0)
    end

    local after = self.scope:metadata(resolved, 10000)

    if before.modified ~= after.modified or before.size ~= after.size or not prefix and count ~= before.size then
        failed("remote_file_changed")
    end

    if not prefix and count > maximum then
        failed("remote_file_too_large")
    end

    return table.concat(parts):sub(1, maximum), count <= maximum, resolved
end

function Files:read(path)
    local bytes, _, resolved = self:bytes(path, maximumBytes, false)
    local revision, problem = workpane.files.hash(bytes):await()

    if problem ~= nil then
        error(problem, 0)
    end

    return { path = resolved, content = crypto.base64Encode(bytes), revision = revision }
end

function Files:prefix(path, maximum)
    local bytes, complete = self:bytes(path, maximum, true)

    return { content = crypto.base64Encode(bytes), complete = complete }
end

function Files:upload(path, bytes, mode)
    if type(bytes) ~= "string" or #bytes > maximumBytes then
        failed("remote_file_too_large")
    end

    local handle = self:request(types.open, packets.string(self:wire(path)) .. packets.integer(2 | 8 | 32) .. packets.integer(4) .. packets.integer(mode), types.handle).handle
    local accepted, problem = pcall(function()
        for offset = 0, #bytes - 1, packets.maximumData do
            self:request(types.write, packets.string(handle) .. packets.offset(offset) .. packets.string(bytes:sub(offset + 1, offset + packets.maximumData)), types.status)
        end
    end)

    local closed, cleanup = pcall(self.request, self, types.close, packets.string(handle), types.status)

    if not accepted then
        error(problem, 0)
    end

    if not closed then
        error(cleanup, 0)
    end
end

function Files:write(path, encoded, expected)
    if type(expected) ~= "string" or expected ~= "" and (#expected ~= 64 or expected:match("^[a-f0-9]+$") == nil) or type(encoded) ~= "string" or #encoded > math.ceil(maximumBytes / 3) * 4 or type(self.execute) ~= "function" then
        failed("remote_write_invalid")
    end

    local accepted, bytes = pcall(crypto.base64Decode, encoded)

    if not accepted or type(bytes) ~= "string" or #bytes > maximumBytes or crypto.base64Encode(bytes) ~= encoded then
        failed("remote_write_invalid")
    end

    local resolved = self:resolve(path, true)
    local directory = parent(resolved)

    if directory == nil or resolved == self.root then
        failed("remote_write_invalid")
    end

    local revision, failure = workpane.files.hash(bytes):await()

    if failure ~= nil then
        error(failure, 0)
    end

    local stage = self.scope:stage(directory, 10000)
    local committed, problem = pcall(function()
        self:upload(stage.path .. "/payload", bytes, self.platform == "windows" and 384 or 438)
        local script = source.asset(self.platform == "windows" and "ssh-write-windows.ps1" or "ssh-write-posix.sh")
        local result = self.execute(script, { stage.path, resolved, expected, self.platform })

        if result.code ~= 0 then
            failed(result.code == 10 and "remote_file_conflict" or "remote_file_write_failed")
        end

        return true
    end)

    local closed, cleanup = pcall(self.scope.unstage, self.scope, stage, 10000)

    if not committed then
        error(problem, 0)
    end

    if not closed then
        error(cleanup, 0)
    end

    return { path = resolved, revision = revision }
end

function Files:mkdir(path)
    local resolved = self:resolve(path, true)
    self:request(types.mkdir, packets.string(self:wire(resolved)) .. packets.integer(4) .. packets.integer(511), types.status)

    return self:stat(resolved)
end

function Files:rename(path, destination)
    local resolved, named = self:resolve(path, false)
    local target, renamed = self:resolve(destination, true)

    if resolved == self.root or target == self.root or type(self.execute) ~= "function" then
        failed("remote_file_rename_failed")
    end

    local script = source.asset(self.platform == "windows" and "ssh-rename-windows.ps1" or "ssh-rename-posix.sh")
    local result = self.execute(script, { named, renamed, self.platform })

    if result.code ~= 0 then
        failed("remote_file_rename_failed")
    end

    return { path = renamed }
end

function Files:visit(path, maximum, visitor, follow)
    if math.type(maximum) ~= "integer" or maximum < 0 or maximum > 100000 or type(visitor) ~= "function" then
        failed("remote_listing_invalid")
    end

    local resolved = self:resolve(path, false)
    local handle = self:request(types.opendir, packets.string(self:wire(resolved)), types.handle).handle
    local count = 0
    local raw = 0
    local complete = true
    local accepted, problem = pcall(function()
        while true do
            local value = self:request(types.readdir, packets.string(handle), types.name, false, true)

            if value == nil then
                return
            end

            if #value.entries == 0 then
                failed("ssh_sftp_response_invalid")
            end

            for _, entry in ipairs(value.entries) do
                raw = raw + 1

                if raw > maximum + 2 then
                    complete = false
                    return
                end

                if entry.name ~= "." and entry.name ~= ".." then
                    if count == maximum then
                        complete = false
                        return
                    end

                    count = count + 1
                    local valid = text(entry.name) and entry.name:find("/", 1, true) == nil and (self.platform ~= "windows" or entry.name:find("\\", 1, true) == nil)
                    local readable, metadata = pcall(function()
                        if not valid then
                            failed("remote_path_invalid")
                        end

                        local named = (resolved == "/" and "" or resolved) .. "/" .. entry.name

                        if follow == false then
                            local attributes = self:attributes(named, false)
                            local kind = attributes.permissions & 0xf000

                            return { name = entry.name, path = named, kind = kind == 0x4000 and "directory" or kind == 0x8000 and "file" or "other", symlink = kind == 0xa000 }
                        end

                        return self:stat(named)
                    end)

                    if readable then
                        metadata.path = (resolved == "/" and "" or resolved) .. "/" .. entry.name

                        if not visitor(metadata) then
                            complete = false
                            return
                        end
                    else
                        complete = false
                    end
                end
            end
        end
    end)

    local closed, cleanup = pcall(self.request, self, types.close, packets.string(handle), types.status)

    if not accepted then
        error(problem, 0)
    end

    if not closed then
        error(cleanup, 0)
    end

    return complete, count
end

function Files:remove(path, recursive)
    local resolved, named = self:resolve(path, false)

    if resolved == self.root then
        failed("remote_file_remove_failed")
    end

    local attributes = self:attributes(named, false)
    local root = { path = named, directory = attributes.permissions & 0xf000 == 0x4000 }
    local pending = { root }
    local entries = { root }
    local inspected = 1
    local bytes = #named

    while #pending > 0 do
        local entry = table.remove(pending)

        if recursive and entry.directory then
            local complete, count = self:visit(entry.path, 20000 - inspected, function(child)
                if #child.path > 1048576 - bytes then
                    return false
                end

                local item = { path = child.path, directory = child.kind == "directory" and not child.symlink }
                entries[#entries + 1] = item
                pending[#pending + 1] = item
                bytes = bytes + #child.path

                return true
            end, false)
            inspected = inspected + count

            if not complete then
                failed("remote_file_remove_failed")
            end
        end
    end

    for index = #entries, 1, -1 do
        local entry = entries[index]
        self:request(entry.directory and types.rmdir or types.remove, packets.string(self:wire(entry.path)), types.status)
    end

    return {}
end

function Files:walk(path, maximum, skipped, visitor)
    if math.type(maximum) ~= "integer" or maximum < 1 or maximum > 100000 or type(skipped) ~= "table" or #skipped > 64 then
        failed("remote_walk_invalid")
    end

    local excluded = {}

    for _, name in ipairs(skipped) do
        if not text(name) or name == "." or name == ".." or name:find("[/\\]") ~= nil then
            failed("remote_walk_invalid")
        end

        excluded[name] = true
    end

    local root = self:resolve(path, false)
    local directories = { { path = root, relative = "" } }
    local paths = {}
    local inspected = 0
    local complete = true
    local stopped = false
    local bytes = 0

    while #directories > 0 and not stopped do
        local directory = table.remove(directories)
        local accepted, visited, count = pcall(self.visit, self, directory.path, 100000 - inspected, function(entry)
            local relative = directory.relative .. entry.name

            if entry.kind == "directory" and not entry.symlink and not excluded[entry.name] then
                directories[#directories + 1] = { path = entry.path, relative = relative .. "/" }
            elseif entry.kind == "file" and not entry.symlink then
                if visitor == nil then
                    local size = #json.encode(relative)

                    if #paths == maximum or size > 1048576 - bytes then
                        stopped = true
                        return false
                    end

                    paths[#paths + 1] = relative
                    bytes = bytes + size
                end

                if visitor ~= nil and not visitor(entry, relative) then
                    stopped = true
                    return false
                end
            end

            return true
        end)

        if accepted then
            inspected = inspected + count
            complete = complete and visited
        else
            complete = false

            if self.client.closed or self.scope.closed then
                error(visited, 0)
            end
        end
    end

    return { paths = paths, complete = complete and not stopped }
end

function Files:search(path, wanted, maximum, maximumFileBytes, skipped)
    if not text(wanted) or math.type(maximum) ~= "integer" or maximum < 1 or maximum > 1000 or math.type(maximumFileBytes) ~= "integer" or maximumFileBytes < 1 or maximumFileBytes > maximumBytes then
        failed("remote_search_invalid")
    end

    local matches = {}
    local complete = true
    local inspectedBytes = 0
    local query = wanted:gsub("[A-Z]", string.lower)

    if query:find("[\r\n]") ~= nil then
        return { matches = matches, complete = true }
    end

    local walked = self:walk(path, 100000, skipped, function(entry, relative)
        if entry.size == 0 or entry.size > maximumFileBytes then
            return true
        end

        local accepted, bytes = pcall(self.bytes, self, entry.path, maximumFileBytes, false)

        if not accepted then
            complete = false
            return true
        end

        if bytes:find("%z") ~= nil or utf8.len(bytes) == nil then
            return true
        end

        local line = 0

        for value in (bytes .. "\n"):gmatch("([^\n]*)\n") do
            line = line + 1

            if value:gsub("[A-Z]", string.lower):find(query, 1, true) ~= nil then
                if #matches == maximum then
                    return false
                end

                local trimmed = value:match("^[ \t\r]*(.-)[ \t\r]*$")
                local boundary = utf8.offset(trimmed, 401)
                local match = { path = relative, line = line, text = boundary ~= nil and trimmed:sub(1, boundary - 1) or trimmed }
                local size = #json.encode(match)

                if size > 1048576 - inspectedBytes then
                    return false
                end

                inspectedBytes = inspectedBytes + size
                matches[#matches + 1] = match
            end
        end

        return true
    end)

    return { matches = matches, complete = complete and walked.complete }
end

return sftpFiles
