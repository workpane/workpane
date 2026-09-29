-- Keeps the centralized log of the product, shows it newest first and offers its pages to other plugins.
local settings = include("settings")
local store = include("store")
local view = include("view")

return {
    id = "logs",
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
        workpane.capabilities.provide("logs.entries.page", store.pageRequested)
        workpane.capabilities.provide("logs.entries.clear", store.clearRequested)
        store.open()
    end,
    stop = store.drain,
}
