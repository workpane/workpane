-- The settings the core owns for the whole product: the language, the theme, the version, moving the configuration in and out, the plugins that run and the folders they are loaded from.
local api = require("workpane.api")
local bridge = require("workpane.bridge")
local plugins = require("workpane.plugins")
local ui = require("workpane.ui")

local application = {}

local owner = "workpane"
local workpane
local store
local info

local text = api.text

local transferButtons = {}
local transferBusy = false
local folderControls = {}
local pluginRows = {}
local removedControls = {}
local storedData = {}
local startedFolders = {}
local largestPluginFolders = 32
local largestPluginSwitches = 256

local stateTones = { discovered = "neutral", starting = "information", running = "success", stopping = "information", disabled = "neutral", waiting = "warning", refused = "danger", unavailable = "neutral" }
local settled = { running = true, waiting = true, disabled = true, refused = true }
local erasable = { waiting = true, disabled = true, refused = true, unavailable = true }

-- A failure is notified under the name of its section with the reason as its message.
local function report(titleKey, messageKey, category, failure)
    workpane.notify.error(workpane.i18n.translate(titleKey), workpane.i18n.translate(messageKey))
    workpane.log.error(category, tostring(failure), { code = bridge.failure(failure).code })
end

-- An import or an export holds both buttons until it ends, and one asked for meanwhile is ignored, so two transfers never run together.
local function hold()
    if transferBusy then
        return false
    end

    transferBusy = true

    for _, button in ipairs(transferButtons) do
        button:set({ enabled = false })
    end

    return true
end

local function release()
    transferBusy = false

    for _, button in ipairs(transferButtons) do
        button:set({ enabled = true })
    end
end

-- A change of language or theme applies at once, and a write that fails puts the committed value back in the shell as the store puts it back in the combo.
local function change(key, value, select, failureKey)
    local previous = store:get(key)

    if previous == value then
        return
    end

    select(value)
    local _, failure = store:set(key, value):await()

    if failure ~= nil then
        select(previous)
        report("workpane.application.title", failureKey, "preferences", failure)
    end
end

local function selectLanguage(language)
    bridge.call("workpane_language_select", { language = language })
end

local function selectTheme(theme)
    bridge.call("workpane_theme_select", { theme = theme })
end

local function general()
    local languages = {}
    local themes = {}

    for _, language in ipairs(info.languages) do
        languages[language.id] = text(language.titleKey)
    end

    for _, theme in ipairs(info.themes) do
        themes[theme.id] = text(theme.titleKey)
    end

    local languageCombo = store:control("language", { labels = languages, sorted = true })
    local themeCombo = store:control("theme", { labels = themes, sorted = true })

    -- The combos write through the store, and the shell follows the value the store settles on.
    languageCombo:on("change", function(event)
        change("language", event.value, selectLanguage, "workpane.application.language-save-error")
    end)

    themeCombo:on("change", function(event)
        change("theme", event.value, selectTheme, "workpane.application.theme-save-error")
    end)

    return workpane.ui.settingsForm({}, {
        workpane.ui.settingsRow({ label = text("workpane.application.language") }, languageCombo),
        workpane.ui.settingsRow({ label = text("workpane.application.theme") }, themeCombo),
        workpane.ui.settingsRow({ label = text("workpane.application.version") }, workpane.ui.label({ text = info.version })),
    })
end

local function filters()
    return { { name = workpane.i18n.translate("workpane.configuration.file-filter"), patterns = { "*.sqlite3" } } }
end

-- The snapshot is written where the reader chose, and a success is said as clearly as a failure.
local function exportConfiguration()
    local path = workpane.await(workpane.dialogs.saveFile({ title = workpane.i18n.translate("workpane.configuration.export-title"), initial = "workpane-configuration.sqlite3", filters = filters() }))

    if path == nil then
        return
    end

    if not hold() then
        return
    end

    local _, failure = bridge.request("workpane_configuration_export", { plugin = owner, path = path }):await()
    release()

    if failure ~= nil then
        report("workpane.configuration.title", "workpane.configuration.export-error", "configuration", failure)
        return
    end

    workpane.notify.success(workpane.i18n.translate("workpane.configuration.export-success"), path)
