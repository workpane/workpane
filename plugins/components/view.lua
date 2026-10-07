-- The gallery view: the list of categories beside the page of the chosen one, which shortcuts move through.
local gallery = include("views/gallery")

local ui = workpane.ui
local text = workpane.i18n.text

local view = {}

local categories = {
    { id = "buttons", icon = "start", page = "views/pages/buttons" },
    { id = "text-input", icon = "edit", page = "views/pages/text-input" },
    { id = "selection", icon = "success", page = "views/pages/selection" },
    { id = "numbers", icon = "processor", page = "views/pages/numbers" },
    { id = "date-time", icon = "schedule", page = "views/pages/date-time" },
    { id = "color", icon = "graphics", page = "views/pages/color" },
    { id = "typography", icon = "logs", page = "views/pages/typography" },
    { id = "indicators", icon = "information", page = "views/pages/indicators" },
    { id = "lists", icon = "tasks", page = "views/pages/lists" },
    { id = "tables", icon = "storage", page = "views/pages/tables" },
    { id = "tabs", icon = "workspace", page = "views/pages/tabs" },
    { id = "layout", icon = "layout", page = "views/pages/layout" },
    { id = "dialogs", icon = "chat", page = "views/pages/dialogs" },
    { id = "native-dialogs", icon = "folder", page = "views/pages/native-dialogs" },
    { id = "notifications", icon = "bell", page = "views/pages/notifications" },
    { id = "icons", icon = "spark", page = "views/pages/icons" },
    { id = "theme-colors", icon = "focus", page = "views/pages/theme-colors" },
    { id = "code-editor", icon = "edit", page = "views/pages/code-editor" },
    { id = "terminal", icon = "terminal", page = "views/pages/terminal" },
    { id = "web-view", icon = "browser", page = "views/pages/web-view" },
    { id = "markdown", icon = "bookmark", page = "views/pages/markdown" },
    { id = "canvas", icon = "gamepad-2", page = "views/pages/canvas" },
}

-- A category page is built when it is chosen, so the gallery never keeps a web view or an editor alive in the background.
local function build(category)
    local events, report = gallery.events()
    local sections = include(category.page)(report)
    return gallery.page("components." .. category.id .. ".description", sections, events)
end

local function position(id)
    for index, category in ipairs(categories) do
        if category.id == id then
            return index
        end
    end

    return 1
end

local step

function view.build()
    local items = {}

    for index, category in ipairs(categories) do
        items[index] = { id = category.id, text = text("components." .. category.id .. ".title"), icon = category.icon }
    end

    local current = 1
    local content = ui.column({ grow = 1 }, { build(categories[current]) })
    local list

    local function show(index)
        current = index
        list:set({ selected = categories[index].id })
        content:setChildren({ build(categories[index]) })
    end

    step = function(delta)
        show((current - 1 + delta) % #categories + 1)
    end

    list = ui.list({
        style = "navigation",
        items = items,
        selected = categories[current].id,
        onSelect = function(event)
            show(position(event.id))
        end,
    })

    return ui.column({}, {
        ui.pageHeader({ title = text("components.view.title"), caption = text("components.view.caption") }),
        ui.row({ grow = 1 }, {
            ui.scroll({ width = 220 }, list),
            ui.divider({ orientation = "vertical" }),
            content,
        }),
    })
end

-- The view on screen moves through its categories when a shortcut asks it to, and nothing moves before the view exists.
function view.step(delta)
    if step ~= nil then
        step(delta)
    end
end

return view
