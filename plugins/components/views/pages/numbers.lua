-- Number fields, sliders and progress bars, with one slider driving a progress bar to show a live patch.
local gallery = include("views/gallery")
local ui = workpane.ui
local text = workpane.i18n.text

return function(report)
    local changed = gallery.reporter(report, "change")
    local linked = ui.progress({ value = 0.4, showText = true, width = 320 })

    local function slid(event)
        linked:set({ value = event.value / 100 })
        report("change", event)
    end

    return {
        gallery.section("components.numbers.fields", {
            gallery.sample("components.numbers.integer", ui.numberField({ value = 42, minimum = 0, maximum = 100, step = 1, width = 180, onChange = changed })),
            gallery.sample("components.numbers.decimal", ui.numberField({ value = 1.25, minimum = 0, maximum = 10, step = 0.25, decimals = 2, width = 180, onChange = changed })),
            gallery.sample("components.numbers.disabled", ui.numberField({ value = 7, enabled = false, width = 180 })),
        }),
        gallery.section("components.numbers.sliders", {
            gallery.sample("components.numbers.slider", ui.slider({ value = 40, minimum = 0, maximum = 100, step = 1, showValue = true, width = 320, onChange = slid })),
            gallery.sample("components.numbers.slider-stepped", ui.slider({ value = 4, minimum = 0, maximum = 10, step = 2, showValue = true, width = 320, onChange = changed })),
            gallery.sample("components.numbers.slider-decimal", ui.slider({ value = 0.5, minimum = 0, maximum = 1, step = 0.05, decimals = 2, showValue = true, width = 320, onChange = changed })),
            gallery.sample("components.numbers.slider-disabled", ui.slider({ value = 60, minimum = 0, maximum = 100, enabled = false, width = 320 })),
        }),
        gallery.section("components.numbers.progress", {
            gallery.sample("components.numbers.progress-linked", linked),
            gallery.sample("components.numbers.progress-success", ui.progress({ value = 1, tone = "success", showText = true, width = 320 })),
            gallery.sample("components.numbers.progress-warning", ui.progress({ value = 0.7, tone = "warning", width = 320 })),
            gallery.sample("components.numbers.progress-danger", ui.progress({ value = 0.15, tone = "danger", text = text("components.numbers.progress-failed"), showText = true, width = 320 })),
        }),
    }
end
