-- Flappy Bird drawn on a canvas with the sprites and sounds of the original game, keeping the best score and the ten best rounds.
local preferences = include("preferences")
local settings = include("settings")
local store = include("store")
local view = include("view")

return {
    id = "flappy-bird",
    sdk = 1,
    titleKey = "flappy-bird.plugin.title",
    descriptionKey = "flappy-bird.plugin.description",
    offByDefault = true,
    navigation = {
        { id = "game", titleKey = "flappy-bird.plugin.title", icon = "bird", placement = "secondary", order = 700, view = view.build },
    },
    settings = {
        { id = "flappy-bird", titleKey = "flappy-bird.plugin.title", sections = {
            { id = "general", titleKey = "flappy-bird.settings.general", searchKeys = { "flappy-bird.settings.bird", "flappy-bird.settings.background", "flappy-bird.settings.sound", "flappy-bird.settings.best" }, view = settings.general },
        } },
    },
    migrations = store.migrations,
    start = function()
        preferences.define()
    end,
}
