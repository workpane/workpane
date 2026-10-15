-- The page of the plugin, with the configured servers above and the requests of the selected one below, divided where the reader left the split.
local async = require("async")
local crypto = require("crypto")
local address = include("address")
local preferences = include("preferences")
local servers = include("servers")
local form = include("views/server-form")

local ui = workpane.ui
local text = workpane.i18n.text
local number = workpane.i18n.number
local translate = workpane.i18n.translate

local view = {}

local terminalCapability = "terminal.workspace.snapshot"
local browserCapability = "workspace.page.open"
local splitMinimum = 120
local states = {
    running = { key = "web-server.manager.running", tone = "success" },
    working = { key = "web-server.manager.working", tone = "warning" },
    stopped = { key = "web-server.manager.stopped" },
}

local split

-- A response size is written in binary units, the way the operating system counts file sizes.
local function size(bytes)
    local units = {
        { 1024 ^ 3, "web-server.unit.gibibytes" },
        { 1024 ^ 2, "web-server.unit.mebibytes" },
        { 1024, "web-server.unit.kibibytes" },
    }

    for _, unit in ipairs(units) do
        if bytes >= unit[1] then
            return text(unit[2], number(bytes / unit[1], 2))
        end
    end

    return text("web-server.unit.bytes", number(bytes, 0))
end

local function requestRow(request)
    return {
        id = request.id,
        cells = {
            { text = request.presented, muted = true },
            { text = tostring(request.status), tone = request.status < 400 and "success" or "danger" },
            request.method,
            request.path,
            { text = text("web-server.requests.milliseconds", number(request.durationMs, 0)) },
            { text = size(request.responseBytes) },
            request.remoteAddress,
        },
    }
end

