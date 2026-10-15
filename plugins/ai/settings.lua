-- The settings sections of the plugin: its connections, the providers and their limits, the agents, the tools agents reach and the execution limits.
-- A setting of one value is edited through the control of its preference, and a list is written whole once the reader confirms a change of it.
local catalog = include("catalog")
local connections = include("connections")
local engine = include("engine")
local preferences = include("preferences")
local agentsView = include("views/agents")
local connectionsView = include("views/connections")
local reveal = include("views/reveal")
local servicesView = include("views/services")

local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

local settings = {}

local scope = { provider = nil, listeners = {} }

local function failed(key, failure)
    workpane.log.error("ai.settings", type(failure) == "table" and tostring(failure.message) or tostring(failure), { code = type(failure) == "table" and failure.code or "", detail = type(failure) == "table" and failure.detail or "" })
    workpane.notify.error(translate("ai.plugin.title"), translate(key))
end

-- Answers whether a write finished, told in a notification when it was refused at once or failed once stored, so nothing acts on a write that did not happen.
local function written(key, save, ...)
    local accepted, future = pcall(save, ...)

    if not accepted then
        failed(key, future)
        return false
    end

    local _, failure = future:await()

    if failure ~= nil then
        failed(key, failure)
        return false
    end

    return true
end

function settings.connections()
    local list = ui.table({ columns = {
        { id = "name", title = text("ai.connection.display-name"), width = 220 },
        { id = "provider", title = text("ai.settings.provider"), width = 200 },
        { id = "model", title = text("ai.settings.model"), width = "stretch" },
    }, rows = {}, selection = "accent", height = 220 })
    local empty = ui.label({ text = text("ai.connection.empty"), style = "muted" })
    local defaultCombo = ui.combo({ value = "", options = {}, width = 320, sorted = true })
    local edit = ui.button({ text = text("ai.connection.edit"), icon = "edit" })
    local remove = ui.button({ text = text("ai.connection.remove"), icon = "clear", variant = "destructive" })

    local function selected()
        return connections.find(preferences.connections(), list:get("selected") or "")
    end

    local function render()
        local configured = preferences.connections()
        local rows = {}
        local options = {}

        for index, connection in ipairs(configured) do
            local provider = catalog.provider(connection.providerId)
            rows[index] = { id = connections.key(connection), cells = { connections.label(connection), { text = text(provider.title) }, connection.modelId } }
            options[index] = { value = connections.key(connection), text = connections.label(connection) }
        end

        local chosen = preferences.defaultConnection()
        list:set({ rows = rows, visible = #rows > 0, selected = selected() ~= nil and list:get("selected") or "" })
        empty:set({ visible = #rows == 0 })
        defaultCombo:set({ options = options, value = chosen ~= nil and connections.key(chosen) or "", enabled = #options > 0 })
        edit:set({ enabled = selected() ~= nil })
        remove:set({ enabled = selected() ~= nil })
    end

    defaultCombo:on("change", function(event)
        written("ai.error.connection-save", preferences.setDefaultConnection, event.value)
    end)

    list:on("select", render)
    list:on("activate", function()
        local connection = selected()

        if connection ~= nil then
            connectionsView.dialog(connection)
        end
    end)

    edit:on("click", function()
        local connection = selected()

        if connection ~= nil then
            connectionsView.dialog(connection)
        end
    end)

    remove:on("click", function()
        local connection = selected()

        if connection == nil then
            return
        end

        for _, agent in ipairs(preferences.agents()) do
            if agent.connectionKey == connections.key(connection) then
                workpane.notify.error(translate("ai.plugin.title"), translate("ai.error.connection-in-use", agent.name))
                return
            end
        end

        local confirmed = workpane.await(workpane.dialogs.confirm({ title = translate("ai.connection.remove-title"), message = translate("ai.connection.remove-message"), detail = connections.label(connection), confirmText = translate("ai.connection.remove"), destructive = true }))

        if not confirmed then
            return
        end

        local kept = {}

        for _, candidate in ipairs(preferences.connections()) do
            if connections.key(candidate) ~= connections.key(connection) then
                kept[#kept + 1] = candidate
            end
        end

        written("ai.error.connection-save", preferences.saveConnections, kept, preferences.defaultConnection() ~= nil and connections.key(preferences.defaultConnection()) or "")
    end)

    preferences.listen(function(key)
        if key == "connections" or key == "defaultConnectionKey" then
            render()
        end
    end)

    render()

    return ui.settingsForm({}, {
        ui.settingsRow({ label = text("ai.settings.default-connection") }, defaultCombo),
        list,
        empty,
        ui.settingsActions({}, { ui.button({ text = text("ai.connection.add"), icon = "add", variant = "primary", onClick = function()
            connectionsView.dialog(nil)
        end }), edit, remove }),
    })
end

-- The provider chosen here scopes the rate limits shown below it.
function settings.providerSelection()
    local options = {}

    for index, descriptor in ipairs(catalog.answering("chat")) do
        options[index] = { value = descriptor.id, text = text(descriptor.title) }
    end

    scope.provider = scope.provider or catalog.answering("chat")[1].id

    return ui.settingsForm({}, {
        ui.settingsRow({ label = text("ai.settings.provider-scope") }, ui.combo({ value = scope.provider, options = options, sorted = true, width = 260, onChange = function(event)
            scope.provider = event.value

            for _, listener in ipairs(scope.listeners) do
                listener()
            end
        end })),
    })
end

function settings.rateLimits()
    scope.provider = scope.provider or catalog.answering("chat")[1].id
    local fields = {}
    local names = { "minimumIntervalMs", "maximumRequestsPerMinute", "maximumConcurrentRequests" }
    local labels = { "ai.settings.rate-limit-interval", "ai.settings.rate-limit-per-minute", "ai.settings.rate-limit-concurrent" }
    local maxima = { catalog.limit("maximumRequestDelayMs"), catalog.limit("maximumRequestsPerMinute"), catalog.limit("maximumConcurrentRequests") }
    local rows = {}

    local function save()
        local limit = { providerId = scope.provider }

        for _, name in ipairs(names) do
            limit[name] = math.tointeger(fields[name]:get("value")) or 0
        end

        written("ai.error.settings-save", preferences.saveRateLimit, limit)
    end

    for index, name in ipairs(names) do
        fields[name] = ui.numberField({ value = preferences.rateLimit(scope.provider)[name], minimum = 0, maximum = maxima[index], step = 1, decimals = 0, width = 160, onChange = save })
        rows[index] = ui.settingsRow({ label = text(labels[index]) }, fields[name])
    end

    scope.listeners[#scope.listeners + 1] = function()
        local limit = preferences.rateLimit(scope.provider)

        for _, name in ipairs(names) do
            fields[name]:set({ value = limit[name] })
        end
    end

    return ui.settingsForm({}, rows)
end

function settings.agents()
    local list = ui.table({ columns = {
        { id = "id", title = text("ai.agent.identifier"), width = 160 },
        { id = "name", title = text("ai.agent.name"), width = 180 },
        { id = "connection", title = text("ai.agent.connection"), width = 220 },
        { id = "description", title = text("ai.agent.description"), width = "stretch" },
    }, rows = {}, selection = "accent", height = 220 })
    local empty = ui.label({ text = text("ai.agent.empty"), style = "muted" })
    local edit = ui.button({ text = text("ai.agent.edit"), icon = "edit" })
    local remove = ui.button({ text = text("ai.agent.remove"), icon = "clear", variant = "destructive" })

    local function selected()
        return preferences.agent(list:get("selected") or "")
    end

    local function render()
        local rows = {}

        for index, agent in ipairs(preferences.agents()) do
            local connection = connections.find(preferences.connections(), agent.connectionKey)
            rows[index] = { id = agent.id, cells = { { text = agent.id, monospace = true }, agent.name, connection ~= nil and connections.label(connection) or agent.connectionKey, agent.description } }
        end

        list:set({ rows = rows, visible = #rows > 0, selected = selected() ~= nil and list:get("selected") or "" })
        empty:set({ visible = #rows == 0 })
        edit:set({ enabled = selected() ~= nil })
        remove:set({ enabled = selected() ~= nil })
    end

    list:on("select", render)
    list:on("activate", function()
        if selected() ~= nil then
            agentsView.dialog(selected())
        end
    end)

    edit:on("click", function()
        if selected() ~= nil then
            agentsView.dialog(selected())
        end
    end)

    -- Removing an agent names the tasks handed to it, which stop with an error until another agent is chosen.
    remove:on("click", function()
        local agent = selected()

        if agent == nil then
            return
        end

        local handed = 0

        for _, task in ipairs(engine.tasks(nil)) do
            handed = handed + (task.agentId == agent.id and 1 or 0)
        end

        local confirmed = workpane.await(workpane.dialogs.confirm({ title = translate("ai.agent.remove-title"), message = translate("ai.agent.remove-message"), detail = handed > 0 and translate("ai.agent.remove-detail", tostring(handed)) or agent.name, confirmText = translate("ai.agent.remove"), destructive = true }))

        if not confirmed then
            return
        end

        local kept = {}

        for _, candidate in ipairs(preferences.agents()) do
            if candidate.id ~= agent.id then
                kept[#kept + 1] = candidate
            end
        end

        if written("ai.error.agent-save", preferences.saveAgents, kept) then
            engine.stopOrphans()
        end
    end)

    preferences.listen(function(key)
        if key == "agents" or key == "connections" then
            render()
        end
    end)

    render()

    return ui.settingsForm({}, {
        list,
        empty,
        ui.settingsActions({}, { ui.button({ text = text("ai.agent.add"), icon = "add", variant = "primary", onClick = function()
            agentsView.dialog(nil)
        end }), edit, remove }),
    })
end

function settings.servers()
    local list = ui.table({ columns = {
        { id = "id", title = text("ai.mcp.identifier"), width = 160 },
        { id = "transport", title = text("ai.mcp.transport"), width = 140 },
        { id = "target", title = text("ai.mcp.target"), width = "stretch" },
        { id = "tools", title = text("ai.mcp.tools"), width = 80, align = "end" },
    }, rows = {}, selection = "accent", height = 200 })
    local empty = ui.label({ text = text("ai.mcp.empty"), style = "muted" })
    local edit = ui.button({ text = text("ai.mcp.edit"), icon = "edit" })
    local remove = ui.button({ text = text("ai.mcp.remove"), icon = "clear", variant = "destructive" })

    local function selected()
        for _, server in ipairs(preferences.mcpServers()) do
            if server.id == list:get("selected") then
                return server
            end
        end

        return nil
    end

    local function render()
        local rows = {}

        for index, server in ipairs(preferences.mcpServers()) do
            local target = server.transport == "stdio" and (server.command .. (#server.arguments > 0 and (" " .. table.concat(server.arguments, " ")) or "")) or server.url
            rows[index] = { id = server.id, cells = { { text = server.id, monospace = true }, { text = text(server.transport == "stdio" and "ai.mcp.transport-stdio" or "ai.mcp.transport-http") }, target, tostring(engine.toolCount(server.id)) } }
        end

        list:set({ rows = rows, visible = #rows > 0, selected = selected() ~= nil and list:get("selected") or "" })
        empty:set({ visible = #rows == 0 })
        edit:set({ enabled = selected() ~= nil })
        remove:set({ enabled = selected() ~= nil })
    end

    list:on("select", render)
    list:on("activate", function()
        if selected() ~= nil then
            servicesView.serverDialog(selected())
        end
    end)

    edit:on("click", function()
        if selected() ~= nil then
            servicesView.serverDialog(selected())
        end
    end)

    remove:on("click", function()
        local server = selected()

        if server == nil then
            return
        end

        local confirmed = workpane.await(workpane.dialogs.confirm({ title = translate("ai.mcp.remove-title"), message = translate("ai.mcp.remove-message"), detail = server.id, confirmText = translate("ai.mcp.remove"), destructive = true }))

        if not confirmed then
            return
        end

        local kept = {}

        for _, candidate in ipairs(preferences.mcpServers()) do
            if candidate.id ~= server.id then
                kept[#kept + 1] = candidate
            end
        end

        if written("ai.error.mcp-save", preferences.saveServers, kept) then
            engine.restartServers()
        end
    end)

    preferences.listen(function(key)
        if key == "mcpServers" then
            render()
        end
    end)

    engine.listen(function(kind)
        if kind == "servers" then
            render()
        end
    end)

    render()

    return ui.settingsForm({}, {
        list,
        empty,
        ui.settingsActions({}, { ui.button({ text = text("ai.mcp.add"), icon = "add", variant = "primary", onClick = function()
            servicesView.serverDialog(nil)
        end }), edit, remove }),
    })
end

-- A hosted search service is reached with its key, and a SearXNG instance with the address the reader runs it at.
-- A key left empty reads as the variable the service officially keeps its key in, which the field names as its placeholder and never stores.
function settings.search()
    local provider = preferences.control("searchProvider", { labels = { brave = text("ai.search.brave"), tavily = text("ai.search.tavily"), searxng = text("ai.search.searxng") }, sorted = true, width = 240 })
    local instance = preferences.control("searchInstanceUrl", { placeholder = text("ai.settings.search-instance-placeholder"), width = 380 })
    local key = reveal.guard(preferences.control("searchApiKey", { as = "secretField", placeholder = preferences.searchKeyReference(preferences.get("searchProvider")), confirmReveal = true, width = 380 }))
    local instanceRow = ui.settingsRow({ label = text("ai.settings.search-instance") }, instance)
    local keyRow = ui.settingsRow({ label = text("ai.settings.search-key") }, key)

    local function refresh()
        local chosen = preferences.get("searchProvider")
        instanceRow:set({ visible = chosen == "searxng" })
        keyRow:set({ visible = chosen ~= "searxng" })
        key:set({ placeholder = preferences.searchKeyReference(chosen) })
    end

    -- A key belongs to its service, so another service starts without one and reads the variable it keeps its key in.
    preferences.listen(function(changed)
        if changed ~= "searchProvider" then
            return
        end

        refresh()
        written("ai.error.service-save", preferences.set, "searchApiKey", "")
    end)

    refresh()

    return ui.settingsForm({}, { ui.settingsRow({ label = text("ai.settings.search-provider") }, provider), instanceRow, keyRow })
end

-- A speaking service with a closed set of voices offers them in a list, and one with an account catalog takes the identifier of a voice.
-- A key left empty reads as the variable the service officially keeps its key in, which the field names as its placeholder and never stores.
function settings.speech()
    local options = {}

    for index, descriptor in ipairs(catalog.answering("speech")) do
        options[index] = { value = descriptor.id, text = text(descriptor.title) }
    end

    local provider = preferences.control("speechProvider", { as = "combo", options = options, sorted = true, width = 240 })
    local voiceText = preferences.control("speechVoiceId", { placeholder = text("ai.settings.speech-voice-placeholder"), width = 380 })
    local voiceChoice = ui.combo({ value = "", options = {}, width = 240 })
    local key = reveal.guard(preferences.control("speechApiKey", { as = "secretField", placeholder = preferences.speechKeyReference(preferences.speech().provider), confirmReveal = true, width = 380 }))

    local function refresh()
        local chosen = preferences.speech()
        local endpoint = preferences.speechEndpoint(chosen.provider)
        local closed = endpoint ~= nil and #endpoint.voices > 0
        local voices = {}
        local offered = false

        for index, voice in ipairs(closed and endpoint.voices or {}) do
            voices[index] = { value = voice, text = voice }
            offered = offered or voice == chosen.voiceId
        end

        voiceChoice:set({ options = voices, value = closed and (offered and chosen.voiceId or endpoint.defaultVoice) or "", visible = closed })
        voiceText:set({ visible = not closed })
        key:set({ placeholder = preferences.speechKeyReference(chosen.provider) })
    end

    voiceChoice:on("change", function(event)
        written("ai.error.service-save", preferences.set, "speechVoiceId", event.value)
    end)

    -- A voice and a key belong to their service, so another service starts from its own.
    preferences.listen(function(changed)
        if changed == "speechProvider" then
            written("ai.error.service-save", preferences.set, "speechVoiceId", "")
            written("ai.error.service-save", preferences.set, "speechApiKey", "")
        end

        if changed == "speechProvider" or changed == "speechVoiceId" then
            refresh()
        end
    end)

    refresh()

    return ui.settingsForm({}, {
        ui.settingsRow({ label = text("ai.settings.speech-provider") }, provider),
        ui.settingsRow({ label = text("ai.settings.speech-voice") }, ui.column({}, { voiceChoice, voiceText })),
        ui.settingsRow({ label = text("ai.settings.speech-key") }, key),
    })
end

function settings.execution()
    return ui.settingsForm({}, {
        ui.settingsRow({ label = text("ai.settings.command-timeout") }, preferences.control("commandTimeoutSeconds", { width = 160 })),
        ui.settingsRow({ label = text("ai.settings.parallel-limit") }, preferences.control("parallelExecutions", { width = 160 })),
        ui.settingsRow({ label = text("ai.settings.chat-font-size") }, preferences.control("chatFontSize", { width = 160 })),
    })
end

return settings
