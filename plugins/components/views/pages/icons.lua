-- Every icon the product names under its name, in the text color and in the accent, and a few of the other glyphs of the Lucide face any plugin reaches by their Lucide names.
local gallery = include("views/gallery")
local ui = workpane.ui

return function()
    local tiles = {}

    for index, name in ipairs(workpane.app.icons) do
        tiles[index] = ui.card({ padding = 10, spacing = 8 }, {
            ui.row({ spacing = 10, justify = "center" }, {
                ui.icon({ name = name, size = 16 }),
                ui.icon({ name = name, size = 24, color = "accent" }),
            }),
            ui.label({ text = name, style = "caption", textAlign = "center", wrap = false }),
        })
    end

    local lucide = {}

    for index, name in ipairs({ "bird", "swords", "castle", "crown", "trophy", "gamepad-2", "puzzle", "rocket" }) do
        lucide[index] = ui.column({ spacing = 6 }, {
            ui.icon({ name = name, size = 24, color = "accent", align = "center" }),
            ui.label({ text = name, style = "caption", textAlign = "center", wrap = false }),
        })
    end

    return {
        gallery.section("components.icons.catalog", {
            ui.grid({ columns = 6, columnSpacing = 8, rowSpacing = 8 }, tiles),
        }),
        gallery.section("components.icons.lucide", {
            ui.row({ spacing = 24 }, lucide),
        }),
    }
end