-- The actions of a row follow the state of its server, so a starting server can only be stopped and a running one cannot be removed.
local function actions(id)
    local state = servers.state(id)

    if state == "working" then
        return { { id = "stop", icon = "stop", tooltip = text("web-server.manager.stop-server"), tone = "danger" } }
    end

    if state == "stopped" then
        return {
            { id = "edit", icon = "edit", tooltip = text("web-server.dialog.edit") },
            { id = "start", icon = "start", tooltip = text("web-server.manager.start-server"), tone = "success" },
            { id = "remove", icon = "close", tooltip = text("web-server.manager.remove-server"), destructive = true },
        }
    end

    local running = {
        { id = "edit", icon = "edit", tooltip = text("web-server.dialog.edit") },
        { id = "open", icon = "external-link", tooltip = text("web-server.manager.open-browser"), tone = "success" },
    }

    if workpane.capabilities.available(browserCapability) then
        running[#running + 1] = { id = "browse", icon = "browser", tooltip = text("web-server.manager.open-browser-plugin"), tone = "success" }
    end

    running[#running + 1] = { id = "stop", icon = "stop", tooltip = text("web-server.manager.stop-server"), tone = "danger" }

    return running
end

local function serverRow(configuration)
    local state = states[servers.state(configuration.id)]

    return {
        id = configuration.id,
        cells = {
            { text = text(state.key), icon = "web-server", tone = state.tone, muted = state.tone == nil },
            configuration.name,
            address.url(configuration.host, configuration.port),
            configuration.root,
        },
        actions = actions(configuration.id),
    }
end

local function editServer(id)
    local configuration = servers.find(id)

    if configuration ~= nil then
        form.open({ id = id, terminalId = configuration.terminalId })
    end
end

local function removeServer(id)
    local configuration = servers.find(id)

    if configuration == nil then
        return
    end

    local confirmed = workpane.await(workpane.dialogs.confirm({ title = translate("web-server.dialog.remove-title"), message = translate("web-server.dialog.remove-message", configuration.name), detail = translate("web-server.dialog.remove-detail"), confirmText = translate("web-server.dialog.remove-action"), destructive = true }))

    if confirmed then
        servers.remove(id)
    end
end

-- An address that does not open is told in a notification, because the reader expected a page to appear.
local function openFailed(key)
    workpane.notify.error(translate("web-server.plugin.title"), translate(key))
end

local function openExternally(id)
    local url = servers.url(id)

    if url == nil then
        openFailed("web-server.error.open")
        return
    end

    local _, failure = workpane.system.openUrl(url):await()

    if failure ~= nil then
        openFailed("web-server.error.open")
    end
end

local function openInBrowser(id)
    local url = servers.url(id)

    if url == nil then
        openFailed("web-server.error.browser-message")
        return
    end

    local _, failure = workpane.capabilities.request(browserCapability, { url = url }):await()

    if failure ~= nil then
        openFailed("web-server.error.browser-message")
    end
end

local rowActions = {
    edit = editServer,
    start = servers.start,
    stop = servers.stop,
    remove = removeServer,
    open = openExternally,
    browse = openInBrowser,
}

-- A server made from the active terminal serves its folder, and the terminal that already has a server opens that server instead of a second one.
local function fromTerminal()
    local terminalId = servers.activeTerminal()

    if terminalId == "" then
        workpane.notify.error(translate("web-server.plugin.title"), translate("web-server.manager.no-terminal"))
        return
    end

    local linked = servers.forTerminal(terminalId)
    form.open({ id = linked ~= nil and linked.id or crypto.uuidV4(), terminalId = terminalId })
end

-- Another plugin asks for a folder to be served, and a folder that already has a server opens that server instead of a second one.
function view.serveFolder(payload)
    local folder, configured = servers.requested(payload)
    workpane.shell.navigate("manager")
    workpane.task(form.open, { id = configured ~= nil and configured.id or crypto.uuidV4(), terminalId = configured ~= nil and configured.terminalId or nil, folder = configured == nil and folder or nil })

    return {}
end

-- Moves the split to a ratio in thousandths, which is how the settings section changes a page that is already built.
function view.showSplitRatio(ratio)
    if split ~= nil then
        split:set({ ratio = ratio / 1000 })
    end
end

function view.build()
    local selected = ""
    local requestsShownFor
    local requestsDue = false

    local summary = ui.label({ text = "", style = "muted" })
    local empty = ui.emptyState({ text = text("web-server.manager.empty"), icon = "web-server", grow = 1 })
    local serverTable = ui.table({ columns = {
        { id = "status", title = text("web-server.manager.status") },
        { id = "name", title = text("web-server.manager.name") },
        { id = "address", title = text("web-server.manager.address") },
        { id = "root", title = text("web-server.manager.document-root"), width = "stretch" },
    }, rows = {}, header = true, selection = "subtle", grow = 1 })
    local requestTitle = ui.sectionTitle({ text = text("web-server.manager.requests") })
    local clear = ui.button({ text = text("web-server.manager.clear-log"), icon = "clear", enabled = false })
    local requestTable = ui.table({ columns = {
        { id = "time", title = text("web-server.requests.date-time"), width = 180 },
        { id = "status", title = text("web-server.requests.status"), width = 90 },
        { id = "method", title = text("web-server.requests.method"), width = 100 },
        { id = "path", title = text("web-server.requests.path"), width = "stretch" },
        { id = "duration", title = text("web-server.requests.duration"), width = 100 },
        { id = "size", title = text("web-server.requests.response-size"), width = 110 },
        { id = "remote", title = text("web-server.requests.remote-address"), width = 140 },
    }, rows = {}, header = true, grow = 1 })
    local terminalButton = ui.button({ text = text("web-server.manager.from-terminal"), icon = "terminal", visible = false, onClick = fromTerminal })

    local function renderRequests()
        local rows = {}

        for index, request in ipairs(servers.requests(selected)) do
            rows[index] = requestRow(request)
        end

        requestsShownFor = selected
        requestTable:set({ rows = rows })
        clear:set({ enabled = #rows > 0 })
    end

    -- Requests arrive in batches, and every batch of one turn of the loop is shown by a single render on the next one.
    local function requestsArrived()
        if requestsDue then
            return
        end

        requestsDue = true
        async.sleep(0):await()
        requestsDue = false
        renderRequests()
    end

    -- The selection stays on the server it was on, and falls to the first server when that one is gone.
    local function renderServers()
        local listed = servers.list()
        local rows = {}
        local kept = false

        for index, configuration in ipairs(listed) do
            rows[index] = serverRow(configuration)
            kept = kept or configuration.id == selected
        end

        selected = kept and selected or (listed[1] ~= nil and listed[1].id or "")
        local current = servers.find(selected)

        serverTable:set({ rows = rows, selected = selected })
        summary:set({ text = text("web-server.manager.summary", number(#listed, 0), number(servers.runningCount(), 0)) })
        empty:set({ visible = #listed == 0 })
        split:set({ visible = #listed > 0 })
        requestTitle:set({ text = current ~= nil and text("web-server.manager.requests-for", current.name) or text("web-server.manager.requests") })
        terminalButton:set({ visible = workpane.capabilities.available(terminalCapability) })

        -- The requests follow the selection, and a server that changed state announces its own requests.
        if selected ~= requestsShownFor then
            renderRequests()
        end
    end

    serverTable:on("select", function(event)
        selected = event.id
        renderServers()
    end)

    serverTable:on("activate", function(event)
        editServer(event.id)
    end)

    serverTable:on("action", function(event)
        rowActions[event.action](event.id)
    end)

    clear:on("click", function()
        servers.clearRequests(selected)
    end)

    split = ui.splitter({ orientation = "vertical", ratio = preferences.splitRatio() / 1000, firstMinimum = splitMinimum, secondMinimum = splitMinimum, grow = 1 }, serverTable, ui.column({}, {
        ui.row({ padding = { 10, 10, 8, 18 }, spacing = 8 }, { requestTitle, ui.spacer({ grow = 1 }), clear }),
        requestTable,
    }))

    -- The split is stored in thousandths within the bounds the settings offer.
    split:on("resize", function(event)
        preferences.setSplitRatio(math.max(150, math.min(850, math.floor(event.ratio * 1000 + 0.5))))
    end)

    servers.subscribe(function(id, what)
        if what == "requests" and id == selected then
            requestsArrived()
        elseif what == "servers" then
            renderServers()
        end
    end)

    -- The actions and the button that reach other plugins follow those plugins as they start and stop.
    for _, capability in ipairs({ browserCapability, terminalCapability }) do
        workpane.capabilities.watch(capability, renderServers)
    end

    renderServers()

    return ui.column({}, {
        ui.pageHeader({ title = text("web-server.manager.title") }, {
            terminalButton,
            ui.button({ text = text("web-server.manager.new-server"), icon = "add", variant = "primary", onClick = function()
                form.open({ id = crypto.uuidV4() })
            end }),
        }),
        ui.row({ padding = { 12, 18, 10, 18 } }, { summary }),
        empty,
        split,
    })
end

return view
