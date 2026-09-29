-- The product notifications in every severity, and a burst that shows the overlay keeping only the newest ones.
local gallery = include("views/gallery")
local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

return function(report)
    local function notify(severity)
        return function()
            workpane.notify[severity](translate("components.notifications.title-" .. severity), translate("components.notifications.message-" .. severity))
            report("notify", { severity = severity })
        end
    end

    local function burst()
        for index = 1, 6 do
            workpane.notify.information(translate("components.notifications.burst-title", index), translate("components.notifications.burst-message"))
        end

        report("notify", { count = 6 })
    end

    return {
        gallery.section("components.notifications.severities", {
            ui.row({ spacing = 8 }, {
                ui.button({ text = text("components.notifications.information"), icon = "information", onClick = notify("information") }),
                ui.button({ text = text("components.notifications.success"), icon = "success", onClick = notify("success") }),
                ui.button({ text = text("components.notifications.warning"), icon = "warning", onClick = notify("warning") }),
                ui.button({ text = text("components.notifications.error"), icon = "error", onClick = notify("error") }),
            }),
        }),
        gallery.section("components.notifications.limits", {
            ui.label({ text = text("components.notifications.limits-text"), style = "muted" }),
            ui.button({ text = text("components.notifications.burst"), icon = "bell", onClick = burst }),
        }),
    }
end
