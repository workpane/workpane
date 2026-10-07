-- Browses the web in movable tabs on the native web view of the platform, with ordered bookmarks and a session that survives a restart.
local address = include("address")
local bookmarks = include("bookmarks")
local preferences = include("preferences")
local session = include("session")
local settings = include("settings")
local store = include("store")
local view = include("view")

-- Another plugin asks for an address to be read here, so it opens in a new active tab and the browser comes on screen.
local function openPage(payload)
    local keys = 0

    for _ in pairs(type(payload) == "table" and payload or {}) do
        keys = keys + 1
    end

    if type(payload) ~= "table" or type(payload.url) ~= "string" or keys ~= 1 then
        error({ code = "browser_request_invalid", message = "A request to open a page carries exactly its address", detail = "" }, 0)
    end

    local normalized = address.normalize(payload.url)

    if normalized == nil then
        error({ code = "browser_address_invalid", message = "The address is not one the browser opens", detail = payload.url }, 0)
    end

    local id = session.create(normalized, true)
    view.render()
    workpane.shell.navigate("web")

    return { tabId = id }
end

return {
    id = "browser",
    sdk = 1,
    titleKey = "browser.plugin.title",
    descriptionKey = "browser.plugin.description",
    navigation = {
        {
            id = "web",
            titleKey = "browser.navigation.web",
            icon = "browser",
            placement = "primary",
            order = 200,
            view = view.build,
            shortcuts = {
                { id = "new-tab", keys = "mod+t", action = view.newTab },
                { id = "close-tab", keys = "mod+w", action = view.closeActive },
                { id = "close-tab-function", keys = "mod+f4", action = view.closeActive },
                { id = "next-tab", keys = "mod+tab", action = function()
                    view.step(1)
                end },
                { id = "previous-tab", keys = "mod+shift+tab", action = function()
                    view.step(-1)
                end },
                { id = "next-tab-page", keys = "mod+pagedown", action = function()
                    view.step(1)
                end },
                { id = "previous-tab-page", keys = "mod+pageup", action = function()
                    view.step(-1)
                end },
                { id = "next-tab-bracket", keys = "mod+shift+rightbracket", action = function()
                    view.step(1)
                end },
                { id = "previous-tab-bracket", keys = "mod+shift+leftbracket", action = function()
                    view.step(-1)
                end },
                { id = "reload", keys = "mod+r", action = function()
                    view.command("reload")
                end },
                { id = "refresh", keys = "f5", action = function()
                    view.command("reload")
                end },
            },
        },
    },
    settings = {
        { id = "browser", titleKey = "browser.plugin.title", sections = {
            { id = "general", titleKey = "browser.settings.general", searchKeys = { "browser.settings.homepage" }, view = settings.general },
        } },
    },
    migrations = store.migrations,
    -- The stored session and bookmarks are checked whole before anything is offered, so a broken row keeps the plugin from starting.
    start = function()
        preferences.define()
        session.restore(preferences.homepage())
        bookmarks.restore()
        workpane.capabilities.provide("workspace.page.open", openPage, { summaryKey = "browser.contract.page-open", payload = { type = "object", properties = { url = { type = "string", description = "Address of the page to open" } }, required = { "url" } }, agents = true })
    end,
}
