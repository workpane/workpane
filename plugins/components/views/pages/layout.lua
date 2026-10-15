-- The layout system: rows and columns with spacing, justification, growth and alignment, grids, splitters, stacks, cards, drag and drop, spacers and dividers.
local gallery = include("views/gallery")
local ui = workpane.ui
local text = workpane.i18n.text

-- A box is a raised card with a centered caption, which is enough to see where the layout placed it.
local function box(caption, props)
    local merged = { padding = 10, background = "raised", outline = "none" }

    for key, value in pairs(props or {}) do
        merged[key] = value
    end

    return ui.card(merged, { ui.label({ text = caption, textAlign = "center", wrap = false }) })
end

-- Two lanes trade their cards by dragging, and each lane is built again from the order the page keeps.
local function lanes(report)
    local names = { alpha = "components.layout.drag-alpha", beta = "components.layout.drag-beta", gamma = "components.layout.drag-gamma" }
    local order = { todo = { "alpha", "beta" }, done = { "gamma" } }
    local columns = {}

    local function cards(lane)
        local children = {}

        for index, id in ipairs(order[lane]) do
            children[index] = box(text(names[id]), { drag = { kind = "components.card", value = id, label = text(names[id]) } })
        end

        return children
    end

    local function receiver(lane)
        return function(event)
            for _, ids in pairs(order) do
                for index, id in ipairs(ids) do
                    if id == event.value then
                        table.remove(ids, index)
                        break
                    end
                end
            end

            table.insert(order[lane], event.value)

            for name, column in pairs(columns) do
                column:setChildren(cards(name))
            end

            report("drop", event)
        end
    end

    for _, lane in ipairs({ "todo", "done" }) do
        columns[lane] = ui.column({ spacing = 8, padding = 10, minHeight = 132, accepts = { "components.card" }, onDrop = receiver(lane) }, cards(lane))
    end

    return columns
end

return function(report)
    local justified = {}
    local board = lanes(report)

    for index, justify in ipairs({ "start", "center", "end", "space-between" }) do
        justified[index] = gallery.sample("components.layout.justify-" .. justify, ui.row({ spacing = 8, justify = justify, grow = 1, padding = 6, background = "panel" }, { box("A"), box("B"), box("C") }))
    end

    local aligned = {}

    for index, align in ipairs({ "start", "center", "end", "stretch" }) do
        aligned[index] = box(text("components.layout.align-" .. align), { align = align })
    end

    local cells = {}

    for index = 1, 6 do
        cells[index] = box(text("components.layout.cell", index))
    end

    local stack = ui.stack({ current = 0, height = 72 }, {
        box(text("components.layout.stack-first"), { grow = 1 }),
        box(text("components.layout.stack-second"), { grow = 1, background = "panel" }),
        box(text("components.layout.stack-third"), { grow = 1, outline = "border" }),
    })

    local function switched(event)
        stack:set({ current = tonumber(event.value) })
        report("change", event)
    end

    return {
        gallery.section("components.layout.justify", justified),
        gallery.section("components.layout.grow", {
            ui.row({ spacing = 8 }, {
                box(text("components.layout.grow-one"), { grow = 1 }),
                box(text("components.layout.grow-two"), { grow = 2 }),
                box(text("components.layout.grow-one"), { grow = 1 }),
            }),
            ui.row({ spacing = 8 }, {
                box(text("components.layout.fixed"), { width = 160 }),
                box(text("components.layout.bounded"), { grow = 1, minWidth = 120, maxWidth = 320 }),
                ui.spacer({ grow = 1 }),
                box(text("components.layout.after-spacer")),
            }),
        }),
        gallery.section("components.layout.alignment", {
            ui.column({ spacing = 6, padding = 8, background = "panel" }, aligned),
        }),
        gallery.section("components.layout.grid", {
            ui.grid({ columns = 3, columnSpacing = 8, rowSpacing = 8 }, cells),
        }),
        gallery.section("components.layout.splitter", {
            ui.splitter({ orientation = "horizontal", ratio = 0.35, firstMinimum = 120, secondMinimum = 160, height = 160, onResize = gallery.reporter(report, "resize") },
                box(text("components.layout.splitter-first"), { grow = 1 }),
                ui.splitter({ orientation = "vertical", ratio = 0.5, firstMinimum = 40, secondMinimum = 40, onResize = gallery.reporter(report, "resize") },
                    box(text("components.layout.splitter-top"), { grow = 1 }),
                    box(text("components.layout.splitter-bottom"), { grow = 1 }))),
        }),
        gallery.section("components.layout.stack", {
            ui.radioGroup({
                value = "0",
                orientation = "horizontal",
                options = {
                    { value = "0", text = text("components.layout.stack-first") },
                    { value = "1", text = text("components.layout.stack-second") },
                    { value = "2", text = text("components.layout.stack-third") },
                },
                onChange = switched,
            }),
            stack,
        }),
        gallery.section("components.layout.cards", {
            ui.row({ spacing = 12 }, {
                ui.card({ title = text("components.layout.card-titled"), icon = "information", padding = 12, grow = 1 }, {
                    ui.label({ text = text("components.layout.card-text") }),
                }),
                ui.card({ padding = 12, background = "raised", outline = "none", grow = 1 }, {
                    ui.label({ text = text("components.layout.card-raised") }),
                }),
                ui.card({ padding = 12, radius = 12, borders = { "left" }, grow = 1 }, {
                    ui.label({ text = text("components.layout.card-rounded") }),
                }),
                ui.card({ padding = 12, background = "raised", outline = "success", grow = 1 }, {
                    ui.label({ text = text("components.layout.card-outlined") }),
                }),
            }),
        }),
        gallery.section("components.layout.drag", {
            ui.row({ spacing = 12 }, {
                ui.card({ title = text("components.layout.drag-todo"), padding = 0, grow = 1 }, { board.todo }),
                ui.card({ title = text("components.layout.drag-done"), padding = 0, grow = 1 }, { board.done }),
            }),
        }),
        gallery.section("components.layout.dividers", {
            ui.label({ text = text("components.layout.above") }),
            ui.divider({}),
            ui.row({ spacing = 12, height = 28 }, {
                ui.label({ text = text("components.layout.left"), align = "center", wrap = false }),
                ui.divider({ orientation = "vertical" }),
                ui.label({ text = text("components.layout.right"), align = "center", wrap = false }),
            }),
        }),
    }
end
