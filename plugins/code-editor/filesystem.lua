local crypto = require("crypto")
local fs = require("fs")
local paths = include("paths")
local servers = include("servers")
local catalog = include("catalog")

local filesystem = {}

local function read(path)
    return workpane.future(function()
        local value = workpane.await(workpane.files.read(path, catalog.limit("maximumFileBytes")))

        if not value.complete then
            error({ code = "files_read_too_large", message = "The file exceeds its read bound", detail = "" }, 0)
        end

        return { bytes = value.bytes }
    end)
end

function filesystem.native()
    return {
        remote = false,
        available = true,
        executable = servers.executable,
        hasServers = servers.any,
        process = {
            start = function(options)
                return workpane.future(function() return workpane.process.start(options) end)
            end,
            write = function(id, bytes)
                workpane.process.write(id, bytes)
            end,
            stop = function(id)
                local stopped, problem = pcall(workpane.process.stop, id)

                if not stopped and problem.code ~= "process_unknown" then
                    error(problem, 0)
                end
            end,
        },
        stat = function(path)
            return workpane.files.stat(path)
        end,
        readFile = function(path)
            return workpane.future(function()
                return workpane.await(read(path)).bytes
            end)
        end,
        read = read,
        writeFile = function(path, bytes)
            return fs.writeFile(path, bytes)
        end,
        mkdir = function(path)
            return fs.mkdir(path)
        end,
        rename = function(source, destination)
            return fs.rename(source, destination)
        end,
        removeRecursive = function(path)
            return fs.removeRecursive(path)
        end,
        list = function(path, options)
            return workpane.files.list(path, options)
        end,
        walk = function(path, options)
            return workpane.files.walk(path, options)
        end,
        search = function(path, options)
            return workpane.files.search(path, options)
        end,
        canonical = function(path)
            return workpane.files.canonical(path)
        end,
        uri = function(path)
            return workpane.files.uri(path)
        end,
        path = function(uri)
            return workpane.files.path(uri)
        end,
        recent = function(info)
            return info.mtime >= os.time() - 2
        end,
        save = function(path, bytes)
            return workpane.future(function()
                local info = workpane.await(workpane.files.stat(path))

                if info ~= nil and info.isSymlink then
                    error({ code = "code_editor_save_symlink", message = "The editor cannot replace a symbolic link", detail = "" }, 0)
                end

                local directory = paths.parent(path)
                local stage = workpane.await(workpane.files.staging(directory .. "/.workpane-save-" .. crypto.uuidV4()))
                local _, failure = stage:run(function()
                    return workpane.future(function()
                        local temporary = stage.path .. "/content"
                        local written = workpane.await(fs.writeFile(temporary, bytes))

                        if written == nil then
                            error({ code = "code_editor_save_failed", message = "The document could not be saved", detail = "" }, 0)
                        end

                        workpane.await(workpane.files.replace(temporary, path))
                    end)
                end):await()
                local _, cleanup = stage:close():await()

                if failure ~= nil or cleanup ~= nil then
                    error(failure or cleanup, 0)
                end

                return {}
            end)
        end,
    }
end

return filesystem
