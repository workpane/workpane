-- The preferences of the plugin: connections, agents, execution, search, speech, MCP servers and rate limits, where every list is read entry by entry so one broken entry never hides the others.
local catalog = include("catalog")
local connections = include("connections")
local prompt = include("prompt")

local preferences = {}

local store

local searchServices = { brave = "BRAVE_API_KEY", tavily = "TAVILY_API_KEY", searxng = "" }
local standardChatFontSize = 11
local largestList = 256

local function entries()
    return { type = "list", items = { type = "any" }, maxItems = largestList }
end

function preferences.define()
    store = workpane.preferences.define({
        connections = entries(),
        defaultConnectionKey = { type = "string", default = "" },
        agents = entries(),
        commandTimeoutSeconds = { type = "integer", default = 600, minimum = 0, maximum = catalog.limit("maximumCommandTimeoutSeconds") },
        parallelExecutions = { type = "integer", default = 1, minimum = 0, maximum = catalog.limit("maximumParallelExecutions") },
        chatFontSize = { type = "integer", default = standardChatFontSize, minimum = 8, maximum = 36 },
        searchProvider = { type = "string", default = "brave", choices = { "brave", "tavily", "searxng" } },
        searchInstanceUrl = { type = "string", default = "" },
        searchApiKey = { type = "string", default = "" },
        speechProvider = { type = "string", default = "" },
        speechVoiceId = { type = "string", default = "" },
        speechApiKey = { type = "string", default = "" },
        mcpServers = entries(),
        rateLimits = entries(),
    })
end

-- A listener hears the key of every section the plugin writes, and the answer removes it.
function preferences.listen(handler)
    return store:watch(function(_, key)
        handler(key)
    end)
end

