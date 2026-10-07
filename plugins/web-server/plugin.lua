-- Serves folders over HTTP from the servers the reader configures, keeps their configurations in its table and shows the requests each one answers.
local preferences = include("preferences")
local servers = include("servers")
local settings = include("settings")
local store = include("store")
local terminals = include("terminals")
local view = include("view")

return {
    id = "web-server",
    sdk = 1,
    titleKey = "web-server.plugin.title",
    descriptionKey = "web-server.plugin.description",
    navigation = {
        { id = "manager", titleKey = "web-server.navigation.server", icon = "web-server", placement = "primary", order = 400, view = view.build },
    },
    settings = {
        { id = "web-server", titleKey = "web-server.plugin.title", sections = {
            { id = "general", titleKey = "web-server.settings.general", searchKeys = { "web-server.settings.splitter-label" }, view = settings.general },
        } },
    },
    migrations = store.migrations,
    -- The stored configurations are read before anything is offered, so a broken row keeps the plugin from starting instead of misbehaving later.
    start = function()
        preferences.define()
        preferences.watchSplitRatio(view.showSplitRatio)
        servers.load(store.load())
        workpane.capabilities.provide("workspace.folder.serve", view.serveFolder, { summaryKey = "web-server.contract.folder-serve", payload = { type = "object", properties = { path = { type = "string", description = "Absolute path of a readable folder" } }, required = { "path" } } })
        workpane.events.subscribe("terminal.workspace.changed", terminals.changed)
        workpane.events.subscribe("terminal.session.closed", terminals.closed)
        workpane.task(terminals.synchronize)

        -- A Terminal plugin that starts after this one hands over its terminals as soon as it answers.
        workpane.capabilities.watch("terminal.workspace.snapshot", function(available)
            if available then
                terminals.synchronize()
            end
        end)
    end,
}
