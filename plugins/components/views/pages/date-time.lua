-- Date and date with time fields, each opening the calendar, and a field that starts empty behind its placeholder.
local gallery = include("views/gallery")
local ui = workpane.ui
local text = workpane.i18n.text

return function(report)
    local changed = gallery.reporter(report, "change")

    return {
        gallery.section("components.date-time.fields", {
            gallery.sample("components.date-time.date", ui.dateTimeField({ mode = "date", value = "2026-09-26", width = 240, onChange = changed })),
            gallery.sample("components.date-time.date-time", ui.dateTimeField({ mode = "datetime", value = "2026-09-26 14:30", width = 240, onChange = changed })),
            gallery.sample("components.date-time.empty", ui.dateTimeField({ mode = "date", placeholder = text("components.date-time.placeholder"), width = 240, onChange = changed })),
            gallery.sample("components.date-time.disabled", ui.dateTimeField({ mode = "datetime", value = "2026-01-01 09:00", enabled = false, width = 240 })),
        }),
    }
end
