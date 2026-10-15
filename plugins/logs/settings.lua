-- The settings section of the log, which empties the stored entries.
local view = include("view")

local ui = workpane.ui
local text = workpane.i18n.text

local settings = {}

function settings.general()
    return ui.settingsForm({}, {
        ui.settingsRow({ label = text("logs.settings.storage") }, ui.button({ text = text("logs.viewer.clear"), icon = "clear", variant = "destructive", onClick = view.clear })),
    })
end

return settings
