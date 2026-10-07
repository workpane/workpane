-- Edits the files of the folders the reader opens, with the language servers found on the machine, and opens a folder for any plugin that asks.
local catalog = include("catalog")
local preferences = include("preferences")
local servers = include("servers")
local settings = include("settings")
local store = include("store")
local view = include("view")

local folderCapability = "workspace.folder.open"

-- A finished search for servers updates the settings table and starts the servers of the documents already open.
local function serversChanged()
    settings.showServers()

    if not servers.searching() then
        view.applyPreferences("languageServers", preferences.get("languageServers"))
    end
end

return {
    id = "code-editor",
    sdk = 1,
    titleKey = "code-editor.plugin.title",
    descriptionKey = "code-editor.plugin.description",
    navigation = {
        {
            id = "editor",
            titleKey = "code-editor.navigation.editor",
            icon = "edit",
            placement = "primary",
            order = 300,
            view = view.build,
            shortcuts = {
                { id = "save", keys = "mod+s", action = view.save },
                { id = "close-document", keys = "mod+w", action = view.closeDocument },
                { id = "find-file", keys = "mod+p", action = view.findFile },
                { id = "definition", keys = "f12", action = view.goToDefinition },
                { id = "references", keys = "shift+f12", action = view.findReferences },
            },
        },
    },
    settings = {
        { id = "editor", titleKey = "code-editor.plugin.title", sections = {
            { id = "appearance", titleKey = "code-editor.settings.appearance", searchKeys = { "code-editor.settings.font-family", "code-editor.settings.font-size", "code-editor.settings.color-scheme", "code-editor.settings.word-wrap", "code-editor.settings.word-wrap-description" }, view = settings.appearance },
            { id = "files", titleKey = "code-editor.settings.files", searchKeys = { "code-editor.settings.default-charset", "code-editor.settings.default-charset-description" }, view = settings.files },
            { id = "language-servers", titleKey = "code-editor.settings.language-servers", searchKeys = { "code-editor.settings.language-servers-enabled", "code-editor.settings.language", "code-editor.settings.executable" }, view = settings.languageServers },
        } },
    },
    migrations = store.migrations,
    -- The catalogs and the stored folders are checked whole before anything is offered, so a broken asset or row keeps the plugin from starting.
    start = function()
        catalog.load()
        preferences.define()
        view.restore(store.load())
        preferences.watch(function(value, key)
            view.applyPreferences(key, value)
        end)

        servers.listen(serversChanged)
        workpane.capabilities.provide(folderCapability, view.folderRequested, { summaryKey = "code-editor.contract.folder-open", payload = { type = "object", properties = { path = { type = "string", description = "Absolute path of a readable folder" } }, required = { "path" } } })
        servers.refresh()
    end,
    stop = function()
        view.stop()
        store.drain()
    end,
}
