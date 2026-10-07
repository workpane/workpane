-- An adventure that runs by itself in a band across the bottom of the window, where a knight of the Tiny Swords pack walks, fights, gathers gold and grows stronger.
local preferences = include("preferences")
local settings = include("settings")
local view = include("view")

local band = "adventure"

return {
    id = "task-hero",
    sdk = 1,
    titleKey = "task-hero.plugin.title",
    descriptionKey = "task-hero.plugin.description",
    offByDefault = true,
    bands = {
        { id = band, titleKey = "task-hero.band.title", placement = "bottom", order = 100, height = 88, view = view.build },
    },
    settings = {
        { id = "task-hero", titleKey = "task-hero.plugin.title", sections = {
            { id = "general", titleKey = "task-hero.settings.general", searchKeys = { "task-hero.settings.size", "task-hero.settings.speed", "task-hero.settings.counters", "task-hero.settings.day-night", "task-hero.settings.progress" }, view = settings.general },
        } },
    },
    -- The band takes the height the reader chose and follows every later choice.
    start = function()
        preferences.define()
        workpane.shell.resizeBand(band, preferences.height())

        preferences.watch("size", function()
            workpane.shell.resizeBand(band, preferences.height())
        end)
    end,
    stop = view.stop,
}
