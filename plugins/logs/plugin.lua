-- Keeps the centralized log of the product, shows it newest first and offers its pages to other plugins.
local settings = include("settings")
local store = include("store")
local view = include("view")

return {
    id = "logs",
    sdk = 1,
    titleKey = "logs.plugin.title",
    descriptionKey = "logs.plugin.description",
    navigation = {
        {
            id = "viewer",
            titleKey = "logs.navigation.viewer",
            icon = "logs",
            placement = "secondary",
            order = 800,
            view = view.build,
            shortcuts = {
                { id = "refresh", keys = "mod+r", action = view.refresh },
            },
        },
    },
    settings = {
        { id = "logs", titleKey = "logs.plugin.title", sections = {
            { id = "general", titleKey = "logs.settings.general", searchKeys = { "logs.settings.storage" }, view = settings.general },
        } },
    },
    migrations = store.migrations,
    start = function()
        workpane.log.subscribe(store.append)
        workpane.capabilities.provide("logs.entries.page", store.pageRequested, { summaryKey = "logs.contract.entries-page", payload = { type = "object", properties = { beforeSequence = { type = "integer", description = "Sequence the entries come before, zero for the newest" }, limit = { type = "integer", description = "How many entries to answer, at most a hundred" } }, required = { "beforeSequence", "limit" } }, agents = true })
        workpane.capabilities.provide("logs.entries.clear", store.clearRequested, { summaryKey = "logs.contract.entries-clear", payload = { type = "object", properties = {} } })
        store.open()
    end,
    stop = store.drain,
}
