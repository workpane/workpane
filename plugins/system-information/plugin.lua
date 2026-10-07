-- Shows the machine the product runs on, collected by the host on a worker.
local view = include("view")

return {
    id = "system-information",
    sdk = 1,
    titleKey = "system-information.plugin.title",
    descriptionKey = "system-information.plugin.description",
    navigation = {
        { id = "overview", titleKey = "system-information.navigation.overview", icon = "system", placement = "secondary", order = 850, view = view.build },
    },
}
