local fs = require("fs")

local filesystem = {}
local native

function filesystem.native()
    if native == nil then
        native = {
            remote = false,
            available = true,
            platform = workpane.app.platform,
            paths = workpane.files.paths(workpane.app.platform),
            stat = workpane.files.stat,
            canonical = workpane.files.canonical,
            list = workpane.files.list,
            walk = workpane.files.walk,
            search = workpane.files.search,
            readBounded = workpane.files.read,
            mkdir = fs.mkdir,
            rename = fs.rename,
            copy = fs.copy,
            removeRecursive = fs.removeRecursive,
            access = workpane.files.access,
            write = fs.writeFile,
            process = {
                find = workpane.process.find,
                start = function(options)
                    return workpane.future(function() return workpane.process.start(options) end)
                end,
                write = workpane.process.write,
                stop = workpane.process.stop,
            },
        }
    end

    return native
end

function filesystem.remote(backend)
    backend.write = backend.save
    backend.access = function(path)
        return workpane.future(function()
            local info = workpane.await(backend.stat(path))

            return info ~= nil and { readable = info.readable, writable = info.writable } or nil
        end)
    end

    backend.copy = function(source, destination)
        return workpane.future(function()
            local value = workpane.await(backend.read(source))

            return workpane.await(backend.save(destination, value.bytes, ""))
        end)
    end

    return backend
end

function filesystem.profiles()
    return workpane.future(function()
        if not workpane.capabilities.available("ssh.connections.list") then
            return {}
        end

        return workpane.await(workpane.capabilities.request("ssh.connections.list", {})).profiles
    end)
end

function filesystem.open(task)
    return workpane.future(function()
        if task.hostKind == "local" then
            return filesystem.native()
        end

        local profile

        for _, candidate in ipairs(workpane.await(filesystem.profiles())) do
            if candidate.profile == task.hostProfile and candidate.platform == task.hostPlatform and candidate.state == "connected" and candidate.session ~= nil then
                profile = candidate
                break
            end
        end

        if profile == nil then
            error({ code = "ai_workspace_disconnected", message = "The remote task requires its saved SSH profile to be connected", detail = "" }, 0)
        end

        local backend = workpane.await(workpane.workspace.open({ open = "ssh.workspace.open", request = "ssh.workspace.request", close = "ssh.workspace.close", changed = "remote-ssh.workspace.changed", profile = profile.profile, session = profile.session, closedCodes = { "ssh_connection_closed", "ssh_workspace_unavailable" } }))
        local accepted, context = pcall(function()
            local info = workpane.await(backend.stat(task.workdir))

            if info == nil or not info.isDir then
                error({ code = "ai_workspace_invalid", message = "The remote task working directory is unavailable", detail = "" }, 0)
            end

            return workpane.await(backend.context())
        end)

        if not accepted then
            backend.close():await()
            error(context, 0)
        end

        backend.account = context
        local repository = backend.inspect("repository")
        local home = backend.inspect("home")
        backend.resource = function(path)
            local prefix = context.repository:sub(-1) == "/" and context.repository or context.repository .. "/"

            return (path == context.repository or path:sub(1, #prefix) == prefix) and repository or home
        end

        return filesystem.remote(backend)
    end)
end

return filesystem