end

-- An import replaces every setting of every plugin, so it is confirmed first and the product restarts once the file is staged.
local function importConfiguration()
    local paths = workpane.await(workpane.dialogs.openFile({ title = workpane.i18n.translate("workpane.configuration.import-title"), filters = filters() }))

    if paths == nil or paths[1] == nil then
        return
    end

    local confirmed = workpane.await(workpane.dialogs.confirm({
        title = workpane.i18n.translate("workpane.configuration.confirm-title"),
        message = workpane.i18n.translate("workpane.configuration.confirm-message"),
        detail = workpane.i18n.translate("workpane.configuration.confirm-detail"),
        confirmText = workpane.i18n.translate("workpane.configuration.confirm-action"),
        destructive = true,
    }))

    if not confirmed then
        return
    end

    if not hold() then
        return
    end

    local _, failure = bridge.request("workpane_configuration_import", { plugin = owner, path = paths[1], schemas = plugins.schemas() }):await()
    release()

    if failure ~= nil then
        report("workpane.configuration.title", "workpane.configuration.import-error", "configuration", failure)
        return
    end

    bridge.call("workpane_restart", {})
end

local function configuration()
    transferButtons = {
        workpane.ui.button({ text = text("workpane.configuration.import"), icon = "import", onClick = importConfiguration }),
        workpane.ui.button({ text = text("workpane.configuration.export"), icon = "export", onClick = exportConfiguration }),
    }

    return workpane.ui.settingsForm({}, {
        workpane.ui.settingsActions({}, transferButtons),
    })
end

local function sameFolders(first, second)
    if #first ~= #second then
        return false
    end

    for index, folder in ipairs(first) do
        if second[index] ~= folder then
            return false
        end
    end

    return true
end

-- The name of a plugin in the language of the reader, or its identifier when its catalog could not be installed.
local function nameOf(plugin)
    return plugin.titleKey ~= nil and text(plugin.titleKey) or plugin.id
end

-- The name of a plugin written out now, for a sentence of a dialog or a cell that is read once.
local function writtenName(plugin, id)
    if plugin == nil or plugin.titleKey == nil then
        return id
    end

    return workpane.i18n.translate(plugin.titleKey)
end

local function findPlugin(id)
    for _, plugin in ipairs(plugins.list()) do
        if plugin.id == id then
            return plugin
        end
    end

    return nil
end

-- The hint under a plugin is its description while it runs, rests or changes state, and the reason it does not run otherwise.
local function hintOf(plugin)
    if plugin.state == "waiting" and plugin.failure ~= nil then
        local dependency = findPlugin(plugin.failure.detail)
        return text("workpane.plugins.waiting", dependency ~= nil and nameOf(dependency) or plugin.failure.detail)
    end

    if plugin.state == "refused" then
        return text("workpane.plugins.refused")
    end

    if plugin.state == "unavailable" then
        return text("workpane.plugins.unavailable")
    end

    return plugin.descriptionKey ~= nil and text(plugin.descriptionKey) or text("workpane.plugins.no-description")
end

-- The switch of a plugin follows what the reader chose, and it waits while any change of the plugins runs or while the plugin has not settled, so a press never meets a change halfway.
local function switchable(plugin)
    return settled[plugin.state] == true and not plugins.changing()
end

