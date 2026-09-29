-- The color field with its picker, and a swatch that follows the color the reader chooses.
local gallery = include("views/gallery")
local ui = workpane.ui
local text = workpane.i18n.text

return function(report)
    local chosen = ui.label({ text = "#2e7d32", style = "monospace" })

    local function changed(event)
        chosen:set({ text = event.value })
        report("change", event)
    end

    return {
        gallery.section("components.color.fields", {
            gallery.sample("components.color.field", ui.colorField({ value = "#2E7D32", width = 200, onChange = changed })),
            gallery.sample("components.color.chosen", chosen),
            gallery.sample("components.color.disabled", ui.colorField({ value = "#1565C0", enabled = false, width = 200 })),
        }),
        gallery.section("components.color.usage", {
            ui.label({ text = text("components.color.usage-text"), style = "muted" }),
        }),
    }
end