-- A connection that breaks a rule of the catalog is left out, so the rest still answer.
function preferences.connections()
    local valid = {}
    local keys = {}

    for _, stored in ipairs(store:get("connections")) do
        local accepted, connection = pcall(connections.validate, stored)

        if accepted and not keys[connections.key(connection)] then
            keys[connections.key(connection)] = true
            valid[#valid + 1] = connection
        end
    end

    return valid
end

-- A default key nobody configured reads as the first connection, which is what the settings choose when the default goes away.
function preferences.defaultConnection()
    local list = preferences.connections()
    return connections.find(list, store:get("defaultConnectionKey")) or list[1]
end

local function refuse(code, message, detail)
    error({ code = code, message = message, detail = detail or "" }, 0)
end

-- An agent names itself in lowercase words, carries instructions without unknown tags and runs a bounded number of iterations.
local function validateAgent(agent)
    local name = (agent.name or ""):match("^%s*(.-)%s*$")
    local instructions = (agent.systemPrompt or ""):match("^%s*(.-)%s*$")

    if type(agent.id) ~= "string" or not agent.id:match("^[a-z0-9-]+$") or name == "" or instructions == "" or type(agent.connectionKey) ~= "string" or agent.connectionKey == "" then
        refuse("ai_agent_invalid", "The AI agent is invalid", tostring(agent.id))
    end

    if math.type(agent.maximumIterations) ~= "integer" or agent.maximumIterations < 0 or agent.maximumIterations > catalog.limit("maximumAgentIterations") then
        refuse("ai_agent_invalid", "The AI agent iteration limit is out of range", agent.id)
    end

    local unknown = prompt.unknownTags(instructions)

    if #unknown > 0 then
        refuse("ai_agent_tag_unknown", "The system prompt carries a tag nobody declares", table.concat(unknown, ", "))
    end

    return { id = agent.id, name = name, description = (agent.description or ""):match("^%s*(.-)%s*$"), systemPrompt = instructions, connectionKey = agent.connectionKey, maximumIterations = agent.maximumIterations }
end

local function agentValid(agent, list)
    local accepted = pcall(validateAgent, agent)
    return accepted and connections.find(list, agent.connectionKey) ~= nil
end

function preferences.agents()
    local list = preferences.connections()
    local valid = {}
    local ids = {}

    for _, agent in ipairs(store:get("agents")) do
        if type(agent) == "table" and agentValid(agent, list) and not ids[agent.id] then
            ids[agent.id] = true
            valid[#valid + 1] = agent
        end
    end

    return valid
end

function preferences.agent(id)
    for _, agent in ipairs(preferences.agents()) do
        if agent.id == id then
            return agent
        end
    end

    return nil
end

function preferences.execution()
    return { commandTimeoutSeconds = store:get("commandTimeoutSeconds"), parallelExecutions = store:get("parallelExecutions"), chatFontSize = store:get("chatFontSize") }
end

-- A setting of one value is edited through the control the store builds, bound to its key both ways.
function preferences.control(key, properties)
    return store:control(key, properties)
end

-- Answers the future of the write, which fails when the value breaks its rule or the document could not be kept.
function preferences.set(key, value)
    return store:set(key, value)
end

function preferences.get(key)
    return store:get(key)
end

-- A key the reader left empty reads as the variable the service officially keeps its key in, and the service counts as configured only when a key is really there.
function preferences.search()
    local provider = store:get("searchProvider")
    local stored = store:get("searchApiKey")
    local apiKey = provider ~= "searxng" and (stored ~= "" and stored or preferences.searchKeyReference(provider)) or ""
    local instanceUrl = provider == "searxng" and store:get("searchInstanceUrl") or ""

    return { provider = provider, instanceUrl = instanceUrl, apiKey = apiKey, configured = provider == "searxng" and instanceUrl ~= "" or connections.available(apiKey) }
end

function preferences.searchKeyReference(provider)
    return connections.reference(searchServices[provider] or "")
end

function preferences.searchAddress(settings)
    if settings.provider == "tavily" then
        return "https://api.tavily.com"
    end

    if settings.provider == "searxng" then
        return settings.instanceUrl
    end

    return "https://api.search.brave.com"
end

function preferences.speechEndpoint(providerId)
    local provider = catalog.provider(providerId)
    return provider ~= nil and provider.endpoints.speech or nil
end

-- The voice and the key a reader left empty read as the ones the service declares, and the service counts as configured only when a key is really there.
function preferences.speech()
    local chosen = store:get("speechProvider")
    local answering = catalog.answering("speech")
    local provider = preferences.speechEndpoint(chosen) ~= nil and chosen or (answering[1] ~= nil and answering[1].id or "")
    local endpoint = preferences.speechEndpoint(provider)
    local voice = store:get("speechVoiceId")
    local key = store:get("speechApiKey")
    local apiKey = key ~= "" and key or preferences.speechKeyReference(provider)

    return { provider = provider, voiceId = voice ~= "" and voice or (endpoint ~= nil and endpoint.defaultVoice or ""), apiKey = apiKey, configured = connections.available(apiKey) }
end

function preferences.speechKeyReference(providerId)
    local descriptor = catalog.provider(providerId)
    return descriptor ~= nil and connections.reference(descriptor.apiKeyVariable) or ""
end

local function validateServer(server)
    local transport = server.transport

    if type(server.id) ~= "string" or not server.id:match("^[a-z][a-z0-9-]*$") or #server.id > 32 or (transport ~= "stdio" and transport ~= "http") then
        refuse("ai_mcp_server_invalid", "The MCP server is invalid", tostring(server.id))
    end

    if transport == "stdio" and (type(server.command) ~= "string" or server.command:match("^%s*$")) then
        refuse("ai_mcp_server_invalid", "The MCP server command is required", server.id)
    end

    if transport == "http" and (type(server.url) ~= "string" or not server.url:match("^https?://[^/%s]+")) then
        refuse("ai_mcp_server_invalid", "The MCP server address is invalid", server.id)
    end

    if math.type(server.samplingMaximumTokens) ~= "integer" or server.samplingMaximumTokens < 0 or server.samplingMaximumTokens > catalog.limit("maximumSamplingTokens") then
        refuse("ai_mcp_server_invalid", "The MCP sampling budget is out of range", server.id)
    end

    local arguments = {}
    local roots = {}

    for index, argument in ipairs(type(server.arguments) == "table" and server.arguments or {}) do
        arguments[index] = tostring(argument)
    end

    -- A root is a folder the server may read, which the client names to it by its address, so only an absolute one is kept.
    for index, root in ipairs(type(server.roots) == "table" and server.roots or {}) do
        if type(root) ~= "string" or not workpane.files.absolute(root) then
            refuse("ai_mcp_server_invalid", "An MCP root is not an absolute folder", server.id)
        end

        roots[index] = root
    end

    return { id = server.id, transport = transport, command = server.command or "", arguments = arguments, workdir = server.workdir or "", url = server.url or "", apiKey = server.apiKey or "", roots = roots, samplingEnabled = server.samplingEnabled == true, samplingMaximumTokens = server.samplingMaximumTokens }
end

function preferences.mcpServers()
    local valid = {}
    local ids = {}

    for _, stored in ipairs(store:get("mcpServers")) do
        local accepted, server = pcall(validateServer, stored)

        if accepted and not ids[server.id] then
            ids[server.id] = true
            valid[#valid + 1] = server
        end
    end

    return valid
end

function preferences.rateLimits()
    local valid = {}

    for _, stored in ipairs(store:get("rateLimits")) do
        local fields = { "minimumIntervalMs", "maximumRequestsPerMinute", "maximumConcurrentRequests" }
        local maxima = { catalog.limit("maximumRequestDelayMs"), catalog.limit("maximumRequestsPerMinute"), catalog.limit("maximumConcurrentRequests") }
        local accepted = type(stored) == "table" and catalog.provider(stored.providerId or "") ~= nil

        for index, field in ipairs(fields) do
            accepted = accepted and math.type(stored[field]) == "integer" and stored[field] >= 0 and stored[field] <= maxima[index]
        end

        if accepted then
            valid[#valid + 1] = stored
        end
    end

    return valid
end

function preferences.rateLimit(providerId)
    for _, limit in ipairs(preferences.rateLimits()) do
        if limit.providerId == providerId then
            return limit
        end
    end

    return { providerId = providerId, minimumIntervalMs = 0, maximumRequestsPerMinute = 0, maximumConcurrentRequests = 0 }
end

-- A change is validated whole before anything is written, so the settings never hold a set that breaks a rule.
function preferences.saveConnections(list, defaultKey)
    local validated = connections.validateSet(list)
    local key = defaultKey

    if connections.find(validated, key) == nil then
        key = validated[1] ~= nil and connections.key(validated[1]) or ""
    end

    for _, agent in ipairs(preferences.agents()) do
        if connections.find(validated, agent.connectionKey) == nil then
            refuse("ai_connection_in_use", "An agent runs on the connection", agent.connectionKey)
        end
    end

    return store:update({ connections = validated, defaultConnectionKey = key })
end

-- Editing the key of a connection moves every agent and the default along with it.
function preferences.replaceConnection(previousKey, connection)
    local list = preferences.connections()
    local replaced = false
    local validated = connections.validate(connection)
    local key = connections.key(validated)

    for index, existing in ipairs(list) do
        if connections.key(existing) == previousKey then
            list[index] = validated
            replaced = true
        end
    end

    if not replaced then
        list[#list + 1] = validated
    end

    local agents = preferences.agents()

    for _, agent in ipairs(agents) do
        if agent.connectionKey == previousKey then
            agent.connectionKey = key
        end
    end

    local defaultKey = store:get("defaultConnectionKey")
    defaultKey = (defaultKey == previousKey or defaultKey == "") and key or defaultKey
    local checked = connections.validateSet(list)

    -- The connections, the default and the agents are written together, so a failure never leaves agents pointing at a connection that is gone.
    return store:update({ connections = checked, defaultConnectionKey = connections.find(checked, defaultKey) ~= nil and defaultKey or key, agents = agents })
end

function preferences.setDefaultConnection(key)
    if connections.find(preferences.connections(), key) == nil then
        refuse("ai_connection_unknown", "The connection is not configured", key)
    end

    return store:update({ defaultConnectionKey = key })
end

function preferences.saveAgents(list)
    local ids = {}
    local configured = preferences.connections()
    local validated = {}

    for index, agent in ipairs(list) do
        local checked = validateAgent(agent)

        if ids[checked.id] then
            refuse("ai_agent_duplicate", "The AI agent identifier is already used", checked.id)
        end

        if connections.find(configured, checked.connectionKey) == nil then
            refuse("ai_connection_unknown", "The connection the agent runs on is not configured", checked.connectionKey)
        end

        ids[checked.id] = true
        validated[index] = checked
    end

    return store:update({ agents = validated })
end

function preferences.saveServers(list)
    local ids = {}
    local validated = {}

    for index, server in ipairs(list) do
        local checked = validateServer(server)

        if ids[checked.id] then
            refuse("ai_mcp_server_invalid", "The MCP server identifier is already used", checked.id)
        end

        ids[checked.id] = true
        validated[index] = checked
    end

    return store:update({ mcpServers = validated })
end

-- A limit is stored only while one of its values is set, once per provider.
function preferences.saveRateLimit(limit)
    local list = {}

    for _, existing in ipairs(preferences.rateLimits()) do
        if existing.providerId ~= limit.providerId then
            list[#list + 1] = existing
        end
    end

    if catalog.provider(limit.providerId) == nil then
        refuse("ai_rate_limit_invalid", "The provider rate limit is invalid", tostring(limit.providerId))
    end

    if limit.minimumIntervalMs > 0 or limit.maximumRequestsPerMinute > 0 or limit.maximumConcurrentRequests > 0 then
        list[#list + 1] = limit
    end

    return store:update({ rateLimits = list })
end

return preferences
