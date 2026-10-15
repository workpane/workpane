-- Every button variant and state, the chips, the menu button, the popover and the layout swatches it offers.
local gallery = include("views/gallery")
local ui = workpane.ui
local text = workpane.i18n.text

return function(report)
    local function clicked(name)
        return function()
            report("click", { button = name })
        end
    end

    local function toggled(value, node)
        node:set({ checked = not node:get("checked") })
        report("click", { checked = node:get("checked") })
    end

    -- A popover offers a few arrangements as swatches, and the one pressed becomes the chosen one and closes the panel.
    local arrangements = {
        { id = "single", columns = 1, rows = 1, cells = { { column = 0, row = 0 } } },
        { id = "columns", columns = 2, rows = 1, cells = { { column = 0, row = 0 }, { column = 1, row = 0 } } },
        { id = "left", columns = 2, rows = 2, cells = { { column = 0, row = 0, rowSpan = 2 }, { column = 1, row = 0 }, { column = 1, row = 1 } } },
        { id = "grid", columns = 2, rows = 2, cells = { { column = 0, row = 0 }, { column = 1, row = 0 }, { column = 0, row = 1 }, { column = 1, row = 1 } } },
    }
    local swatches = {}
    local popover

    for index, arrangement in ipairs(arrangements) do
        swatches[index] = ui.layoutSwatch({ columns = arrangement.columns, rows = arrangement.rows, cells = arrangement.cells, checked = index == 1, onClick = function()
            for other, swatch in ipairs(swatches) do
                swatch:set({ checked = other == index })
            end

            popover:command("close")
            report("click", { layout = arrangement.id })
        end })
    end

    popover = ui.popover({ text = text("components.buttons.popover-layout"), icon = "layout", variant = "toolbar" }, ui.grid({ columns = 4, columnSpacing = 3, rowSpacing = 3, width = 201 }, swatches))

    -- Two swatches beside the popover choose between themselves, so pressing one checks it and leaves the other unchecked.
    local chosen = {}
    local beside = {
        { id = "three", columns = 3, rows = 2, cells = { { column = 0, row = 0 }, { column = 1, row = 0 }, { column = 2, row = 0 }, { column = 0, row = 1 }, { column = 1, row = 1 } } },
        { id = "bottom", columns = 2, rows = 2, cells = { { column = 0, row = 0 }, { column = 1, row = 0 }, { column = 0, row = 1, columnSpan = 2 } } },
    }

    for index, arrangement in ipairs(beside) do
        chosen[index] = ui.layoutSwatch({ columns = arrangement.columns, rows = arrangement.rows, cells = arrangement.cells, checked = index == 2, onClick = function()
            for other, swatch in ipairs(chosen) do
                swatch:set({ checked = other == index })
            end

            report("click", { layout = arrangement.id })
        end })
    end

    return {
        gallery.section("components.buttons.variants", {
            ui.row({ spacing = 8 }, {
                ui.button({ text = text("components.buttons.default"), onClick = clicked("default") }),
                ui.button({ text = text("components.buttons.primary"), variant = "primary", onClick = clicked("primary") }),
                ui.button({ text = text("components.buttons.destructive"), variant = "destructive", onClick = clicked("destructive") }),
                ui.button({ text = text("components.buttons.toolbar"), icon = "refresh", variant = "toolbar", onClick = clicked("toolbar") }),
                ui.button({ icon = "settings", variant = "icon", tooltip = text("components.buttons.icon-tooltip"), onClick = clicked("icon") }),
                ui.button({ text = text("components.buttons.link"), variant = "link", onClick = clicked("link") }),
            }),
        }),
        gallery.section("components.buttons.states", {
            ui.row({ spacing = 8 }, {
                ui.button({ text = text("components.buttons.with-icon"), icon = "add", onClick = clicked("with-icon") }),
                ui.button({ text = text("components.buttons.checkable"), icon = "bookmark", variant = "toolbar", checked = false, onClick = toggled }),
                ui.button({ text = text("components.buttons.disabled"), enabled = false }),
                ui.button({ text = text("components.buttons.disabled-primary"), variant = "primary", enabled = false }),
                ui.button({ text = text("components.buttons.tooltip"), tooltip = text("components.buttons.tooltip-text"), onClick = clicked("tooltip") }),
            }),
        }),
        gallery.section("components.buttons.chips", {
            ui.row({ spacing = 8 }, {
                ui.chip({ text = text("components.buttons.chip-all"), checked = true, onClick = toggled }),
                ui.chip({ text = text("components.buttons.chip-errors"), onClick = toggled }),
                ui.chip({ text = text("components.buttons.chip-warnings"), onClick = toggled }),
                ui.chip({ text = text("components.buttons.chip-disabled"), enabled = false }),
            }),
        }),
        gallery.section("components.buttons.menu", {
            ui.row({ spacing = 8 }, {
                ui.menuButton({
                    text = text("components.buttons.menu-actions"),
                    icon = "more",
                    items = {
                        { id = "open", text = text("components.buttons.menu-open"), icon = "folder", shortcut = "Ctrl+O" },
                        { id = "refresh", text = text("components.buttons.menu-refresh"), icon = "refresh" },
                        { separator = true },
                        { id = "unavailable", text = text("components.buttons.menu-unavailable"), enabled = false },
                        { id = "delete", text = text("components.buttons.menu-delete"), icon = "clear", destructive = true },
                    },
                    onSelect = gallery.reporter(report, "select"),
                }),
                ui.menuButton({
                    icon = "more",
                    variant = "icon",
                    tooltip = text("components.buttons.menu-more"),
                    items = {
                        { id = "export", text = text("components.buttons.menu-export"), icon = "export" },
                        { id = "import", text = text("components.buttons.menu-import"), icon = "import" },
                    },
                    onSelect = gallery.reporter(report, "select"),
                }),
            }),
        }),
        gallery.section("components.buttons.popover", {
            ui.row({ spacing = 8 }, {
                popover,
                chosen[1],
                chosen[2],
                ui.layoutSwatch({ columns = 1, rows = 1, enabled = false, cells = { { column = 0, row = 0 } } }),
            }),
        }),
        gallery.section("components.buttons.context", {
            ui.card({
                padding = 24,
                background = "raised",
                outline = "none",
                menu = {
                    { id = "copy", text = text("components.buttons.menu-copy"), shortcut = "Ctrl+C" },
                    { id = "rename", text = text("components.buttons.menu-rename"), icon = "edit" },
                    { separator = true },
                    { id = "delete", text = text("components.buttons.menu-delete"), icon = "clear", destructive = true },
                },
                onMenu = gallery.reporter(report, "menu"),
            }, {
                ui.label({ text = text("components.buttons.context-hint"), style = "muted", textAlign = "center" }),
            }),
        }),
    }
end
