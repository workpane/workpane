-- One open folder: its file tree, its documents in tabs, the panels below them, the outline beside them, the status bar of the document on screen and its language servers.
local async = require("async")
local fs = require("fs")
local analysis = include("analysis")
local documents = include("documents")
local editorconfig = include("editorconfig")
local encoding = include("encoding")
local panels = include("views/panels")
local paths = include("paths")
local preferences = include("preferences")
local tree = include("views/tree")

local ui = workpane.ui
local text = workpane.i18n.text
local number = workpane.i18n.number
local translate = workpane.i18n.translate

local workspace = {}

local Workspace = {}
Workspace.__index = Workspace

local pollMilliseconds = 1000
local endings = { lf = "LF", crlf = "CRLF", cr = "CR" }
local navigation = {
    { id = "definition", method = "textDocument/definition", key = "code-editor.actions.go-to-definition" },
    { id = "declaration", method = "textDocument/declaration", key = "code-editor.actions.go-to-declaration" },
    { id = "type-definition", method = "textDocument/typeDefinition", key = "code-editor.actions.go-to-type-definition" },
    { id = "implementation", method = "textDocument/implementation", key = "code-editor.actions.go-to-implementation" },
    { id = "references", method = "textDocument/references", key = "code-editor.actions.find-references" },
    { id = "incoming-calls", method = "textDocument/prepareCallHierarchy", key = "code-editor.actions.incoming-calls" },
    { id = "outgoing-calls", method = "textDocument/prepareCallHierarchy", key = "code-editor.actions.outgoing-calls" },
}

local function report(key, detail)
    workpane.notify.error(translate("code-editor.error.title"), translate(key) .. (detail ~= nil and detail ~= "" and "\n" .. detail or ""))
end

