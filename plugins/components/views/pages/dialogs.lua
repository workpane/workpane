-- The product dialogs: confirmation, destructive confirmation, alert, prompt and a custom dialog holding components of its own.
local gallery = include("views/gallery")
local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

return function(report)
    local function confirm()
        local confirmed = workpane.await(workpane.dialogs.confirm({ title = translate("components.dialogs.confirm-title"), message = translate("components.dialogs.confirm-message"), detail = translate("components.dialogs.confirm-detail") }))
        report("confirm", { confirmed = confirmed })
    end

    local function destructive()
        local confirmed = workpane.await(workpane.dialogs.confirm({ title = translate("components.dialogs.destructive-title"), message = translate("components.dialogs.destructive-message"), confirmText = translate("components.dialogs.destructive-confirm"), destructive = true }))
        report("confirm", { confirmed = confirmed, destructive = true })
    end

    local function alert()
        workpane.await(workpane.dialogs.alert({ title = translate("components.dialogs.alert-title"), message = translate("components.dialogs.alert-message") }))
        report("alert", { closed = true })
    end

    local function prompt()
        local value = workpane.await(workpane.dialogs.prompt({ title = translate("components.dialogs.prompt-title"), message = translate("components.dialogs.prompt-message"), value = "Workpane", placeholder = translate("components.dialogs.prompt-placeholder") }))
        report("prompt", { value = value or "nil" })
    end

    -- The custom dialog is a small form whose Create button checks it before closing, so a problem shows inside the dialog and scrolls there while the dialog keeps its size.
    local function custom()
        local name = ui.textField({ placeholder = text("components.dialogs.custom-name-placeholder") })
        local notify = ui.checkbox({ text = text("components.dialogs.custom-notify"), checked = true })
        local problem = ui.alert({ text = text("components.dialogs.custom-name-missing"), visible = false })
        local button = workpane.await(workpane.dialogs.custom({
            title = translate("components.dialogs.custom-title"),
            message = translate("components.dialogs.custom-message"),
            width = 440,
            content = ui.column({ spacing = 12 }, {
                ui.formField({ label = text("components.dialogs.custom-name") }, name),
                ui.formField({ label = text("components.dialogs.custom-priority"), hint = text("components.dialogs.custom-priority-hint") }, ui.combo({ value = "normal", options = {
                    { value = "low", text = text("components.dialogs.priority-low") },
                    { value = "normal", text = text("components.dialogs.priority-normal") },
                    { value = "high", text = text("components.dialogs.priority-high") },
                } })),
                notify,
                problem,
            }),
            buttons = {
                { id = "cancel", text = translate("components.dialogs.custom-cancel") },
                { id = "create", text = translate("components.dialogs.custom-create"), variant = "primary", closes = false },
            },
            onButton = function(pressed, dialog)
                if pressed ~= "create" then
                    return
                end

                if (name:get("value") or ""):match("^%s*$") then
                    problem:set({ visible = true })
                    return
                end

                dialog:close("create")
            end,
        }))
        report("custom", { button = button, name = name:get("value") or "", notify = notify:get("checked") })
    end

    return {
        gallery.section("components.dialogs.product", {
            ui.row({ spacing = 8 }, {
                ui.button({ text = text("components.dialogs.open-confirm"), onClick = confirm }),
                ui.button({ text = text("components.dialogs.open-destructive"), variant = "destructive", onClick = destructive }),
                ui.button({ text = text("components.dialogs.open-alert"), onClick = alert }),
                ui.button({ text = text("components.dialogs.open-prompt"), onClick = prompt }),
                ui.button({ text = text("components.dialogs.open-custom"), variant = "primary", onClick = custom }),
            }),
        }),
    }
end
