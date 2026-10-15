local engine = include("engine")
local filesystem = include("filesystem")

local translate = workpane.i18n.translate
local ui = workpane.ui

local links = {}
local available = {}
local forget = {}

function links.start()
    for _, name in ipairs({ "workspace.file.open", "workspace.folder.open", "workspace.remote-path.open" }) do
        available[name] = workpane.capabilities.available(name)
        forget[#forget + 1] = workpane.capabilities.watch(name, function(present)
            available[name] = present
        end)
    end
end

function links.stop()
    for _, unwatch in ipairs(forget) do
        unwatch()
    end

    forget = {}
    available = {}
end

local function withBackend(task, operation)
    if task == nil or task.hostKind == "local" then
        return operation(filesystem.native())
    end

    local scope = workpane.resources.scope()
    local accepted, result = pcall(function()
        local function open()
            return filesystem.open(task)
        end

        local function close(backend)
            return backend.close()
        end

        local backend = workpane.await(scope:acquire(open, close))

        return operation(backend)
    end)

    local _, cleanup = scope:close():await()

    if not accepted or cleanup ~= nil then
        error(not accepted and result or cleanup, 0)
    end

    return result
end

function links.openFolder(task, requested)
    return withBackend(task, function(backend)
        local capability = requested or (backend.remote and "workspace.remote-path.open" or "workspace.folder.open")
        local payload = backend.remote and { profile = backend.identity.profile, session = backend.identity.session, path = task.workdir } or { path = task.workdir }

        return workpane.await(workpane.capabilities.request(capability, payload))
    end)
end

function links.bind(node, taskId, fileOpened)
    local task = engine.task(taskId)

    if task ~= nil and task.hostKind == "local" and task.workdir ~= "" then
        node:set({ basePath = task.workdir })
    end

    node:on("image-request", function(event)
        local task = engine.task(taskId)
        local loaded, reply = pcall(function()
            return withBackend(task, function(backend)
                local request = { source = event.source, request = event.request, basePath = task ~= nil and task.workdir ~= "" and task.workdir or event.basePath }

                return workpane.await(ui.loadMarkdownImage(request, backend.remote and backend or nil))
            end)
        end)

        node:command("load-image", loaded and reply or { source = event.source, request = event.request, failed = true })
    end)

    node:on("link", function(event)
        local opened, failure = pcall(function()
            if event.url:match("^https?://") then
                workpane.await(workpane.system.openUrl(event.url))
                return
            end

            local task = engine.task(taskId)
            return withBackend(task, function(backend)
                local paths = backend.paths
                local line, column = event.url:match(":(%d+):(%d+)$")
                line = line or event.url:match(":(%d+)$") or event.url:match("#L(%d+)")
                local target = event.url:gsub("#L%d+.*$", ""):gsub(":%d+:?%d*$", "")
                local path = paths.path(target) or target:gsub("%%(%x%x)", function(hex)
                    return string.char(tonumber(hex, 16))
                end)

                if path:match("^%a[%w+.-]*:") and not paths.absolute(path) then
                    error({ code = "ai_link_invalid", message = "The link does not name a web address or a file", detail = "" }, 0)
                end

                if path:sub(1, 2) == "~/" then
                    path = (backend.remote and backend.account.home or workpane.system.home()) .. path:sub(2)
                end

                if not paths.absolute(path) then
                    if task == nil or task.workdir == "" then
                        error({ code = "ai_link_directory_missing", message = "The task has no folder for a relative link", detail = "" }, 0)
                    end

                    path = task.workdir .. "/" .. path
                end

                path = workpane.await(backend.canonical(path))
                local info = workpane.await(backend.stat(path))
                local capability = backend.remote and "workspace.remote-path.open" or info ~= nil and info.isDir and "workspace.folder.open" or "workspace.file.open"

                if not available[capability] then
                    error({ code = "ai_link_editor_unavailable", message = "The code editor is unavailable", detail = "" }, 0)
                end

                local payload = { path = path }

                if backend.remote then
                    payload.profile = backend.identity.profile
                    payload.session = backend.identity.session
                end

                if capability ~= "workspace.folder.open" then
                    payload.line = tonumber(line) or 1
                    payload.column = tonumber(column) or 1

                    if not backend.remote and task ~= nil and task.workdir ~= "" then
                        local root = workpane.await(backend.canonical(task.workdir))
                        local prefix = root:sub(-1) == "/" and root or root .. "/"

                        if path:sub(1, #prefix) == prefix then
                            payload.root = root
                        end
                    end
                end

                workpane.await(workpane.capabilities.request(capability, payload))

                if fileOpened ~= nil then
                    fileOpened()
                end
            end)
        end)

        if not opened then
            workpane.log.warning("ai.tasks", "The conversation link could not be opened", { code = type(failure) == "table" and failure.code or "ai_link_failed" })
            workpane.notify.error(translate("ai.plugin.title"), translate("ai.error.link-open"))
        end
    end)

    return node
end

return links
