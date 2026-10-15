-- Plain and navigation lists with icons and details, and a tree whose branches open and close, whose files are dragged between folders and carry their own menu.
local gallery = include("views/gallery")
local ui = workpane.ui
local text = workpane.i18n.text

return function(report)
    local selected = gallery.reporter(report, "select")
    local activated = gallery.reporter(report, "activate")

    local files = {
        { id = "plugin", text = "plugin.lua", detail = text("components.lists.detail-plugin"), icon = "file" },
        { id = "translations", text = "translations.lua", detail = text("components.lists.detail-translations"), icon = "file" },
        { id = "assets", text = "assets", detail = text("components.lists.detail-assets"), icon = "folder", iconColor = "warning" },
        { id = "logs", text = text("components.lists.item-logs"), icon = "logs", iconColor = "information" },
        { id = "broken", text = text("components.lists.item-broken"), icon = "error", iconColor = "danger" },
    }

    local destinations = {
        { id = "home", text = text("components.lists.item-home"), icon = "home" },
        { id = "workspace", text = text("components.lists.item-workspace"), icon = "workspace" },
        { id = "tasks", text = text("components.lists.item-tasks"), icon = "tasks" },
    }

    local menu = {
        { id = "open", text = text("components.lists.menu-open"), icon = "edit" },
        { separator = true },
        { id = "remove", text = text("components.lists.menu-remove"), icon = "clear", destructive = true },
    }

    local function file(id, name)
        return { id = id, text = name, icon = "file", draggable = true, menu = menu }
    end

    local items = {
        { id = "workpane", text = "workpane", icon = "folder", expanded = true, droppable = true, children = {
            { id = "src", text = "src", icon = "folder", expanded = true, droppable = true, children = { file("main", "main.cpp"), file("application", "Application.cpp") } },
            { id = "lua", text = "lua", icon = "folder", droppable = true, children = { file("bootstrap", "bootstrap.lua") } },
            file("readme", "README.md"),
        } },
    }

    local tree

    -- Takes an item out of the branch that holds it and answers it, which is how a drop and a removal both start.
    local function take(branch, id)
        for index, item in ipairs(branch) do
            if item.id == id then
                return table.remove(branch, index)
            end

            local found = take(item.children or {}, id)

            if found ~= nil then
                return found
            end
        end

        return nil
    end

    local function folder(branch, id)
        for _, item in ipairs(branch) do
            if item.id == id then
                return item
            end

            local found = folder(item.children or {}, id)

            if found ~= nil then
                return found
            end
        end

        return nil
    end

    -- The tree reports where the reader dropped a file, and the page moves it there and shows the tree again.
    local function moved(event)
        local moved = take(items, event.item)
        table.insert(folder(items, event.parent).children, event.index + 1, moved)
        tree:set({ items = items })
        report("move", event)
    end

    local function picked(event)
        if event.item == "remove" then
            take(items, event.id)
            tree:set({ items = items, selected = "" })
        end

        report("item-menu", event)
    end

    tree = ui.tree({ selected = "src", items = items, height = 200, onSelect = selected, onActivate = activated, onToggle = gallery.reporter(report, "toggle"), onMove = moved, onItemMenu = picked })

    return {
        gallery.section("components.lists.plain", {
            ui.list({ items = files, selected = "plugin", onSelect = selected, onActivate = activated }),
        }),
        gallery.section("components.lists.navigation", {
            ui.list({ items = destinations, style = "navigation", selected = "workspace", width = 240, onSelect = selected }),
        }),
        gallery.section("components.lists.tree", { tree }),
    }
end
