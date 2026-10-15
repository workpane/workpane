local view = include("view")

return {
    id = "studio",
    sdk = 6,
    titleKey = "studio.plugin.title",
    descriptionKey = "studio.plugin.description",
    navigation = {
        { id = "home", titleKey = "studio.navigation.home", icon = "home", placement = "primary", order = 100, view = view.build },
    },
}
