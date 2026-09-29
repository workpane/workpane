-- The settings sections of the editor, whose controls follow every value in force, including one put back after a write failed.
local catalog = include("catalog")
local encoding = include("encoding")
local preferences = include("preferences")
local servers = include("servers")

local ui = workpane.ui
local text = workpane.i18n.text

local settings = {}

local bundledFont = "JetBrains Mono"
local serverControls = {}

-- The families offered are the bundled one, the one in force and every monospaced family of the machine, each once.
local function fontOptions(families)
    local options = {}
    local seen = {}

    for _, family in ipairs(families) do
        if not seen[family] then
            seen[family] = true
            options[#options + 1] = { value = family, text = family }
        end
    end

    return options
end

function settings.appearance()
    local schemes = {}

    for index, scheme in ipairs(catalog.schemes()) do
        schemes[index] = { value = scheme.id, text = scheme.name }
    end

    local family = preferences.control("fontFamily", { as = "combo", options = fontOptions({ bundledFont, preferences.get("fontFamily") }), sorted = true, width = 220 })

    -- The families of the machine join the choices once they were listed, so the section opens without waiting for them.
    workpane.task(function()
        local installed = workpane.await(workpane.system.monospaceFonts())
        table.insert(installed, 1, preferences.get("fontFamily"))
        table.insert(installed, 1, bundledFont)
        family:set({ options = fontOptions(installed) })
    end)

    return ui.settingsForm({}, {
        ui.settingsRow({ label = text("code-editor.settings.font-family") }, family),
        ui.settingsRow({ label = text("code-editor.settings.font-size") }, preferences.control("fontSize", { width = 130 })),
        ui.settingsRow({ label = text("code-editor.settings.color-scheme") }, preferences.control("colorScheme", { as = "combo", options = schemes, sorted = true, width = 220 })),
        ui.settingsRow({ label = text("code-editor.settings.word-wrap"), hint = text("code-editor.settings.word-wrap-description") }, preferences.control("wordWrap")),
    })
end

function settings.files()
    local charsets = {}

    for index, charset in ipairs(encoding.charsets()) do
        charsets[index] = { value = charset, text = charset }
    end

    return ui.settingsForm({}, {
        ui.settingsRow({ label = text("code-editor.settings.default-charset"), hint = text("code-editor.settings.default-charset-description") }, preferences.control("defaultCharset", { as = "combo", options = charsets, sorted = true, width = 220 })),
    })
end

-- Every language the catalog names a server for is listed with the executable found for it, and Refresh looks again.
function settings.showServers()
    if serverControls.table == nil then
        return
    end

    local rows = {}

    for index, entry in ipairs(catalog.servers()) do
        local found = servers.executable(entry.language)
        rows[index] = { id = entry.language, cells = { catalog.language(entry.language).name, found ~= nil and { text = found.path, monospace = true } or { text = text("code-editor.settings.not-found"), muted = true } } }
    end

    serverControls.table:set({ rows = rows })
    serverControls.refresh:set({ enabled = not servers.searching() })
end

function settings.languageServers()
    serverControls.table = ui.table({ columns = {
        { id = "language", title = text("code-editor.settings.language"), width = 200 },
        { id = "executable", title = text("code-editor.settings.executable"), width = "stretch" },
    }, rows = {}, selection = "subtle", height = 280 })
    serverControls.refresh = ui.button({ text = text("code-editor.actions.refresh"), icon = "refresh", onClick = servers.refresh })
    settings.showServers()

    return ui.settingsForm({}, {
        ui.settingsRow({ label = text("code-editor.settings.language-servers-enabled"), hint = text("code-editor.settings.language-servers-enabled-description") }, preferences.control("languageServers")),
        serverControls.table,
        ui.settingsActions({}, { serverControls.refresh }),
    })
end

return settings
