-- Tabs that switch pages, and a strip of closable tabs that grows and shrinks as the reader adds and closes them, both reordered by dragging a tab.
local gallery = include("views/gallery")
local ui = workpane.ui
local text = workpane.i18n.text

return function(report)
    local pages = {}
    local items = {}

    for index, id in ipairs({ "overview", "details", "history" }) do
        items[index] = { id = id, text = text("components.tabs.tab-" .. id) }
        pages[index] = ui.column({ padding = 16 }, { ui.label({ text = text("components.tabs.page-" .. id) }) })
    end

    local opened = {
        { id = "document-1", text = text("components.tabs.document", 1), icon = "file", tooltip = text("components.tabs.drag") },
        { id = "document-2", text = text("components.tabs.document", 2), icon = "file", tooltip = text("components.tabs.drag") },
    }
    local counter = #opened
    local strip

    local function added()
        counter = counter + 1
        opened[#opened + 1] = { id = "document-" .. counter, text = text("components.tabs.document", counter), icon = "file", tooltip = text("components.tabs.drag") }
        strip:set({ items = opened, current = opened[#opened].id })
        report("add", { count = #opened })
    end

    local function closed(event)
        local kept = {}

        for _, item in ipairs(opened) do
            if item.id ~= event.id then
                kept[#kept + 1] = item
            end
        end

        opened = kept
        strip:set({ items = opened, current = opened[1] and opened[1].id or "" })
        report("close", event)
    end

    -- A drag moves a tab to a new index, which the page applies to the tabs it holds so a later close keeps the order the reader left.
    local function moved(event)
        for index, item in ipairs(opened) do
            if item.id == event.id then
                table.insert(opened, event.index + 1, table.remove(opened, index))
                break
            end
        end

        report("move", { id = event.id, index = event.index })
    end

    strip = ui.tabs({ items = opened, current = "document-1", closable = true, addButton = true, movable = true, onSelect = gallery.reporter(report, "select"), onClose = closed, onAdd = added, onMove = moved })

    return {
        gallery.section("components.tabs.pages", {
            ui.tabs({ items = items, current = "overview", height = 140, movable = true, onSelect = gallery.reporter(report, "select"), onMove = gallery.reporter(report, "move") }, pages),
        }),
        gallery.section("components.tabs.closable", { strip }),
    }
end
