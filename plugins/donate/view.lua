-- Presents the maintainer and the verified donation destinations, which open only in the default browser of the reader.
local ui = workpane.ui
local text = workpane.i18n.text

local view = {}

local destinations = {
    githubSponsors = "https://github.com/sponsors/paulocoutinhox",
    kofi = "https://ko-fi.com/A0A412XEV",
}

-- A page that could not be handed to the browser is said in the language of the reader, and the reason goes to the log.
local function open(url)
    local _, failure = workpane.system.openUrl(url):await()

    if failure ~= nil then
        workpane.notify.error(workpane.i18n.translate("donate.error.open-title"), workpane.i18n.translate("donate.error.open-message"))
        workpane.log.error("browser", tostring(failure), { url = url })
    end
end

function view.build()
    return ui.column({ padding = 32, justify = "center" }, {
        ui.card({ maxWidth = 640, align = "center", radius = 8, padding = { 42, 48, 40, 48 }, spacing = 14 }, {
            ui.image({ source = "profile.png", shape = "circle", width = 176, height = 176 }),
            ui.label({ text = text("donate.view.name"), style = "strong", size = 11, textAlign = "center" }),
            ui.label({ text = text("donate.view.title"), style = "heading", textAlign = "center" }),
            ui.label({ text = text("donate.view.description"), style = "muted", size = 13, textAlign = "center" }),
            ui.row({ spacing = 10, justify = "center", padding = { 8, 0, 0, 0 } }, {
                ui.button({ text = text("donate.view.github-sponsors"), icon = "donate", variant = "primary", onClick = function()
                    open(destinations.githubSponsors)
                end }),
                ui.button({ text = text("donate.view.kofi"), icon = "donate", onClick = function()
                    open(destinations.kofi)
                end }),
            }),
            ui.label({ text = text("donate.view.external-note"), style = "muted", textAlign = "center" }),
        }),
    })
end

return view
