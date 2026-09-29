-- The file tree of one open folder: folders listed when they open, hidden entries left out, a filter by name and the actions that create, rename, move and delete.
local fs = require("fs")
local catalog = include("catalog")
local paths = include("paths")

local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

local tree = {}

local Tree = {}
Tree.__index = Tree

local maximumDepth = 64

local function report(key, detail)
    workpane.notify.error(translate("code-editor.error.title"), translate(key) .. (detail ~= nil and detail ~= "" and "\n" .. detail or ""))
end

local function itemMenu()
    return {
        { id = "new-file", text = text("code-editor.actions.new-file"), icon = "add" },
        { id = "new-folder", text = text("code-editor.actions.new-folder"), icon = "folder" },
        { separator = true },
        { id = "rename", text = text("code-editor.actions.rename"), icon = "edit" },
        { id = "move", text = text("code-editor.actions.move") },
        { separator = true },
        { id = "delete", text = text("code-editor.actions.delete"), icon = "clear", destructive = true },
    }
end

local function rootMenu()
    return {
        { id = "new-file", text = text("code-editor.actions.new-file"), icon = "add" },
        { id = "new-folder", text = text("code-editor.actions.new-folder"), icon = "folder" },
        { separator = true },
        { id = "refresh", text = text("code-editor.actions.refresh"), icon = "refresh" },
    }
end

-- A name is one path segment, never a dot, two dots or anything holding a separator.
local function validName(candidate)
    return candidate ~= "" and candidate ~= "." and candidate ~= ".." and not candidate:find("[/\\\0]")
end

local function name(path)
    return path:match("[^/\\]+$") or path
end

-- Handlers open a file, follow a moved path, close what was deleted and tell the language servers what changed.
function tree.new(root, handlers)
    local created = setmetatable({ root = root, handlers = handlers, folders = {}, expanded = { [root] = true }, filter = "", filtered = nil, selected = "" }, Tree)

    created.node = ui.tree({ items = {}, menu = rootMenu(), onActivate = function(event)
        created:activated(event.id)
    end, onToggle = function(event)
        created:toggled(event.id, event.expanded)
    end, onSelect = function(event)
        created.selected = event.id
    end, onItemMenu = function(event)
        created:act(event.item, event.id)
    end, onMenu = function(event)
        created:act(event.item, root)
    end })

    return created
end

-- Forgets the listings and the open state of a folder and of everything inside it, which a folder that went away no longer has.
function Tree:forget(path)
    for folder in pairs(self.folders) do
        if paths.inside(path, folder) then
            self.folders[folder] = nil
        end
    end

    for folder in pairs(self.expanded) do
        if paths.inside(path, folder) and folder ~= self.root then
            self.expanded[folder] = nil
        end
    end
end

