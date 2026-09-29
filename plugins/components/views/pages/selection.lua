-- Check boxes, switches, radio groups and combo boxes, enabled and disabled.
local gallery = include("views/gallery")
local ui = workpane.ui
local text = workpane.i18n.text

-- Programming languages are proper names, so they read the same in every language and are written literally.
local languages = {
    { value = "lua", text = "Lua" },
    { value = "cpp", text = "C++" },
    { value = "python", text = "Python" },
    { value = "rust", text = "Rust" },
    { value = "go", text = "Go" },
}

return function(report)
    local changed = gallery.reporter(report, "change")

    return {
        gallery.section("components.selection.checks", {
            ui.checkbox({ text = text("components.selection.remember"), checked = true, onChange = changed }),
            ui.checkbox({ text = text("components.selection.notify"), onChange = changed }),
            ui.checkbox({ text = text("components.selection.disabled"), checked = true, enabled = false }),
        }),
        gallery.section("components.selection.toggles", {
            gallery.sample("components.selection.toggle-on", ui.toggle({ checked = true, onChange = changed })),
            gallery.sample("components.selection.toggle-off", ui.toggle({ checked = false, onChange = changed })),
            gallery.sample("components.selection.toggle-disabled", ui.toggle({ checked = true, enabled = false })),
        }),
        gallery.section("components.selection.radios", {
            ui.radioGroup({ value = "lua", options = languages, onChange = changed }),
            ui.radioGroup({ value = "cpp", options = languages, orientation = "horizontal", onChange = changed }),
            ui.radioGroup({ value = "go", options = languages, orientation = "horizontal", enabled = false }),
        }),
        gallery.section("components.selection.combos", {
            gallery.sample("components.selection.combo", ui.combo({ value = "lua", options = languages, width = 260, onChange = changed })),
            gallery.sample("components.selection.combo-sorted", ui.combo({ options = languages, sorted = true, placeholder = text("components.selection.combo-placeholder"), width = 260, onChange = changed })),
            gallery.sample("components.selection.combo-disabled", ui.combo({ value = "python", options = languages, enabled = false, width = 260 })),
        }),
    }
end
