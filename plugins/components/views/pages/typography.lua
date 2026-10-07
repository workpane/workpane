-- Every label style, the alignments, wrapping, selectable text, colored text, the page header, section titles and the empty state.
local gallery = include("views/gallery")
local ui = workpane.ui
local text = workpane.i18n.text

return function()
    local styles = {}

    for index, style in ipairs({ "title", "heading", "strong", "body", "muted", "caption", "monospace" }) do
        styles[index] = gallery.sample("components.typography.style-" .. style, ui.label({ text = text("components.typography.sample"), style = style, wrap = false }))
    end

    local colors = {}

    for index, color in ipairs({ "accent", "success", "warning", "danger", "information" }) do
        colors[index] = ui.label({ text = color, color = color, style = "strong", wrap = false })
    end

    return {
        gallery.section("components.typography.styles", styles),
        gallery.section("components.typography.alignment", {
            ui.label({ text = text("components.typography.align-start"), textAlign = "start" }),
            ui.label({ text = text("components.typography.align-center"), textAlign = "center" }),
            ui.label({ text = text("components.typography.align-end"), textAlign = "end" }),
        }),
        gallery.section("components.typography.wrapping", {
            ui.label({ text = text("components.typography.paragraph") }),
            ui.label({ text = text("components.typography.selectable"), selectable = true }),
        }),
        gallery.section("components.typography.colors", {
            ui.row({ spacing = 18 }, colors),
        }),
        gallery.section("components.typography.structure", {
            ui.pageHeader({ title = text("components.typography.header-title"), caption = text("components.typography.header-caption") }, {
                ui.button({ text = text("components.typography.header-action"), icon = "add", variant = "primary" }),
            }),
            ui.sectionTitle({ text = text("components.typography.section-title") }),
            ui.emptyState({ text = text("components.typography.empty"), icon = "search", height = 120 }),
            ui.alert({ text = text("components.typography.alert") }),
            ui.alert({ text = text("components.typography.alert-warning"), tone = "warning" }),
            ui.alert({ text = text("components.typography.alert-success"), tone = "success" }),
            ui.alert({ text = text("components.typography.alert-information"), tone = "information" }),
            ui.alert({ text = text("components.typography.alert-accent"), tone = "accent" }),
            ui.alert({ text = text("components.typography.alert-neutral"), tone = "neutral" }),
        }),
    }
end