-- A plugin that does not run and still keeps data offers to erase it beside its switch.
local function renderPlugins()
    for _, plugin in ipairs(plugins.list()) do
        local row = pluginRows[plugin.id]

        if row ~= nil then
            row.row:set({ hint = hintOf(plugin) })
            row.badge:set({ text = text("workpane.plugins.state-" .. plugin.state), tone = stateTones[plugin.state] })
            row.toggle:set({ checked = not plugin.disabled, enabled = switchable(plugin) })
            row.erase:set({ visible = erasable[plugin.state] == true and storedData[plugin.id] == true, enabled = not plugins.changing() })
        end
    end
end

-- The position the reader gave a switch is kept, so a plugin stays as the reader left it after a restart whatever it declares by default.
local function remember(id, on)
    local switches = store:get("pluginSwitches")
    switches[id] = on
    store:set("pluginSwitches", switches)
end

-- The reader turns a plugin off at once, confirming first when running plugins depend on it, since they stop too.
local function disablePlugin(plugin)
    local dependents = plugins.dependents(plugin.id)

    if #dependents > 0 then
        local names = {}

        for index, id in ipairs(dependents) do
            names[index] = writtenName(findPlugin(id), id)
        end

        local confirmed = workpane.await(workpane.dialogs.confirm({
            title = workpane.i18n.translate("workpane.plugins.disable-title"),
            message = workpane.i18n.translate("workpane.plugins.disable-message", table.concat(names, ", ")),
            confirmText = workpane.i18n.translate("workpane.plugins.disable-action"),
            destructive = true,
        }))

        if not confirmed then
            renderPlugins()
            return
        end
    end

    remember(plugin.id, false)
    local _, failure = plugins.disable(plugin.id):await()

    if failure ~= nil then
        report("workpane.plugins.title", "workpane.plugins.toggle-error", "plugins", failure)
    end
end

local function enablePlugin(plugin)
    remember(plugin.id, true)
    local _, failure = plugins.enable(plugin.id):await()

    if failure ~= nil then
        report("workpane.plugins.title", "workpane.plugins.toggle-error", "plugins", failure)
    end
end

local function pluginRow(plugin, eraseData)
    local badge = workpane.ui.badge({ text = text("workpane.plugins.state-" .. plugin.state), tone = stateTones[plugin.state] })
    local toggle = workpane.ui.toggle({ checked = not plugin.disabled, enabled = switchable(plugin), onChange = function(event)
        if event.checked then
            enablePlugin(plugin)
        else
            disablePlugin(plugin)
        end
    end })

    local erase = workpane.ui.button({ icon = "trash-2", variant = "icon", tooltip = text("workpane.plugins.erase-data"), visible = false, onClick = function()
        eraseData(plugin.id)
    end })

    -- The folder a plugin was read from rests in the tooltip of its row, so the list stays clean.
    local row = workpane.ui.settingsRow({ label = nameOf(plugin), hint = hintOf(plugin), tooltip = plugin.directory }, workpane.ui.row({ spacing = 10 }, { badge, toggle, erase }))
    pluginRows[plugin.id] = { row = row, badge = badge, toggle = toggle, erase = erase }

    return row
end

