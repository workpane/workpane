-- The settings section of the adventure, which sizes its band, sets its pace, shows or hides its counters, lets its light follow the clock and starts the knight over.
local preferences = include("preferences")

local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

local settings = {}

local function describe(progress)
    return text("task-hero.settings.progress-value", text("task-hero.class." .. progress.class), progress.level, workpane.i18n.number(progress.gold), workpane.i18n.number(progress.victories))
end

-- Starting over cannot be undone, so the reader confirms it first.
local function restart()
    local confirmed = workpane.await(workpane.dialogs.confirm({
        title = translate("task-hero.settings.restart-title"),
        message = translate("task-hero.settings.restart-message"),
        confirmText = translate("task-hero.settings.restart-action"),
        destructive = true,
    }))

    if confirmed then
        preferences.forget()
    end
end

function settings.general()
    local progress = ui.label({ text = describe(preferences.get("progress")), textAlign = "end" })

    preferences.watch("progress", function(value)
        progress:set({ text = describe(value) })
    end)

    return ui.settingsForm({}, {
        ui.settingsRow({ label = text("task-hero.settings.size") }, preferences.control("size", { labels = { hidden = text("task-hero.size.hidden"), small = text("task-hero.size.small"), medium = text("task-hero.size.medium"), large = text("task-hero.size.large") }, width = 200 })),
        ui.settingsRow({ label = text("task-hero.settings.speed") }, preferences.control("speed", { labels = { slow = text("task-hero.speed.slow"), normal = text("task-hero.speed.normal"), fast = text("task-hero.speed.fast"), faster = text("task-hero.speed.faster") }, width = 200 })),
        ui.settingsRow({ label = text("task-hero.settings.counters") }, preferences.control("counters")),
        ui.settingsRow({ label = text("task-hero.settings.day-night"), hint = text("task-hero.settings.day-night-hint") }, preferences.control("dayNight")),
        ui.settingsRow({ label = text("task-hero.settings.progress") }, progress),
        ui.settingsActions({}, {
            ui.button({ text = text("task-hero.settings.restart"), icon = "refresh", variant = "destructive", onClick = restart }),
        }),
    })
end

return settings