local function encodingItems()
    local items = {}

    for _, charset in ipairs(encoding.charsets()) do
        items[#items + 1] = { id = "reopen:" .. charset, text = text("code-editor.status.reopen-as", encoding.label(charset)) }
    end

    items[#items + 1] = { separator = true }

    for _, charset in ipairs(encoding.charsets()) do
        items[#items + 1] = { id = "save:" .. charset, text = text("code-editor.status.save-as", encoding.label(charset)) }
    end

    return items
end

-- Looks at EditorConfig files together and answers how each one looks now.
local function stampsOf(list)
    local pending = {}
    local stamps = {}

    for index, path in ipairs(list) do
        pending[index] = fs.stat(path)
    end

    local settled = async.allSettled(pending):await()

    for index, path in ipairs(list) do
        local info = settled[index].ok and settled[index].value or nil
        stamps[path] = info ~= nil and (info.size .. ":" .. info.mtime) or "missing"
    end

    return stamps
end

-- The handler persist stores the open folders whenever their documents, order or active document change, and current answers whether a folder is the one the reader has in front.
-- A folder keeps the documents it was stored with until it starts, so a folder restored but never shown is stored as it was.
function workspace.new(record, handlers, stored)
    local created = setmetatable({ id = record.id, root = record.root, documents = {}, opening = {}, handlers = handlers, progress = "", stamps = {}, stored = stored or {}, started = false, closed = false }, Workspace)
    created:build()

    return created
end

function Workspace:name()
    return self.root:match("[^/\\]+$") or self.root
end

function Workspace:build()
    self.nodes = {}
    self.tree = tree.new(self.root, {
        open = function(path)
            self:open(path)
        end,
        moved = function(source, destination)
            self:followMove(source, destination)
        end,
        deleted = function(path)
            self:closeUnder(path)
        end,
        watched = function(changes)
            local converted = {}

            for index, change in ipairs(changes) do
                converted[index] = { uri = workpane.files.uri(change.path), type = change.type }
            end

            self.analysis:watched(converted)
        end,
    })
    self.panels = panels.new(self.root, {
        open = function(path, line, column)
            self:open(path, { line = line, column = column })
        end,
        reveal = function(line, column)
            if self.active ~= nil then
                self.active.node:command("reveal", { line = line, column = column })
            end
        end,
        workspaceSymbols = function(query)
            self.analysis:workspaceSymbols(query, function(_, found)
                self.panels:addSymbols(query, found)
            end)
        end,
    })
    self.analysis = analysis.new(self.root, {
        diagnostics = function(path, language, list)
            self.panels:setDiagnostics(path, language, list)
            self:renderMarkers(path)
        end,
        outline = function(document, items)
            document.outline = items

            if document == self.active then
                self.panels:setOutline(items)
            end
        end,
        highlights = function(document, list, struck)
            document.struck = struck

            if document.node ~= nil then
                document.node:set({ highlights = list, decorations = self:decorationsOf(document) })
            end
        end,
        progress = function(message)
            self.progress = message
            self:renderStatus()
        end,
        changed = function()
            self:refreshCapabilities()
        end,
        dropped = function(language)
            self.panels:dropLanguage(language)

            for _, document in ipairs(self.documents) do
                if document.language.id == language then
                    document.outline = {}
                    document.struck = {}

                    if document.node ~= nil then
                        document.node:set({ highlights = {} })
                    end

                    self:renderMarkers(document.path)
                end
            end

            if self.active ~= nil and self.active.language.id == language then
                self.panels:setOutline({})
            end
        end,
    })

    self.nodes.documents = ui.tabs({ items = {}, closable = true, movable = true, grow = 1, onSelect = function(event)
        self:activate(self:find(event.id))
    end, onClose = function(event)
        self:close(self:find(event.id), true)
    end, onMove = function(event)
        self:reorder(event.id, event.index)
    end, onActivate = function(event)
        self.tree:reveal(event.id)
    end }, {})

    self.nodes.cursor = ui.label({ text = "", style = "caption", wrap = false })
    self.nodes.indentation = ui.label({ text = "", style = "caption", color = "text-muted", wrap = false })
    self.nodes.ending = ui.label({ text = "", style = "caption", color = "text-muted", wrap = false })
    self.nodes.encoding = ui.menuButton({ text = "", variant = "toolbar", enabled = false, tooltip = text("code-editor.status.encoding-action"), items = encodingItems(), onSelect = function(event)
        self:chooseEncoding(event.item)
    end })

    self.nodes.progress = ui.label({ text = "", style = "caption", color = "text-muted", wrap = false })
    self.nodes.signature = ui.label({ text = "", style = "caption", wrap = false, grow = 1 })
    self.nodes.wrap = ui.button({ text = text("code-editor.status.word-wrap"), variant = "toolbar", checked = preferences.get("wordWrap"), onClick = function()
        preferences.set("wordWrap", not preferences.get("wordWrap"))
    end })

    local filter = ui.filterField({ caption = text("code-editor.tree.filter"), placeholder = text("code-editor.tree.filter-placeholder"), onChange = function(event)
        self.tree:setFilter(event.value)
    end })

    local treePanel = ui.column({}, { ui.row({ padding = 9 }, { filter }), ui.divider({}), ui.scroll({ grow = 1 }, self.tree.node) })
    local sidebar = ui.splitter({ orientation = "vertical", ratio = 0.65, firstMinimum = 120, secondMinimum = 100 }, treePanel, self.panels.nodes.symbolPanel)
    local editorArea = ui.splitter({ orientation = "vertical", ratio = 0.75, firstMinimum = 160, secondMinimum = 120 }, self.nodes.documents, self.panels.nodes.bottom)
    local status = ui.row({ padding = { 2, 8, 2, 12 }, spacing = 16, background = "panel", borders = { "top" } }, { self.nodes.cursor, self.nodes.indentation, self.nodes.ending, self.nodes.encoding, self.nodes.progress, self.nodes.signature, self.nodes.wrap })

    self.node = ui.column({ grow = 1 }, {
        ui.splitter({ orientation = "horizontal", ratio = 0.22, firstMinimum = 200, secondMinimum = 360, grow = 1 }, sidebar, editorArea),
        status,
    })
end

function Workspace:find(path)
    for index, document in ipairs(self.documents) do
        if document.path == path then
            return document, index
        end
    end

    return nil, nil
end

-- The folder lists its root, opens the documents it was stored with and follows every file it shows for changes made outside the product, and a folder closed meanwhile stops where it is.
function Workspace:start()
    if self.started then
        return
    end

    self.started = true
    local info = fs.stat(self.root):await()

    if self.closed then
        return
    end

    if info == nil or not info.isDir then
        report("code-editor.error.workspace-unavailable", self.root)
        return
    end

    self.tree:start()
    local active

    for _, record in ipairs(self.stored) do
        local file = fs.stat(record.path):await()

        if self.closed then
            return
        end

        if file ~= nil and file.isFile and paths.inside(self.root, record.path) then
            local document = self:add(record.path, { line = record.line, column = record.column })
            active = record.active and document or active
        end
    end

    if self.closed then
        return
    end

    self:activate(active or self.documents[1])
    self:watch()
end

-- Every second the folder in front compares its open documents with their files, the EditorConfig files above them with what was read and the open folders of the tree with what they hold.
function Workspace:watch()
    workpane.task(function()
        while not self.closed do
            async.sleep(pollMilliseconds):await()

            if not self.closed and self.handlers.current(self) then
                self:pollFiles()
            end
        end
    end)
end

-- A folder closed while the loop slept or while one of its documents was read polls nothing more.
function Workspace:pollFiles()
    for _, document in ipairs({ table.unpack(self.documents) }) do
        if self.closed then
            return
        end

        document:poll()
    end

    if self.closed then
        return
    end

    self:checkEditorconfig()

    if not self.closed then
        self.tree:poll()
    end
end

-- The EditorConfig files of every open document are looked at together once a pass, and a change in any of them configures the documents again.
-- Only the files an open document still reads are remembered.
function Workspace:checkEditorconfig()
    local wanted = {}
    local list = {}

    for _, document in ipairs(self.documents) do
        for _, path in ipairs(editorconfig.watched(document.path, self.root)) do
            if not wanted[path] then
                wanted[path] = true
                list[#list + 1] = path
            end
        end
    end

    local stamps = stampsOf(list)
    local changed = false

    for path, stamp in pairs(stamps) do
        changed = changed or (self.stamps[path] ~= nil and self.stamps[path] ~= stamp)
    end

    self.stamps = stamps

    if not changed or self.closed then
        return
    end

    for _, document in ipairs({ table.unpack(self.documents) }) do
        if not document.closed then
            document:configure()
        end
    end

    self:renderStatus()
end

function Workspace:add(path, cursor)
    local document = documents.new(self, path, cursor, {
        changed = function(changed)
            self:renderDocuments()

            if changed == self.active then
                self:renderStatus()
            end
        end,
        edited = function(changed)
            self.analysis:changed(changed)
            self:signature(changed)
        end,
        cursor = function(moved)
            if moved == self.active then
                self:renderStatus()
            end

            -- The other uses of the symbol under the cursor are asked for at every move, and only the decorations are drawn again, and not at all while there are none before or after.
            self.analysis:occurrences(moved, function(ranges)
                if #ranges == 0 and #(moved.occurrences or {}) == 0 then
                    return
                end

                moved.occurrences = ranges

                if moved.node ~= nil then
                    moved.node:set({ decorations = self:decorationsOf(moved) })
                end
            end)
        end,
        -- Completion and hover text are asked of the text on screen, which the editor reports only a moment after the reader types.
        completion = function(asking, request)
            asking:settle()

            if asking.closed then
                return
            end

            self.analysis:complete(asking, request, function(proposals)
                asking.node:command("suggest", { request = request.request, items = proposals })
            end)
        end,
        hover = function(asking, request)
            asking:settle()

            if not asking.closed then
                self:hover(asking, request)
            end
        end,
        saved = function(saved)
            self.analysis:saved(saved)
        end,
        loaded = function(reloaded)
            self.analysis:changed(reloaded)
        end,
        removed = function(removed)
            self:close(removed, false)
        end,
    })

    document:component()

    for path, stamp in pairs(stampsOf(editorconfig.watched(path, self.root))) do
        self.stamps[path] = stamp
    end

    local loaded = not self.closed and document:load()

    -- A folder closed while the file was read keeps no document of it.
    if not loaded or self.closed then
        return nil
    end

    self.documents[#self.documents + 1] = document
    self.analysis:open(document)
    self:refreshCapabilities()
    self:renderMarkers(document.path)
    self:renderDocuments()

    return document
end

-- A path already being opened is waited for, so two opens of one file at once make one document.
function Workspace:opened(path, cursor)
    local pending = self.opening[path]

    if pending ~= nil then
        pending:await()
        return self:find(path)
    end

    local settled, resolve = async.deferred()
    self.opening[path] = settled
    local added, document = pcall(self.add, self, path, cursor)
    self.opening[path] = nil
    resolve()

    if not added then
        error(document, 0)
    end

    return document
end

-- Opening a path inside the folder shows its document, opening it first when it is not open, and places the cursor when a position comes with it.
function Workspace:open(path, cursor)
    local canonical = workpane.files.canonical(path):await()

    if self.closed then
        return
    end

    if canonical == nil or not paths.inside(self.root, canonical) then
        report("code-editor.error.path-outside", path)
        return
    end

    local document = self:find(canonical) or self:opened(canonical, cursor)

    if document == nil or self.closed then
        return
    end

    self:activate(document)

    if cursor ~= nil then
        document.cursor = cursor
        document.node:command("reveal", { line = cursor.line, column = cursor.column })
    end

    self.handlers.persist()
end

function Workspace:activate(document)
    self.active = document
    self.nodes.signature:set({ text = "" })
    self:renderDocuments()
    self:renderStatus()
    self.panels:setOutline(document ~= nil and document.outline or {})

    if document ~= nil then
        self.analysis:revisit(document)
        document.node:command("focus")
    end

    self.handlers.persist()
end

function Workspace:reorder(path, index)
    local document, position = self:find(path)

    if document ~= nil then
        table.insert(self.documents, index + 1, table.remove(self.documents, position))
        self.handlers.persist()
    end
end

-- A document with unsaved changes closes only after the reader agrees to lose them, and the neighbour of the closed tab comes on screen.
function Workspace:close(document, confirm)
    if document == nil then
        return true
    end

    document:settle()

    if confirm and document.dirty then
        local agreed = workpane.await(workpane.dialogs.confirm({ title = translate("code-editor.close.title"), message = translate("code-editor.close.file-message"), detail = document.path, confirmText = translate("code-editor.close.discard"), destructive = true }))

        if not agreed then
            return false
        end
    end

    local _, index = self:find(document.path)

    if index == nil then
        return true
    end

    table.remove(self.documents, index)
    document:close()
    self.analysis:closed(document)

    if self.active == document then
        self:activate(self.documents[math.min(index, #self.documents)])
    end

    self:renderDocuments()
    self.handlers.persist()

    return true
end

-- Closing the folder asks once for every document with unsaved changes, and one refusal keeps the whole folder open.
-- The documents are gone through from a copy, so a document the poll closes meanwhile never makes another one be skipped.
function Workspace:confirmClose()
    for _, document in ipairs({ table.unpack(self.documents) }) do
        document:settle()

        if not document.closed and document.dirty then
            local agreed = workpane.await(workpane.dialogs.confirm({ title = translate("code-editor.close.title"), message = translate("code-editor.close.workspace-message"), detail = document.path, confirmText = translate("code-editor.close.discard"), destructive = true }))

            if not agreed then
                return false
            end
        end
    end

    return true
end

-- A closed folder lets go of everything that waits for the text of its editors, which will never answer once they are gone.
function Workspace:shutdown()
    self.closed = true

    for _, document in ipairs(self.documents) do
        document:close()
    end

    self.analysis:stop()
end

function Workspace:settled()
    return self.analysis:settled()
end

-- Documents follow a moved file or folder to their new paths, with their language detected again and their servers told.
function Workspace:followMove(source, destination)
    for _, document in ipairs(self.documents) do
        if paths.inside(source, document.path) then
            local previousPath = document.path
            local previousLanguage = document.language.id
            document:moveTo(destination .. document.path:sub(#source + 1))
            document:configure()
            self.analysis:moved(document, previousPath, previousLanguage)
        end
    end

    self.panels:movePath(source, destination)
    self:renderDocuments()
    self.handlers.persist()
end

function Workspace:closeUnder(path)
    local closing = {}

    for _, document in ipairs(self.documents) do
        if paths.inside(path, document.path) then
            closing[#closing + 1] = document
        end
    end

    for _, document in ipairs(closing) do
        self:close(document, false)
    end
end

function Workspace:saveAll()
    for _, document in ipairs({ table.unpack(self.documents) }) do
        document:settle()

        if not document.closed and document.dirty then
            document:save()
        end
    end
end

function Workspace:saveActive()
    local document = self.active

    if document == nil then
        return
    end

    document:settle()

    if document.dirty then
        document:save()
    end
end

function Workspace:chooseEncoding(item)
    local action, charset = item:match("^(%a+):(.+)$")

    if self.active == nil or charset == nil then
        return
    end

    if action == "reopen" then
        self.active:reopenWith(charset)
    else
        self.active:saveWith(charset)
    end

    self:renderStatus()
end

-- The language server answers with the hover text of the word under the pointer, which the editor shows under the markers covering it.
function Workspace:hover(document, request)
    self.analysis:hover(document, request, function(hovered)
        document.node:command("hover-text", { line = request.line, column = request.column, text = hovered })
    end)
end

-- A request of the navigation menu is asked of the text on screen.
function Workspace:navigate(action)
    local document = self.active

    if document == nil then
        return
    end

    document:settle()

    if document.closed then
        return
    end

    local function opened(place)
        self:open(place.path, { line = place.line, column = place.character + 1 })
    end

    for _, entry in ipairs(navigation) do
        if entry.id == action and action == "references" then
            self.analysis:references(document, function(places)
                local list = {}

                for index, place in ipairs(places) do
                    list[index] = { path = place.path, line = place.line, column = place.character + 1 }
                end

                self.panels:setReferences("references", list)
            end)
        elseif entry.id == action and (action == "incoming-calls" or action == "outgoing-calls") then
            self.analysis:calls(document, action == "incoming-calls", function(calls)
                for _, call in ipairs(calls) do
                    call.column = call.character + 1
                end

                self.panels:setReferences(action == "incoming-calls" and "calls-incoming" or "calls-outgoing", calls)
            end)
        elseif entry.id == action then
            self.analysis:locate(document, entry.method, opened)
        end
    end
end

-- The active signature shows in the status bar once the reader types a character the server names for it, and any other edit clears it.
function Workspace:signature(document)
    if document ~= self.active then
        return
    end

    if not self.analysis:triggersSignature(document, document:characterBefore()) then
        self.nodes.signature:set({ text = "" })
        return
    end

    self.analysis:signature(document, function(label)
        if document == self.active then
            self.nodes.signature:set({ text = label })
        end
    end)
end

-- Completion, hover text and the navigation menu are offered only for what the server of each document declares.
function Workspace:refreshCapabilities()
    self.panels:setActive(self.analysis:active())

    for _, document in ipairs(self.documents) do
        if document.node ~= nil then
            local menu = {}

            for _, entry in ipairs(navigation) do
                if self.analysis:supports(document, entry.method) then
                    menu[#menu + 1] = { id = entry.id, text = text(entry.key) }
                end
            end

            document.node:set({ completion = self.analysis:supports(document, "textDocument/completion"), completionTriggers = self.analysis:completionTriggers(document), hovers = self.analysis:active(), definitions = self.analysis:supports(document, "textDocument/definition"), menu = menu, onMenu = function(event)
                self.active = document
                self:navigate(event.item)
            end, onDefinitionRequest = function(event)
                self.active = document
                document.cursor = { line = event.line, column = event.column }
                self:navigate("definition")
            end })
        end
    end
end

-- The decorations of a document are those of its problems, its struck tokens and the other uses of the symbol under its cursor.
function Workspace:decorationsOf(document)
    local decorations = self.panels:decorationsOf(document.path)

    for _, list in ipairs({ document.struck or {}, document.occurrences or {} }) do
        for _, decoration in ipairs(list) do
            decorations[#decorations + 1] = decoration
        end
    end

    return decorations
end

function Workspace:renderMarkers(path)
    local document = self:find(path)

    if document == nil or document.node == nil then
        return
    end

    document.node:set({ markers = self.panels:markersOf(path), decorations = self:decorationsOf(document) })
end

function Workspace:renderDocuments()
    local items = {}
    local pages = {}
    local order = {}

    for index, document in ipairs(self.documents) do
        items[index] = { id = document.path, text = document:name() .. (document.dirty and " *" or ""), tooltip = document.path }
        pages[index] = document:component()
        order[index] = document.path
    end

    local current = self.active or self.documents[1]
    local shown = { items = items, current = current ~= nil and current.path or "" }

    -- The pages are sent again only when the documents changed, together with the tabs, because a page that only moved keeps its place.
    if table.concat(order, "\n") == self.shownOrder then
        self.nodes.documents:set(shown)
    else
        self.shownOrder = table.concat(order, "\n")
        self.nodes.documents:setChildren(pages, shown)
    end
end

function Workspace:renderStatus()
    local document = self.active

    if document == nil then
        self.nodes.cursor:set({ text = "" })
        self.nodes.indentation:set({ text = "" })
        self.nodes.ending:set({ text = "" })
        self.nodes.encoding:set({ text = "", enabled = false })
        self.nodes.progress:set({ text = self.progress })
        return
    end

    local width = number(document.properties.indentWidth, 0)
    self.nodes.cursor:set({ text = text("code-editor.status.cursor", number(document.cursor.line, 0), number(document.cursor.column, 0)) })
    self.nodes.indentation:set({ text = text(document.properties.indentStyle == "tab" and "code-editor.status.tab-size" or "code-editor.status.space-size", width) })
    self.nodes.ending:set({ text = endings[document:effectiveEnding()] })
    self.nodes.encoding:set({ text = encoding.label(document:effectiveCharset()), enabled = true })
    self.nodes.progress:set({ text = self.progress })
end

function Workspace:applyPreferences(key, value)
    for _, document in ipairs(self.documents) do
        document:applyPreferences(key, value)
    end

    if key == "wordWrap" then
        self.nodes.wrap:set({ checked = value })
    elseif key == "languageServers" then
        self:restartServers()
    end
end

-- Turning the servers off stops them and clears what they reported, turning them on opens every document with them again, and only a server whose program changed starts again.
function Workspace:restartServers()
    self.analysis:refresh(self.documents)
    self:refreshCapabilities()
end

-- A folder that has not started yet is stored with the documents it was restored with.
function Workspace:snapshot()
    if not self.started then
        return { id = self.id, root = self.root, documents = self.stored }
    end

    local list = {}

    for index, document in ipairs(self.documents) do
        list[index] = { path = document.path, line = document.cursor.line, column = document.cursor.column, active = document == self.active }
    end

    return { id = self.id, root = self.root, documents = list }
end

return workspace
