-- Runs tasks on a board with agents that talk to AI providers and command-line tools, starts a task for any plugin that asks and answers a conversation any plugin sends.
local catalog = include("catalog")
local completion = include("completion")
local engine = include("engine")
local preferences = include("preferences")
local settings = include("settings")
local store = include("store")
local view = include("view")

local startCapability = "ai.task.start"
local completionCapability = "ai.chat.complete"

return {
    id = "ai",
    titleKey = "ai.plugin.title",
    descriptionKey = "ai.plugin.description",
    navigation = {
        {
            id = "tasks",
            titleKey = "ai.navigation.tasks",
            icon = "tasks",
            placement = "primary",
            order = 0,
            view = view.build,
        },
    },
    settings = {
        { id = "connections", titleKey = "ai.settings.group-connections", sections = {
            { id = "general", titleKey = "ai.settings.connections", searchKeys = { "ai.settings.provider", "ai.settings.model", "ai.settings.api-key", "ai.settings.default-connection" }, view = settings.connections },
        } },
        { id = "providers", titleKey = "ai.settings.group-providers", sections = {
            { id = "selection", titleKey = "ai.settings.provider-selection", searchKeys = { "ai.settings.provider-scope", "ai.settings.provider" }, view = settings.providerSelection },
            { id = "rate-limits", titleKey = "ai.settings.rate-limits", searchKeys = { "ai.settings.rate-limit-interval", "ai.settings.rate-limit-per-minute", "ai.settings.rate-limit-concurrent" }, view = settings.rateLimits },
        } },
        { id = "agents", titleKey = "ai.settings.group-agents", sections = {
            { id = "general", titleKey = "ai.settings.agents", searchKeys = { "ai.agent.identifier", "ai.agent.name", "ai.agent.connection", "ai.agent.system-prompt" }, view = settings.agents },
        } },
        { id = "tools", titleKey = "ai.settings.group-tools", sections = {
            { id = "mcp", titleKey = "ai.settings.mcp", searchKeys = { "ai.mcp.identifier", "ai.mcp.transport", "ai.mcp.command", "ai.mcp.address" }, view = settings.servers },
            { id = "search", titleKey = "ai.settings.search", searchKeys = { "ai.settings.search-provider", "ai.settings.search-instance", "ai.settings.search-key" }, view = settings.search },
            { id = "speech", titleKey = "ai.settings.speech", searchKeys = { "ai.settings.speech-provider", "ai.settings.speech-voice", "ai.settings.speech-key" }, view = settings.speech },
        } },
        { id = "general", titleKey = "ai.settings.group-general", sections = {
            { id = "general", titleKey = "ai.settings.execution", searchKeys = { "ai.settings.command-timeout", "ai.settings.parallel-limit", "ai.settings.chat-font-size" }, view = settings.execution },
        } },
    },
    migrations = store.migrations,
    -- The catalogs and the stored board are checked whole before anything runs, so a broken asset or row keeps the plugin from starting.
    start = function()
        catalog.load()
        preferences.define()
        engine.load()
        workpane.capabilities.provide(startCapability, engine.startRequested)
        workpane.capabilities.provide(completionCapability, completion.answer)
    end,
    stop = function()
        engine.stop()
    end,
}
