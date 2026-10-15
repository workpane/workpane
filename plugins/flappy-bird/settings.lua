-- The settings section of the game, which chooses the bird, the background and the sound and forgets the best rounds.
local preferences = include("preferences")
local store = include("store")

local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

local settings = {}

-- Forgetting the best rounds cannot be undone, so the reader confirms it first.
local function reset()
    local confirmed = workpane.await(workpane.dialogs.confirm({
        title = translate("flappy-bird.settings.reset-title"),
        message = translate("flappy-bird.settings.reset-message"),
        confirmText = translate("flappy-bird.settings.reset-action"),
        destructive = true,
    }))

    if not confirmed then
        return
    end

    store.clear()
    workpane.await(preferences.set("best", 0))
end

function settings.general()
    local best = ui.label({ text = text("flappy-bird.settings.best-value", preferences.get("best")) })

    preferences.watch("best", function(value)
        best:set({ text = text("flappy-bird.settings.best-value", value) })
    end)

    return ui.settingsForm({}, {
        ui.settingsRow({ label = text("flappy-bird.settings.bird") }, preferences.control("bird", { labels = { yellow = text("flappy-bird.bird.yellow"), blue = text("flappy-bird.bird.blue"), red = text("flappy-bird.bird.red"), random = text("flappy-bird.bird.random") }, width = 200 })),
        ui.settingsRow({ label = text("flappy-bird.settings.background"), hint = text("flappy-bird.settings.background-hint") }, preferences.control("background", { labels = { day = text("flappy-bird.background.day"), night = text("flappy-bird.background.night"), clock = text("flappy-bird.background.clock") }, width = 200 })),
        ui.settingsRow({ label = text("flappy-bird.settings.sound") }, preferences.control("sound")),
        ui.settingsRow({ label = text("flappy-bird.settings.best") }, best),
        ui.settingsActions({}, {
            ui.button({ text = text("flappy-bird.settings.reset"), icon = "clear", variant = "destructive", onClick = reset }),
        }),
    })
end

return settings
