-- The bookmarks panel: its actions on top, the tree of groups with the ungrouped collection first, and the actions that open the selected bookmark.
local address = include("address")
local bookmarks = include("bookmarks")

local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

local panel = {}

local ungrouped = "ungrouped"

local function notifyInvalid()
    workpane.notify.error(translate("browser.plugin.title"), translate("browser.error.invalid-bookmark"))
end

local function field(key, control)
    return ui.formField({ label = text(key) }, control)
end

-- Asks for the name of a group and answers it, or nil when the reader cancelled.
local function editGroup(titleKey, name)
    local value = ui.textField({ value = name })
    local button = workpane.await(workpane.dialogs.custom({ title = translate(titleKey), width = 420, content = ui.column({ spacing = 12 }, { field("browser.bookmarks.name", value) }), buttons = {
        { id = "cancel", text = translate("browser.actions.cancel") },
        { id = "save", text = translate("browser.actions.save"), variant = "primary" },
    } }))

    return button == "save" and value:get("value") or nil
end

-- The group choice opens with the ungrouped collection, which means none, followed by every group in the order of its name.
local function editBookmark(titleKey, name, url, groupId)
    local sorted = {}

    for _, group in ipairs(bookmarks.groups()) do
        sorted[#sorted + 1] = group
    end

    table.sort(sorted, function(left, right)
        return left.name:lower() < right.name:lower()
    end)

    local options = { { value = ungrouped, text = text("browser.bookmarks.ungrouped") } }

    for _, group in ipairs(sorted) do
        options[#options + 1] = { value = group.id, text = group.name }
    end

    local nameField = ui.textField({ value = name })
    local addressField = ui.textField({ value = url })
    local groupField = ui.combo({ value = groupId or ungrouped, options = options })
    local button = workpane.await(workpane.dialogs.custom({ title = translate(titleKey), width = 520, content = ui.column({ spacing = 12 }, {
        field("browser.bookmarks.name", nameField),
        field("browser.bookmarks.address", addressField),
        field("browser.bookmarks.group", groupField),
    }), buttons = {
        { id = "cancel", text = translate("browser.actions.cancel") },
        { id = "save", text = translate("browser.actions.save"), variant = "primary" },
    } }))

    if button ~= "save" then
        return nil
    end

    local chosen = groupField:get("value")
    return { name = nameField:get("value"), url = addressField:get("value"), groupId = chosen ~= ungrouped and chosen or nil }
end

-- Builds the panel around the page on screen, which the view answers through the current and open functions it hands over.
function panel.build(options)
    local selected = ""
    local collapsed = {}
    local tree
    local edit = ui.button({ icon = "edit", variant = "toolbar", tooltip = text("browser.actions.edit"), enabled = false })
    local remove = ui.button({ icon = "clear", variant = "toolbar", tooltip = text("browser.actions.remove"), enabled = false })
    local openHere = ui.button({ text = text("browser.bookmarks.open-current"), enabled = false, grow = 1 })
    local openNew = ui.button({ text = text("browser.bookmarks.open-new"), enabled = false, grow = 1 })
    local menu = {
        { id = "open-current", text = text("browser.bookmarks.open-current") },
        { id = "open-new", text = text("browser.bookmarks.open-new") },
        { separator = true },
        { id = "edit", text = text("browser.actions.edit"), icon = "edit" },
        { id = "remove", text = text("browser.actions.remove"), icon = "clear", destructive = true },
    }

    -- A group opens the first time it appears and then stays as the reader left it.
    local function items()
        local ungroupedItems = {}
        local byGroup = {}

        for _, entry in ipairs(bookmarks.entries()) do
            local item = { id = entry.id, text = entry.name, icon = "bookmark", draggable = true, menu = menu }

            if entry.groupId == nil then
                ungroupedItems[#ungroupedItems + 1] = item
            else
                byGroup[entry.groupId] = byGroup[entry.groupId] or {}
                table.insert(byGroup[entry.groupId], item)
            end
        end

        local list = { { id = ungrouped, text = text("browser.bookmarks.ungrouped"), icon = "folder", iconColor = "text", expanded = not collapsed[ungrouped], droppable = true, children = ungroupedItems } }

        for _, group in ipairs(bookmarks.groups()) do
            list[#list + 1] = { id = group.id, text = group.name, icon = "folder", iconColor = "text", expanded = not collapsed[group.id], droppable = true, children = byGroup[group.id] or {} }
        end

        return list
    end

    -- The selection stays on its item while it exists, and the actions follow what is selected.
    local function render()
        if bookmarks.find(selected) == nil and bookmarks.findGroup(selected) == nil then
            selected = ""
        end

        local bookmark = bookmarks.find(selected)
        tree:set({ items = items(), selected = selected })
        edit:set({ enabled = selected ~= "" })
        remove:set({ enabled = selected ~= "" })
        openHere:set({ enabled = bookmark ~= nil })
        openNew:set({ enabled = bookmark ~= nil })
    end

    local function open(inNewTab)
        local bookmark = bookmarks.find(selected)

        if bookmark ~= nil then
            options.open(bookmark.url, inNewTab)
        end
    end

    local function addGroup()
        local name = editGroup("browser.bookmarks.add-group", "")

        if name ~= nil and not bookmarks.createGroup(name) then
            notifyInvalid()
        end
    end

    -- A new bookmark starts from the page on screen and from the group that is selected.
    local function addBookmark()
        local page = options.current()
        local group = bookmarks.findGroup(selected)
        local value = editBookmark("browser.bookmarks.add-bookmark", page.title ~= "" and page.title or address.name(page.url), page.url, group ~= nil and group.id or nil)

        if value == nil then
            return
        end

        local id = bookmarks.create(value.name, value.url, value.groupId)

        if id == nil then
            notifyInvalid()
            return
        end

        selected = id
        render()
    end

    local function editSelected()
        local group = bookmarks.findGroup(selected)
        local bookmark = bookmarks.find(selected)

        if group ~= nil then
            local name = editGroup("browser.bookmarks.edit-group", group.name)

            if name ~= nil and not bookmarks.renameGroup(group.id, name) then
                notifyInvalid()
            end

            return
        end

        local value = bookmark ~= nil and editBookmark("browser.bookmarks.edit-bookmark", bookmark.name, bookmark.url, bookmark.groupId) or nil

        if value ~= nil and not bookmarks.update(bookmark.id, value.name, value.url, value.groupId) then
            notifyInvalid()
        end
    end

    local function removeSelected()
        local group = bookmarks.findGroup(selected)
        local bookmark = bookmarks.find(selected)
        local target = group or bookmark

        if target == nil then
            return
        end

        local confirmed = workpane.await(workpane.dialogs.confirm({
            title = translate(group ~= nil and "browser.bookmarks.remove-group" or "browser.bookmarks.remove-bookmark"),
            message = translate(group ~= nil and "browser.bookmarks.remove-group-question" or "browser.bookmarks.remove-bookmark-question"),
            detail = target.name,
            confirmText = translate("browser.actions.remove"),
            destructive = true,
        }))

        if not confirmed then
            return
        end

        if group ~= nil and not bookmarks.removeGroup(group.id) then
            notifyInvalid()
            return
        end

        if bookmark ~= nil and not bookmarks.remove(bookmark.id) then
            notifyInvalid()
        end
    end

    local menuActions = {
        ["open-current"] = function()
            open(false)
        end,
        ["open-new"] = function()
            open(true)
        end,
        edit = editSelected,
        remove = removeSelected,
    }

    tree = ui.tree({ items = items(), grow = 1, onSelect = function(event)
        selected = event.id
        render()
    end, onActivate = function(event)
        selected = event.id
        open(false)
    end, onToggle = function(event)
        collapsed[event.id] = not event.expanded or nil
    end, onMove = function(event)
        local groupId = event.parent ~= ungrouped and event.parent or nil

        if not bookmarks.place(event.item, groupId, event.index) then
            notifyInvalid()
            render()
        end
    end, onItemMenu = function(event)
        selected = event.id
        menuActions[event.item]()
    end })

    edit:on("click", editSelected)
    remove:on("click", removeSelected)
    openHere:on("click", function()
        open(false)
    end)

    openNew:on("click", function()
        open(true)
    end)

    bookmarks.listen(render)

    return ui.column({ background = "panel", visible = false }, {
        ui.row({ padding = { 6, 6, 6, 10 }, spacing = 2, borders = { "bottom" } }, {
            ui.sectionTitle({ text = text("browser.bookmarks.title"), grow = 1 }),
            ui.button({ icon = "folder", variant = "toolbar", tooltip = text("browser.bookmarks.add-group"), onClick = addGroup }),
            ui.button({ icon = "bookmark", variant = "toolbar", tooltip = text("browser.bookmarks.add-bookmark"), onClick = addBookmark }),
            edit,
            remove,
        }),
        tree,
        ui.row({ padding = { 6, 10, 8, 10 }, spacing = 6, borders = { "top" } }, { openHere, openNew }),
    })
end

return panel
