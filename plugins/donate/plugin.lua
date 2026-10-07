-- Offers the reader the ways to support the maintainer of Workpane.
local view = include("view")

return {
    id = "donate",
    sdk = 1,
    titleKey = "donate.plugin.title",
    descriptionKey = "donate.plugin.description",
    navigation = {
        { id = "support", titleKey = "donate.navigation.support", icon = "donate", placement = "secondary", order = 900, view = view.build },
    },
}
