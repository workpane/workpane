-- Every color role of the current theme grouped by family, each fill carrying a sample written in the ink made for it, which changes at once when the reader picks another theme.
local gallery = include("views/gallery")
local ui = workpane.ui
local text = workpane.i18n.text

-- A swatch pairs a fill with the ink written on it, and names the fill, the ink or both when both belong to the family.
local families = {
    { key = "surfaces", swatches = { { "window", "text" }, { "panel", "text" }, { "raised", "text" }, { "terminal", "text" }, { "tooltip", "on-tooltip" }, { "overlay", "text" } } },
    { key = "states", swatches = { { "hover", "text" }, { "pressed", "text" }, { "selection", "text" }, { "highlight", "text" }, { "focus", "on-accent" } } },
    { key = "lines", swatches = { { "border", "text" }, { "border-strong", "text" }, { "scrollbar", "text" }, { "scrollbar-hover", "text" }, { "scrollbar-active", "text" } } },
    { key = "text", swatches = { { "window", "text", ink = true }, { "window", "text-muted", ink = true }, { "window", "text-disabled", ink = true }, { "tooltip", "on-tooltip", ink = true }, { "tooltip", "on-tooltip-muted", ink = true } } },
    { key = "accent", swatches = { { "accent", "on-accent", pair = true }, { "accent-hover", "on-accent", pair = true }, { "accent-strong", "on-accent", pair = true }, { "accent-background", "accent-text", pair = true } } },
    { key = "success", swatches = { { "success", "on-success", pair = true }, { "success-background", "success-text", pair = true } } },
    { key = "warning", swatches = { { "warning", "on-warning", pair = true }, { "warning-background", "warning-text", pair = true } } },
    { key = "danger", swatches = { { "danger", "on-danger", pair = true }, { "danger-hover", "on-danger", pair = true }, { "danger-strong", "on-danger", pair = true }, { "danger-background", "danger-text", pair = true } } },
    { key = "information", swatches = { { "information", "on-information", pair = true }, { "information-background", "information-text", pair = true } } },
}

local function swatch(entry)
    local fill, ink = entry[1], entry[2]
    local names = { ui.label({ text = entry.ink and ink or fill, style = "caption", wrap = false }) }

    if entry.pair then
        names[2] = ui.label({ text = ink, style = "caption", color = "text-muted", wrap = false })
    end

    return ui.column({ spacing = 6 }, {
        ui.card({ background = fill, height = 44, radius = 4, padding = 10 }, {
            ui.label({ text = text("components.theme-colors.sample"), color = ink, style = "strong" }),
        }),
        ui.column({ spacing = 2 }, names),
    })
end

return function()
    local sections = {}

    for index, family in ipairs(families) do
        local swatches = {}

        for position, entry in ipairs(family.swatches) do
            swatches[position] = swatch(entry)
        end

        sections[index] = gallery.section("components.theme-colors." .. family.key, {
            ui.grid({ columns = 4, columnSpacing = 12, rowSpacing = 12 }, swatches),
        })
    end

    sections[#sections + 1] = gallery.section("components.theme-colors.themes", {
        ui.label({ text = text("components.theme-colors.themes-text"), style = "muted" }),
    })

    return sections
end
