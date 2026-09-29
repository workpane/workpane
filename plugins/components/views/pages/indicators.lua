-- Badges and status dots in every tone, the busy indicator, icons, images and avatars.
local gallery = include("views/gallery")
local ui = workpane.ui
local text = workpane.i18n.text

local tones = { "neutral", "accent", "success", "warning", "danger", "information" }

return function(report)
    local badges = {}
    local statuses = {}

    for index, tone in ipairs(tones) do
        badges[index] = ui.badge({ text = text("components.indicators.tone-" .. tone), tone = tone })
        statuses[index] = ui.row({ spacing = 6 }, {
            ui.statusIndicator({ tone = tone, align = "center" }),
            ui.label({ text = text("components.indicators.tone-" .. tone), wrap = false }),
        })
    end

    local busy = ui.busyIndicator({ size = 22, running = true })

    local function toggleBusy(_, node)
        local running = not busy:get("running")
        busy:set({ running = running })
        node:set({ text = text(running and "components.indicators.busy-stop" or "components.indicators.busy-start") })
        report("click", { running = running })
    end

    return {
        gallery.section("components.indicators.badges", { ui.row({ spacing = 8 }, badges) }),
        gallery.section("components.indicators.statuses", { ui.row({ spacing = 18 }, statuses) }),
        gallery.section("components.indicators.busy", {
            ui.row({ spacing = 16 }, {
                ui.busyIndicator({ size = 14, running = true, align = "center" }),
                busy,
                ui.busyIndicator({ size = 32, running = true }),
                ui.button({ text = text("components.indicators.busy-stop"), align = "center", onClick = toggleBusy }),
            }),
        }),
        gallery.section("components.indicators.icons", {
            ui.row({ spacing = 16 }, {
                ui.icon({ name = "success", color = "success", size = 20 }),
                ui.icon({ name = "warning", color = "warning", size = 20 }),
                ui.icon({ name = "error", color = "danger", size = 20 }),
                ui.icon({ name = "information", color = "information", size = 20 }),
                ui.icon({ name = "settings", size = 28 }),
            }),
        }),
        gallery.section("components.indicators.images", {
            ui.row({ spacing = 16 }, {
                ui.image({ source = "sample.png", width = 96, height = 96 }),
                ui.image({ source = "sample.png", shape = "circle", width = 96, height = 96 }),
                ui.image({ source = "missing.png", width = 96, height = 96, onError = gallery.reporter(report, "error") }),
            }),
        }),
        gallery.section("components.indicators.avatars", {
            ui.row({ spacing = 12 }, {
                ui.avatar({ icon = "person", size = 40 }),
                ui.avatar({ icon = "spark", fill = "accent", ink = "on-accent", size = 40 }),
                ui.avatar({ icon = "tool", fill = "warning", ink = "on-warning", size = 32 }),
                ui.avatar({ icon = "chat", fill = "information", ink = "on-information", size = 24 }),
            }),
        }),
    }
end
