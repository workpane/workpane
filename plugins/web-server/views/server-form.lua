-- Configures one server in a dialog of its own, which also starts, stops and opens that server while it stays open.
local address = include("address")
local servers = include("servers")

local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

local form = {}

local defaultHost = "127.0.0.1"

local function trim(value)
    return (value or ""):match("^%s*(.-)%s*$")
end

local function folderName(path)
    return path:match("([^/\\]+)[/\\]*$") or path
end

-- The fields start from the saved configuration, then from a folder another plugin asked to serve and then from the terminal the form was opened for.
local function initialFields(id, terminalId, folder)
    local configuration = servers.find(id)

    if configuration ~= nil then
        return { heading = configuration.name, name = configuration.name, root = configuration.root, host = configuration.host, port = configuration.port }
    end

    if folder ~= nil then
        return { heading = folderName(folder), name = folderName(folder), root = folder, host = defaultHost, port = servers.availablePort() }
    end

    local terminal = terminalId ~= nil and servers.terminal(terminalId) or nil
    local name = terminal ~= nil and terminal.name or ""
    return { heading = name, name = name, root = terminal ~= nil and terminal.cwd or "", host = defaultHost, port = servers.availablePort() }
end

-- Opens the form of a server and answers once the reader closed it, which a successful start does by itself.
function form.open(options)
    local id = options.id
    local terminalId = options.terminalId
    local fields = initialFields(id, terminalId, options.folder)
    local dialog

    local name = ui.textField({ value = fields.name, placeholder = text("web-server.dialog.name-placeholder"), clearButton = true, grow = 1 })
    local root = ui.textField({ value = fields.root, placeholder = text("web-server.dialog.absolute-path"), clearButton = true, grow = 1 })
    local browse = ui.button({ icon = "folder", variant = "icon", tooltip = text("web-server.dialog.choose-root") })
    local host = ui.textField({ value = fields.host, placeholder = defaultHost, grow = 1 })
    local port = ui.numberField({ value = fields.port, minimum = 1, maximum = 65535, step = 1, decimals = 0, width = 130 })
    local indicator = ui.statusIndicator({ tone = "neutral" })
    local status = ui.label({ text = text("web-server.manager.stopped") })
    local preview = ui.label({ text = "", style = "muted", grow = 1 })
    local problem = ui.alert({ text = "", visible = false })

    local function showPreview()
        preview:set({ text = address.url(trim(host:get("value")), math.tointeger(port:get("value")) or 0) })
    end

    local function showProblem(message)
        problem:set({ text = message, visible = true })
    end

    local function hideProblem()
        problem:set({ visible = false })
    end

    -- The buttons follow the server: a stopped one starts, a running one opens, stops or unlocks its configuration, and one that is starting or stopping can only be stopped.
    local function actions()
        local state = servers.state(id)
        local running = state == "running"
        local working = state == "working"
        local list = { { id = "close", text = translate("web-server.dialog.close") } }

        if running then
            list[#list + 1] = { id = "edit", text = translate("web-server.dialog.edit"), icon = "edit", closes = false }
            list[#list + 1] = { id = "open", text = translate("web-server.dialog.open"), icon = "web-server", closes = false }
        end

        if running or working then
            list[#list + 1] = { id = "stop", text = translate("web-server.dialog.stop"), icon = "stop", closes = false }
        end

        if not running then
            list[#list + 1] = { id = "start", text = translate("web-server.dialog.start"), icon = "start", variant = "primary", closes = false, enabled = not working }
        end

        return list
    end

    -- A running or starting server keeps its configuration locked, so what the form shows is always what the server serves.
    local function refresh()
        local state = servers.state(id)
        local running = state == "running"
        local working = state == "working"

        indicator:set({ tone = running and "success" or working and "warning" or "neutral" })
        status:set({ text = text(running and "web-server.manager.running" or working and "web-server.manager.working" or "web-server.manager.stopped") })

        for _, field in ipairs({ name, root, browse, host, port }) do
            field:set({ enabled = not running and not working })
        end

        if dialog ~= nil then
            dialog:setButtons(actions())
        end

        showPreview()
    end

    browse:on("click", function()
        local folder = workpane.await(workpane.dialogs.selectFolder({ title = translate("web-server.dialog.choose-root-title"), initial = trim(root:get("value")) }))

        if folder == nil then
            return
        end

        root:set({ value = folder })

        if trim(name:get("value")) == "" then
            name:set({ value = folderName(folder) })
        end
    end)

    local pressed = {}

    function pressed.start()
        local started, failure = servers.configureAndStart({ id = id, name = name:get("value"), root = root:get("value"), host = host:get("value"), port = math.tointeger(port:get("value")), terminalId = terminalId })

        if started then
            hideProblem()
            dialog:close("started")
            return
        end

        if failure.code ~= "web_server_start_cancelled" then
            showProblem(failure.message)
        end

        refresh()
    end

    function pressed.stop()
        servers.stop(id)
        hideProblem()
    end

    function pressed.edit()
        local confirmed = workpane.await(workpane.dialogs.confirm({ title = translate("web-server.dialog.edit-title"), message = translate("web-server.dialog.edit-message"), detail = translate("web-server.dialog.edit-detail"), confirmText = translate("web-server.dialog.stop-edit"), destructive = true }))

        if not confirmed then
            return
        end

        servers.stop(id)
        root:command("focus")
    end

    function pressed.open()
        local url = servers.url(id)

        if url == nil then
            showProblem(translate("web-server.error.open"))
            return
        end

        local _, failure = workpane.system.openUrl(url):await()

        if failure ~= nil then
            showProblem(translate("web-server.error.open"))
        end
    end

    host:on("change", showPreview)
    port:on("change", showPreview)

    local content = ui.column({ spacing = 12 }, {
        ui.formField({ label = text("web-server.dialog.name") }, name),
        ui.formField({ label = text("web-server.dialog.document-root") }, ui.row({ spacing = 6 }, { root, browse })),
        ui.row({ spacing = 12 }, { ui.formField({ label = text("web-server.dialog.bind-host"), grow = 1 }, host), ui.formField({ label = text("web-server.dialog.port") }, port) }),
        ui.row({ padding = { 8, 10 }, spacing = 8, background = "panel" }, { indicator, status, preview }),
        problem,
    })

    refresh()

    local stopListening = servers.subscribe(function(changedId, what)
        if what == "servers" and (changedId == nil or changedId == id) then
            refresh()
        end
    end)

    local heading = fields.heading == "" and translate("web-server.dialog.new-heading") or translate("web-server.dialog.heading", fields.heading)
    dialog = workpane.dialogs.custom({ title = heading, message = translate("web-server.dialog.description"), width = 580, content = content, buttons = actions(), onButton = function(button)
        if pressed[button] ~= nil then
            pressed[button]()
        end
    end })

    local _, failure = dialog:await()
    stopListening()

    if failure ~= nil then
        error(failure, 0)
    end
end

return form
