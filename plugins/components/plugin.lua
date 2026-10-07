-- A gallery of every component the product draws, off until the reader turns it on, so anyone can check each one against the themes on their own machine.
local view = include("view")

return {
    id = "components",
    sdk = 1,
    titleKey = "components.plugin.title",
    descriptionKey = "components.plugin.description",
    offByDefault = true,
    navigation = {
        {
            id = "gallery",
            titleKey = "components.navigation.gallery",
            icon = "components",
            placement = "primary",
            order = 900,
            view = view.build,
            shortcuts = {
                { id = "next", keys = "alt+down", action = function()
                    view.step(1)
                end },
                { id = "previous", keys = "alt+up", action = function()
                    view.step(-1)
                end },
            },
        },
    },
}
