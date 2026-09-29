-- Markdown rendered with the theme fonts, next to the source it was written in, with links opened in the default browser.
local gallery = include("views/gallery")
local ui = workpane.ui
local translate = workpane.i18n.translate

return function(report)
    local source = translate("components.markdown.document")

    local function linked(event)
        report("link", event)
        local _, failure = workpane.system.openUrl(event.url):await()

        if failure ~= nil then
            workpane.notify.error(translate("components.markdown.link-failed"), event.url)
        end
    end

    local rendered = ui.markdown({ text = source, onLink = linked })

    local function edited(event)
        rendered:set({ text = event.value })
    end

    return {
        gallery.section("components.markdown.rendered", { rendered }),
        gallery.section("components.markdown.source", {
            ui.textArea({ value = source, rows = 14, onChange = edited }),
        }),
        gallery.section("components.markdown.sizes", {
            ui.markdown({ text = translate("components.markdown.small"), fontSize = 10 }),
            ui.markdown({ text = translate("components.markdown.large"), fontSize = 16 }),
        }),
    }
end
