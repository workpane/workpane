-- The one guard every stored key of the plugin shares: a key shows in plain text only once the reader confirms it.
local translate = workpane.i18n.translate

local reveal = {}

function reveal.guard(field)
    field:on("reveal-request", function()
        if workpane.await(workpane.dialogs.confirm({ title = translate("ai.settings.reveal-title"), message = translate("ai.settings.reveal-message"), detail = translate("ai.settings.reveal-detail"), confirmText = translate("ai.settings.reveal-action") })) then
            field:set({ revealed = true })
        end
    end)

    return field
end

return reveal
