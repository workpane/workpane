-- Runs the shells of the reader in workspace tabs of tiled terminals, keeps the terminals outside the layout on a shelf and tells other plugins where they stand.
local preferences = include("preferences")
local settings = include("settings")
local store = include("store")
local view = include("view")
local workspace = include("workspace")

local snapshotCapability = "terminal.workspace.snapshot"
local mac = workpane.app.platform == "macos"

-- Another plugin asks for the terminals with an empty request and receives the active one and every terminal with the folder it stands in.
local function snapshot(payload)
    if type(payload) ~= "table" or next(payload) ~= nil then
        error({ code = "terminal_request_invalid", message = "A request for the terminal snapshot carries nothing", detail = "" }, 0)
    end

    return workspace.snapshot()
end

return {
    id = "terminal",
    sdk = 1,
    titleKey = "terminal.plugin.title",
    descriptionKey = "terminal.plugin.description",
    navigation = {
        {
            id = "workspace",
            titleKey = "terminal.navigation.workspace",
            icon = "terminal",
            placement = "primary",
            order = 100,
            -- The terminals run from the moment the product starts, so the view is built before it is first shown.
            preload = true,
            view = view.build,
            -- Control with N or W belongs to the shell outside macOS, so those platforms add shift the way their terminals do.
            shortcuts = {
                { id = "new-terminal", keys = mac and "mod+n" or "mod+shift+n", action = view.createTerminal },
                { id = "new-tab", keys = "mod+shift+t", action = view.createTab },
                { id = "layout", keys = "mod+shift+l", action = view.openLayouts },
                { id = "close-terminal", keys = mac and "mod+w" or "mod+shift+w", action = view.closeFocused },
            },
        },
    },
    settings = {
        { id = "terminal", titleKey = "terminal.plugin.title", sections = {
            { id = "general", titleKey = "terminal.settings.general", searchKeys = { "terminal.settings.font-family", "terminal.settings.font-size", "terminal.settings.color-intensity", "terminal.settings.cursor-blink", "terminal.settings.blink-speed", "terminal.settings.allow-clipboard-write", "terminal.settings.open-links-externally" }, view = settings.general },
        } },
    },
    migrations = store.migrations,
    -- The stored workspace is checked whole before anything is offered, so a broken document keeps the plugin from starting.
    start = function()
        preferences.define()
        workspace.load()
        workspace.listen(view.render)
        preferences.watch(function(value, key)
            view.apply(key, value)
        end)

        workpane.capabilities.provide(snapshotCapability, snapshot, { summaryKey = "terminal.contract.snapshot", payload = { type = "object", properties = {} }, agents = true })

        -- The actions of a terminal header follow the plugins that open and serve folders as they start and stop.
        for _, capability in ipairs({ "workspace.folder.open", "workspace.folder.serve" }) do
            workpane.capabilities.watch(capability, function()
                view.render()
            end)
        end

        workpane.events.publish("terminal.workspace.changed", {})
    end,
}
