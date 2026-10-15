-- The settings section of the terminals, whose controls follow every value in force, including one put back after a write failed.
local preferences = include("preferences")

local ui = workpane.ui
local text = workpane.i18n.text

local settings = {}

local bundledFont = "JetBrains Mono"

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

-- The speed of the blink is chosen only while the cursor blinks, and the control follows the switch for as long as the section holds it.
local function blinkSpeed()
    local speed = preferences.control("blinkSpeed", { labels = { slow = text("terminal.blink.slow"), normal = text("terminal.blink.normal"), fast = text("terminal.blink.fast") }, width = 200, enabled = preferences.get("cursorBlink") })
    local held = setmetatable({ node = speed }, { __mode = "v" })
    local stop

    stop = preferences.watch("cursorBlink", function(value)
        if held.node == nil then
            stop()
            return
        end

        held.node:set({ enabled = value })
    end)

    return speed
end

function settings.general()
    local family = preferences.control("fontFamily", { as = "combo", options = fontOptions({ bundledFont, preferences.get("fontFamily") }), sorted = true, width = 220 })

    -- The families of the machine join the choices once they were listed, so the section opens without waiting for them.
    workpane.task(function()
        local installed = workpane.await(workpane.system.monospaceFonts())
        table.insert(installed, 1, preferences.get("fontFamily"))
        table.insert(installed, 1, bundledFont)
        family:set({ options = fontOptions(installed) })
    end)

    return ui.settingsForm({}, {
        ui.settingsRow({ label = text("terminal.settings.font-family") }, family),
        ui.settingsRow({ label = text("terminal.settings.font-size") }, preferences.control("fontSize", { width = 130 })),
        ui.settingsRow({ label = text("terminal.settings.color-intensity") }, preferences.control("palette", { labels = { vivid = text("terminal.palette.vivid"), balanced = text("terminal.palette.balanced"), soft = text("terminal.palette.soft") }, width = 200 })),
        ui.settingsRow({ label = text("terminal.settings.cursor-blink") }, preferences.control("cursorBlink")),
        ui.settingsRow({ label = text("terminal.settings.blink-speed") }, blinkSpeed()),
        ui.settingsRow({ label = text("terminal.settings.allow-clipboard-write") }, preferences.control("clipboardWrites")),
        ui.settingsRow({ label = text("terminal.settings.open-links-externally") }, preferences.control("openLinksExternally")),
    })
end

return settings
