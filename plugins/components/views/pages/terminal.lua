-- A terminal running the shell of the reader in the home directory, with its color schemes, its steady or blinking cursor and the actions a plugin gives it.
local gallery = include("views/gallery")
local ui = workpane.ui
local text = workpane.i18n.text

return function(report)
    local terminal = ui.terminal({
        directory = workpane.system.home(),
        height = 360,
        onTitle = gallery.reporter(report, "title"),
        onDirectory = gallery.reporter(report, "directory"),
        onBell = gallery.reporter(report, "bell"),
        onExit = gallery.reporter(report, "exit"),
        onLink = gallery.reporter(report, "link"),
        onZoom = gallery.reporter(report, "zoom"),
        onFind = gallery.reporter(report, "find"),
        onFocus = gallery.reporter(report, "focus"),
        onError = gallery.reporter(report, "error"),
    })

    local palettes = {
        { value = "vivid", text = text("components.terminal.vivid") },
        { value = "balanced", text = text("components.terminal.balanced") },
        { value = "soft", text = text("components.terminal.soft") },
    }

    local function run(name)
        return function()
            terminal:command(name)
            report(name, {})
        end
    end

    return {
        gallery.section("components.terminal.shell", {
            ui.row({ spacing = 8 }, {
                ui.combo({ value = "balanced", options = palettes, width = 160, onChange = function(event)
                    terminal:set({ palette = event.value })
                    report("palette", event)
                end }),
                ui.checkbox({ text = text("components.terminal.blink"), checked = false, onChange = function(event)
                    terminal:set({ cursorBlink = event.checked and 530 or 0 })
                    report("blink", event)
                end }),
                ui.button({ text = text("components.terminal.clear"), icon = "clear", onClick = run("clear") }),
                ui.button({ text = text("components.terminal.copy"), onClick = run("copy") }),
                ui.button({ text = text("components.terminal.paste"), onClick = run("paste") }),
                ui.button({ text = text("components.terminal.find"), icon = "search", onClick = run("find") }),
                ui.button({ text = text("components.terminal.restart"), icon = "refresh", onClick = run("restart") }),
            }),
            terminal,
        }),
    }
end
