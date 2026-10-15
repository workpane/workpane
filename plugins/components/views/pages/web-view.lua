-- A native web view with its history and loading controls, showing a page written by the plugin or any web address, and opening a page asked for a new window in place.
local gallery = include("views/gallery")
local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

local home = "https://paulox.dev"

-- The local page follows the dark surfaces of the product, so the embedded view does not flash a white rectangle.
local function page()
    return table.concat({
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>", translate("components.web-view.page-title"), "</title><style>",
        "body{margin:0;padding:24px;background:#252526;color:#e6e6e6;font:14px -apple-system,Segoe UI,Ubuntu,sans-serif}",
        "h1{font-size:20px;margin:0 0 12px}p{color:#a8a8a8;line-height:1.5}code{background:#1e1e1e;padding:2px 6px;border-radius:3px}a{color:#4caf50}",
        "</style></head><body><h1>", translate("components.web-view.page-title"), "</h1><p>",
        translate("components.web-view.page-text"), "</p><p><code>navigator.userAgent</code>: <span id=\"agent\"></span></p>",
        "<p><a href=\"", home, "\" target=\"_blank\">", translate("components.web-view.new-window"), "</a></p>",
        "<script>document.getElementById('agent').textContent=navigator.userAgent</script></body></html>",
    })
end

return function(report)
    local view
    local back = ui.button({ icon = "back", variant = "toolbar", tooltip = text("components.web-view.back"), enabled = false })
    local forward = ui.button({ icon = "forward", variant = "toolbar", tooltip = text("components.web-view.forward"), enabled = false })
    local reload = ui.button({ icon = "refresh", variant = "toolbar", tooltip = text("components.web-view.reload") })
    local stop = ui.button({ icon = "stop", variant = "toolbar", tooltip = text("components.web-view.stop"), visible = false })
    local status = ui.label({ text = "", style = "muted", grow = 1 })

    -- An address the view refuses is told on the event line of the page rather than failing the page.
    local function go(url)
        local sent, failure = pcall(view.command, view, "navigate", { url = url })

        if not sent then
            report("refused", { url = url, code = type(failure) == "table" and failure.code or tostring(failure) })
            return
        end

        report("navigate", { url = url })
    end

    local function showPage()
        view:set({ url = "", html = page() })
        report("navigate", { html = true })
    end

    -- The controls follow the page on screen, so Reload turns into Stop while it loads and the history buttons open only where there is history.
    local function navigated(event)
        back:set({ enabled = event.canGoBack })
        forward:set({ enabled = event.canGoForward })
        reload:set({ visible = not event.loading })
        stop:set({ visible = event.loading })
        status:set({ text = event.title ~= "" and event.title or event.url })
        report("navigation", event)
    end

    view = ui.webView({ url = home, height = 320, onNavigation = navigated, onError = gallery.reporter(report, "error"), onOpenRequest = function(event)
        report("open-request", event)
        go(event.url)
    end })

    back:on("click", function()
        view:command("back")
    end)

    forward:on("click", function()
        view:command("forward")
    end)

    reload:on("click", function()
        view:command("reload")
    end)

    stop:on("click", function()
        view:command("stop")
    end)

    local address = ui.textField({ value = home, grow = 1, clearButton = true, onSubmit = function(event)
        go(event.value)
    end })

    return {
        gallery.section("components.web-view.browser", {
            ui.row({ spacing = 8 }, {
                back,
                forward,
                reload,
                stop,
                address,
                ui.button({ text = text("components.web-view.go"), icon = "forward", variant = "primary", onClick = function()
                    go(address:get("value"))
                end }),
                ui.button({ text = text("components.web-view.local"), icon = "home", onClick = showPage }),
            }),
            status,
            view,
        }),
    }
end
