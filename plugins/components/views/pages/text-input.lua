-- Single line fields in every mode, the secret fields, the filter field and the multiline area.
local gallery = include("views/gallery")
local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

return function(report)
    local changed = gallery.reporter(report, "change")
    local submitted = gallery.reporter(report, "submit")

    -- Revealing the confirmed secret asks first, and only a confirmation shows it.
    local function revealRequested(_, node)
        local confirmed = workpane.await(workpane.dialogs.confirm({
            title = translate("components.text-input.reveal-title"),
            message = translate("components.text-input.reveal-message"),
            confirmText = translate("components.text-input.reveal-confirm"),
        }))

        if confirmed then
            node:set({ revealed = true })
        end

        report("reveal-request", { confirmed = confirmed })
    end

    return {
        gallery.section("components.text-input.fields", {
            gallery.sample("components.text-input.plain", ui.textField({ placeholder = text("components.text-input.placeholder"), width = 320, onChange = changed, onSubmit = submitted })),
            gallery.sample("components.text-input.clearable", ui.textField({ value = "Workpane", clearButton = true, width = 320, onChange = changed, onSubmit = submitted })),
            gallery.sample("components.text-input.monospace", ui.textField({ value = "SELECT * FROM logs__entries", monospace = true, width = 320, onChange = changed })),
            gallery.sample("components.text-input.password", ui.textField({ value = "correct horse battery", password = true, width = 320, onChange = changed })),
            gallery.sample("components.text-input.read-only", ui.textField({ value = translate("components.text-input.read-only-value"), readOnly = true, width = 320 })),
            gallery.sample("components.text-input.disabled", ui.textField({ value = translate("components.text-input.disabled-value"), enabled = false, width = 320 })),
        }),
        gallery.section("components.text-input.secrets", {
            gallery.sample("components.text-input.secret", ui.secretField({ value = "token-1234", width = 320, onChange = changed, onReveal = gallery.reporter(report, "reveal") })),
            gallery.sample("components.text-input.secret-confirmed", ui.secretField({ value = "api-key-5678", confirmReveal = true, width = 320, onChange = changed, onReveal = gallery.reporter(report, "reveal"), onRevealRequest = revealRequested })),
        }),
        gallery.section("components.text-input.filter", {
            ui.filterField({ caption = text("components.text-input.filter-caption"), placeholder = text("components.text-input.filter-placeholder"), onChange = changed }),
        }),
        gallery.section("components.text-input.area", {
            ui.textArea({ value = translate("components.text-input.area-value"), rows = 5, wrap = true, onChange = changed }),
            ui.textArea({ placeholder = text("components.text-input.area-placeholder"), rows = 3, onChange = changed }),
        }),
    }
end