-- Folders come first and every level is ordered by name without regard to case, and a folder that cannot be listed is forgotten.
function Tree:load(path)
    local entries, failure = workpane.files.list(path):await()

    if failure ~= nil then
        self:forget(path)
        return
    end

    local visible = {}

    for _, entry in ipairs(entries) do
        if entry.name:sub(1, 1) ~= "." then
            visible[#visible + 1] = entry
        end
    end

    table.sort(visible, function(first, second)
        local firstFolder = first.kind == "directory"
        local secondFolder = second.kind == "directory"

        if firstFolder ~= secondFolder then
            return firstFolder
        end

        return first.name:lower() < second.name:lower()
    end)

    self.folders[path] = visible
end

function Tree:items(path, depth)
    local items = {}

    for _, entry in ipairs(self.folders[path] or {}) do
        local child = paths.join(path, entry.name)

        if entry.kind == "directory" then
            local open = self.expanded[child] == true
            items[#items + 1] = { id = child, text = entry.name, icon = "folder", iconColor = "warning", branch = true, expanded = open, children = open and depth < maximumDepth and self:items(child, depth + 1) or {}, menu = itemMenu() }
        else
            items[#items + 1] = { id = child, text = entry.name, icon = "file", menu = itemMenu() }
        end
    end

    return items
end

-- A filter shows every file and folder whose name holds it, with the folders that lead to them opened.
function Tree:filteredItems()
    local wanted = self.filter:lower()
    local nodes = {}
    local top = {}

    local function add(relative, isFile)
        local path = self.root
        local list = top
        local segments = {}

        for segment in relative:gmatch("[^/]+") do
            segments[#segments + 1] = segment
        end

        for index, segment in ipairs(segments) do
            path = paths.join(path, segment)
            local last = index == #segments

            if nodes[path] == nil then
                nodes[path] = last and isFile and { id = path, text = segment, icon = "file", menu = itemMenu() } or { id = path, text = segment, icon = "folder", iconColor = "warning", branch = true, expanded = true, children = {}, menu = itemMenu() }
                list[#list + 1] = nodes[path]
            end

            list = nodes[path].children
        end
    end

    for _, relative in ipairs(self.filtered or {}) do
        local hidden = ("/" .. relative):find("/%.")
        local segments = {}

        for segment in relative:gmatch("[^/]+") do
            segments[#segments + 1] = segment
        end

        if not hidden then
            for index = 1, #segments do
                if segments[index]:lower():find(wanted, 1, true) then
                    add(table.concat(segments, "/", 1, index), index == #segments)
                end
            end
        end
    end

    return top
end

local function holds(items, id)
    for _, item in ipairs(items) do
        if item.id == id or holds(item.children or {}, id) then
            return true
        end
    end

    return false
end

-- A selection the items no longer show, such as a file only the filter reached, is let go, since a tree refuses to select what it does not show.
function Tree:render()
    local items = self.filter == "" and self:items(self.root, 1) or self:filteredItems()

    if not holds(items, self.selected) then
        self.selected = ""
    end

    self.node:set({ items = items, selected = self.selected })
end

function Tree:start()
    self:load(self.root)
    self:render()
end

-- A folder opened again is listed again, since what it holds may have changed while it was closed.
function Tree:toggled(path, expanded)
    self.expanded[path] = expanded or nil

    if expanded then
        self:load(path)
    end

    self:render()
end

-- A folder is on screen when it and every folder above it up to the root are open.
function Tree:shown(path)
    local folder = path

    while folder ~= nil and paths.inside(self.root, folder) do
        if not self.expanded[folder] then
            return false
        end

        if folder == self.root then
            return true
        end

        folder = paths.parent(folder)
    end

    return false
end

-- The folders on screen are listed again, what appeared or went away outside the product is shown and told to the language servers, and a folder that went away is forgotten.
function Tree:poll()
    local changes = {}
    local open = {}

    for path in pairs(self.folders) do
        open[#open + 1] = self:shown(path) and path or nil
    end

    for _, path in ipairs(open) do
        if self:shown(path) and self.folders[path] ~= nil then
            local before = {}
            local after = {}

            for _, entry in ipairs(self.folders[path]) do
                before[entry.name] = entry.kind
            end

            self:load(path)

            -- A folder that went away while it was listed holds nothing, so everything it held is told as removed.
            for _, entry in ipairs(self.folders[path] or {}) do
                after[entry.name] = entry.kind

                if before[entry.name] ~= entry.kind then
                    changes[#changes + 1] = { path = paths.join(path, entry.name), type = 1 }
                end
            end

            for entryName, kind in pairs(before) do
                if after[entryName] ~= kind then
                    changes[#changes + 1] = { path = paths.join(path, entryName), type = 3 }
                end

                if kind == "directory" and after[entryName] ~= "directory" then
                    self:forget(paths.join(path, entryName))
                end
            end
        end
    end

    if #changes > 0 then
        self:render()
        self.handlers.watched(changes)
    end
end

function Tree:activated(path)
    for _, entry in ipairs(self.folders[paths.parent(path)] or {}) do
        if entry.name == name(path) and entry.kind == "directory" then
            return
        end
    end

    self.handlers.open(path)
end

function Tree:setFilter(value)
    self.filter = value:match("^%s*(.-)%s*$")

    -- A filter cleared over a selection opens the folders above it, so the file the reader found stays in sight.
    if self.filter == "" and self.selected ~= "" then
        self.filtered = nil
        self:reveal(self.selected)
        return
    end

    if self.filter == "" then
        self.filtered = nil
        self:render()
        return
    end

    if self.filtered == nil then
        local walked = workpane.await(workpane.files.walk(self.root, { maximum = catalog.limit("maximumWorkspaceFiles"), skip = { ".git" } }))
        self.filtered = walked.paths
    end

    self:render()
end

-- Revealing opens every folder above a file, selects it and scrolls it into view.
function Tree:reveal(path)
    local folder = paths.parent(path)
    local chain = {}

    while folder ~= nil and #folder > #self.root do
        table.insert(chain, 1, folder)
        folder = paths.parent(folder)
    end

    for _, directory in ipairs(chain) do
        self.expanded[directory] = true

        if self.folders[directory] == nil then
            self:load(directory)
        end
    end

    self.selected = path
    self:render()
    self.node:command("reveal", { id = path })
end

-- A folder listing is read again after a change inside it, and the filter walks the tree again the next time it is used.
function Tree:refresh(path)
    self.filtered = nil

    for folder in pairs(self.folders) do
        if paths.inside(path, folder) or folder == paths.parent(path) then
            self:load(folder)
        end
    end

    if self.filter ~= "" then
        self:setFilter(self.filter)
        return
    end

    self:render()
end

local function isFolder(path)
    local info = fs.stat(path):await()
    return info ~= nil and info.isDir and not info.isSymlink
end

-- A new file or folder goes into the folder of the item, or beside a file, and a new file opens at once.
function Tree:create(target, folder)
    local directory = isFolder(target) and target or paths.parent(target)
    local answer = workpane.await(workpane.dialogs.prompt({ title = translate(folder and "code-editor.actions.new-folder" or "code-editor.actions.new-file"), message = translate("code-editor.actions.name") }))
    local chosen = answer ~= nil and answer:match("^%s*(.-)%s*$") or nil

    if chosen == nil or not validName(chosen) then
        return
    end

    local path = paths.join(directory, chosen)

    if fs.stat(path):await() ~= nil then
        report("code-editor.error.destination-exists", path)
        return
    end

    local _, failure = (folder and fs.mkdir(path) or fs.writeFile(path, "")):await()

    if failure ~= nil then
        report(folder and "code-editor.error.create-directory-failed" or "code-editor.error.create-file-failed", path)
        return
    end

    self.expanded[directory] = true
    self:refresh(directory)
    self.handlers.watched({ { path = path, type = 1 } })

    if not folder then
        self.handlers.open(path)
    end
end

-- A destination that differs from the source only by the case of its letters is the source itself on a system that ignores case, so it is taken only when another entry spelled exactly so exists.
local function taken(source, destination)
    if fs.stat(destination):await() == nil then
        return false
    end

    if source:lower() ~= destination:lower() then
        return true
    end

    local entries = workpane.files.list(paths.parent(destination)):await() or {}

    for _, entry in ipairs(entries) do
        if entry.name == name(destination) then
            return true
        end
    end

    return false
end

function Tree:relocate(source, destination)
    if taken(source, destination) then
        report("code-editor.error.destination-exists", destination)
        return false
    end

    if destination ~= source and paths.inside(source, destination) then
        report("code-editor.error.move-invalid", destination)
        return false
    end

    local _, failure = fs.rename(source, destination):await()

    if failure ~= nil then
        report("code-editor.error.move-failed", source)
        return false
    end

    self:forget(source)
    self.handlers.moved(source, destination)
    self.handlers.watched({ { path = source, type = 3 }, { path = destination, type = 1 } })
    self:refresh(paths.parent(source))
    self:refresh(paths.parent(destination))

    return true
end

function Tree:rename(path)
    local answer = workpane.await(workpane.dialogs.prompt({ title = translate("code-editor.actions.rename"), message = translate("code-editor.actions.name"), value = name(path) }))

    if answer == nil or answer == name(path) then
        return
    end

    if not validName(answer) then
        report("code-editor.error.operation")
        return
    end

    self:relocate(path, paths.join(paths.parent(path), answer))
end

-- Moving asks for a folder inside the open folder and keeps the name of what moves.
function Tree:move(path)
    local chosen = workpane.await(workpane.dialogs.selectFolder({ title = translate("code-editor.actions.move"), initial = self.root }))

    if chosen == nil then
        return
    end

    local folder = workpane.await(workpane.files.canonical(chosen))

    if not paths.inside(self.root, folder) then
        return
    end

    self:relocate(path, paths.join(folder, name(path)))
end

function Tree:delete(path)
    local folder = isFolder(path)
    local confirmed = workpane.await(workpane.dialogs.confirm({ title = translate("code-editor.delete.title"), message = translate(folder and "code-editor.delete.folder-message" or "code-editor.delete.file-message"), detail = path, confirmText = translate("code-editor.actions.delete"), destructive = true }))

    if not confirmed then
        return
    end

    local _, failure = fs.removeRecursive(path):await()

    if failure ~= nil then
        report(folder and "code-editor.error.remove-directory-failed" or "code-editor.error.remove-file-failed", path)
        return
    end

    self:forget(path)
    self.handlers.deleted(path)
    self.handlers.watched({ { path = path, type = 3 } })
    self:refresh(paths.parent(path))
end

function Tree:act(action, path)
    if action == "new-file" or action == "new-folder" then
        self:create(path, action == "new-folder")
    elseif action == "rename" and path ~= self.root then
        self:rename(path)
    elseif action == "move" and path ~= self.root then
        self:move(path)
    elseif action == "delete" and path ~= self.root then
        self:delete(path)
    elseif action == "refresh" then
        self:refresh(self.root)
    end
end

return tree
