-- The settings section of the web server, which places the split of its page.
local preferences = include("preferences")

local ui = workpane.ui
local text = workpane.i18n.text

local settings = {}

function settings.general()
    return ui.settingsForm({}, {
        ui.settingsRow({ label = text("web-server.settings.splitter-label") }, preferences.control("splitRatio", { step = 10, width = 130 })),
    })
end

return settings