-- The stored data is read again after every change of the plugins, so the erase buttons and the removed plugins follow what the database holds.
local function renderData()
    if removedControls.table == nil then
        return
    end

    local stored, failure = plugins.stored():await()

    if failure ~= nil then
        report("workpane.plugins.title", "workpane.plugins.data-error", "plugins", failure)
        return
    end

    local rows = {}
    storedData = {}

    for _, id in ipairs(stored) do
        storedData[id] = true

        if findPlugin(id) == nil then
            rows[#rows + 1] = { id = id, cells = { { text = id, monospace = true } }, actions = { { id = "erase", icon = "trash-2", tooltip = text("workpane.plugins.erase-data"), destructive = true } } }
        end
    end

    removedControls.table:set({ rows = rows })

    for _, control in ipairs({ removedControls.title, removedControls.hint, removedControls.table }) do
        control:set({ visible = #rows > 0 })
    end

    renderPlugins()
end

local function eraseData(id)
    local plugin = findPlugin(id)
    local confirmed = workpane.await(workpane.dialogs.confirm({
        title = workpane.i18n.translate("workpane.plugins.erase-title"),
        message = workpane.i18n.translate("workpane.plugins.erase-message", writtenName(plugin, id)),
        confirmText = workpane.i18n.translate("workpane.plugins.erase-action"),
        destructive = true,
    }))

    if not confirmed then
        return
    end

    local _, failure = plugins.erase(id):await()

    if failure ~= nil then
        report("workpane.plugins.title", "workpane.plugins.erase-error", "plugins", failure)
    end

    renderData()
end

-- The table lists the added folders, the empty state stands in for an empty list, and the alert asks for a restart while the folders differ from the ones the product started with.
local function renderFolders()
    if folderControls.table == nil then
        return
    end

    local folders = store:get("pluginFolders")
    local rows = {}

    for index, folder in ipairs(folders) do
        rows[index] = { id = folder, cells = { { text = folder, monospace = true } }, actions = { { id = "remove", icon = "close", tooltip = text("workpane.plugins.remove"), destructive = true } } }
    end

    folderControls.table:set({ rows = rows, visible = #folders > 0 })
    folderControls.empty:set({ visible = #folders == 0 })
    folderControls.pending:set({ visible = not sameFolders(folders, startedFolders) })
end

local function saveFolders(folders)
    local _, failure = store:set("pluginFolders", folders):await()

    if failure ~= nil then
        report("workpane.plugins.title", "workpane.plugins.save-error", "preferences", failure)
    end
end

-- A folder already in the list changes nothing when it is chosen again.
local function addFolder()
    local folder = workpane.await(workpane.dialogs.selectFolder({ title = workpane.i18n.translate("workpane.plugins.add-title") }))
    local folders = {}

    if folder == nil then
        return
    end

    for index, existing in ipairs(store:get("pluginFolders")) do
        if existing == folder then
            return
        end

        folders[index] = existing
    end

    folders[#folders + 1] = folder
    saveFolders(folders)
end

local function removeFolder(folder)
    local folders = {}

    for _, existing in ipairs(store:get("pluginFolders")) do
        if existing ~= folder then
            folders[#folders + 1] = existing
        end
    end

    saveFolders(folders)
end

-- A restart stops every plugin, so the reader confirms it first.
local function restart()
    local confirmed = workpane.await(workpane.dialogs.confirm({
        title = workpane.i18n.translate("workpane.plugins.restart-title"),
        message = workpane.i18n.translate("workpane.plugins.restart-message"),
        confirmText = workpane.i18n.translate("workpane.plugins.restart"),
        destructive = true,
    }))

    if confirmed then
        bridge.call("workpane_restart", {})
    end
end

local function installedSection()
    local children = { workpane.ui.label({ text = text("workpane.plugins.installed-hint"), style = "muted", wrap = true }) }

    for _, plugin in ipairs(plugins.list()) do
        children[#children + 1] = pluginRow(plugin, eraseData)
    end

    removedControls.title = workpane.ui.sectionTitle({ text = text("workpane.plugins.removed"), visible = false })
    removedControls.hint = workpane.ui.label({ text = text("workpane.plugins.removed-hint"), style = "muted", wrap = true, visible = false })
    removedControls.table = workpane.ui.table({ columns = { { id = "plugin", title = text("workpane.plugins.removed-plugin"), width = "stretch" } }, rows = {}, selection = "subtle", maxHeight = 160, visible = false, onAction = function(event)
        eraseData(event.id)
    end })

    children[#children + 1] = removedControls.title
    children[#children + 1] = removedControls.hint
    children[#children + 1] = removedControls.table
    workpane.task(renderData)

    return workpane.ui.settingsForm({}, children)
end

local function foldersSection()
    folderControls.table = workpane.ui.table({ columns = { { id = "folder", title = text("workpane.plugins.folder"), width = "stretch" } }, rows = {}, selection = "subtle", height = 200, onAction = function(event)
        removeFolder(event.id)
    end })

    folderControls.empty = workpane.ui.emptyState({ text = text("workpane.plugins.empty"), icon = "folder" })
    folderControls.pending = workpane.ui.alert({ text = text("workpane.plugins.pending"), tone = "information" })
    renderFolders()

    return workpane.ui.settingsForm({}, {
        workpane.ui.label({ text = text("workpane.plugins.folders-hint"), style = "muted", wrap = true }),
        folderControls.pending,
        folderControls.table,
        folderControls.empty,
        workpane.ui.settingsActions({}, {
            workpane.ui.button({ text = text("workpane.plugins.add"), icon = "folder", onClick = addFolder }),
            workpane.ui.button({ text = text("workpane.plugins.restart"), icon = "refresh", onClick = restart }),
        }),
    })
end

local sections = {
    application = { general = general, configuration = configuration },
    plugins = { installed = installedSection, folders = foldersSection },
}

-- Declares the core settings groups, Application and Plugins, through the same contract every plugin uses, without representing the core as a plugin.
function application.register(applicationInfo, resources)
    info = applicationInfo
    workpane = api.create(owner, resources, info)
    local languageIds = {}
    local themeIds = {}

    local generalKeys = { "workpane.application.language", "workpane.application.theme", "workpane.application.version" }

    -- The general section is found by the names of the languages and themes it offers as well as by its own labels.
    for index, language in ipairs(info.languages) do
        languageIds[index] = language.id
        generalKeys[#generalKeys + 1] = language.titleKey
    end

    for index, theme in ipairs(info.themes) do
        themeIds[index] = theme.id
        generalKeys[#generalKeys + 1] = theme.titleKey
    end

    store = workpane.preferences.define({
        language = { type = "string", default = info.language, choices = languageIds },
        theme = { type = "string", default = themeIds[1], choices = themeIds },
        pluginFolders = { type = "list", items = { type = "string", check = workpane.files.absolute }, maxItems = largestPluginFolders, unique = true },
        pluginSwitches = { type = "map", values = { type = "boolean" }, maxEntries = largestPluginSwitches },
    })

    for index = 2, #info.paths.plugins do
        startedFolders[#startedFolders + 1] = info.paths.plugins[index]
    end

    store:watch("pluginFolders", renderFolders)
    plugins.watch(function()
        renderPlugins()
        workpane.task(renderData)
    end)

    bridge.call("workpane_core_settings", {
        settings = {
            {
                id = "application",
                titleKey = "workpane.application.title",
                sections = {
                    { id = "general", titleKey = "workpane.application.general", searchKeys = generalKeys },
                    { id = "configuration", titleKey = "workpane.configuration.title", searchKeys = { "workpane.configuration.import", "workpane.configuration.export" } },
                },
            },
            {
                id = "plugins",
                titleKey = "workpane.plugins.title",
                sections = {
                    { id = "installed", titleKey = "workpane.plugins.installed", searchKeys = { "workpane.plugins.title", "workpane.plugins.removed", "workpane.plugins.erase-data" } },
                    { id = "folders", titleKey = "workpane.plugins.folders", searchKeys = { "workpane.plugins.title", "workpane.plugins.add", "workpane.plugins.restart" } },
                },
            },
        },
    })
end

-- The position the reader gave the switch of each plugin, on or off, which the product follows over what a plugin declares by default.
function application.pluginSwitches()
    return store:get("pluginSwitches")
end

function application.section(groupId, sectionId, surface)
    local group = sections[groupId]
    local build = group ~= nil and group[sectionId] or nil

    if build == nil then
        bridge.raise("settings_section_unknown", "The core declares no such settings section", tostring(groupId) .. ":" .. tostring(sectionId))
    end

    ui.mount(owner, surface, build())
end

return application
