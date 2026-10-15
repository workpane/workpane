-- The settings section of the browser, which edits its homepage.
local preferences = include("preferences")

local ui = workpane.ui
local text = workpane.i18n.text

local settings = {}

-- The homepage is written when the reader leaves its field or presses Enter, and an address the browser cannot read puts the stored one back.
function settings.general()
    return ui.settingsForm({}, {
        ui.settingsRow({ label = text("browser.settings.homepage") }, preferences.control("homepage", { width = 360 })),
    })
end

return settings
