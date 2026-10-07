-- The panels of one open folder: the problems its servers report, the references and calls last asked for, the text search, and the outline beside the documents with the symbols of the folder.
local async = require("async")
local catalog = include("catalog")
local paths = include("paths")

local ui = workpane.ui
local text = workpane.i18n.text
local number = workpane.i18n.number
local translate = workpane.i18n.translate

local panels = {}

local Panels = {}
Panels.__index = Panels

local severityTones = { [1] = "danger", [2] = "warning" }
local relatedShown = 64

local function fileName(path)
    return path:match("[^/\\]+$") or path
end

-- A tab title counts its rows and says so when more arrived than it lists.
local titles = {
    problems = { "code-editor.problems.count", "code-editor.problems.count-capped" },
    references = { "code-editor.references.count", "code-editor.references.count-capped" },
    ["calls-incoming"] = { "code-editor.calls.incoming-count", "code-editor.calls.incoming-count-capped" },
    ["calls-outgoing"] = { "code-editor.calls.outgoing-count", "code-editor.calls.outgoing-count-capped" },
    search = { "code-editor.search.count", "code-editor.search.count-capped" },
}

local function title(kind, count, capped)
    return text(titles[kind][capped and 2 or 1], number(count, 0))
end

-- Handlers open a place in a file and ask the servers for the symbols of the folder.
function panels.new(root, handlers)
    local created = setmetatable({ root = root, handlers = handlers, problems = {}, problemRows = {}, problemFilter = "", references = {}, referenceKind = "references", searchRows = {}, searchCount = 0, searchComplete = true, symbolQuery = "", outline = {}, symbolResults = {}, active = false, revision = 0 }, Panels)
    created:build()

    return created
end

function Panels:build()
    self.nodes = {}
    self.nodes.problemFilter = ui.textField({ placeholder = text("code-editor.problems.filter"), clearButton = true, onChange = function(event)
        self.problemFilter = event.value
        self:renderProblems()
    end })

    self.nodes.problems = ui.table({ columns = {
        { id = "file", title = text("code-editor.problems.file"), width = 180 },
        { id = "line", title = text("code-editor.problems.line"), width = 60, align = "end" },
        { id = "code", title = text("code-editor.problems.code"), width = 90 },
        { id = "source", title = text("code-editor.problems.source"), width = 110 },
        { id = "message", title = text("code-editor.problems.message"), width = "stretch" },
    }, rows = {}, selection = "subtle", grow = 1, onActivate = function(event)
        local place = self.problemRows[event.id]

        if place ~= nil then
            self.handlers.open(place.path, place.line, place.column)
        end
    end })

    self.nodes.references = ui.table({ columns = {
        { id = "file", title = text("code-editor.problems.file"), width = 180 },
        { id = "line", title = text("code-editor.problems.line"), width = 60, align = "end" },
        { id = "location", title = text("code-editor.references.context"), width = "stretch" },
    }, rows = {}, selection = "subtle", grow = 1, onActivate = function(event)
        local place = self.references[tonumber(event.id)]

        if place ~= nil then
            self.handlers.open(place.path, place.line, place.column)
        end
    end })

    self.nodes.searchField = ui.textField({ placeholder = text("code-editor.search.placeholder"), clearButton = true, onSubmit = function(event)
        self:search(event.value)
    end })

    self.nodes.search = ui.table({ columns = {
        { id = "file", title = text("code-editor.problems.file"), width = 180 },
        { id = "line", title = text("code-editor.problems.line"), width = 60, align = "end" },
        { id = "text", title = text("code-editor.search.line-text"), width = "stretch" },
    }, rows = {}, selection = "subtle", grow = 1, onActivate = function(event)
        local place = self.searchRows[tonumber(event.id)]

        if place ~= nil then
            self.handlers.open(place.path, place.line, 1)
        end
    end })

    self.nodes.problemsPage = ui.column({ padding = 6, spacing = 6 }, { self.nodes.problemFilter, self.nodes.problems })
    self.nodes.referencesPage = ui.column({ padding = 6 }, { self.nodes.references })
    self.nodes.searchPage = ui.column({ padding = 6, spacing = 6 }, { self.nodes.searchField, self.nodes.search })
    self.nodes.bottom = ui.tabs({ items = {}, current = "search", minHeight = 120 }, {})

    self.nodes.symbolField = ui.textField({ placeholder = text("code-editor.symbols.search"), clearButton = true, onChange = function(event)
        self:searchSymbols(event.value)
    end })

    self.nodes.symbols = ui.tree({ items = {}, onActivate = function(event)
        self:openSymbol(event.id)
    end })

    self.nodes.symbolPanel = ui.column({ padding = { 6, 6, 0, 6 }, spacing = 6, visible = false }, { self.nodes.symbolField, ui.scroll({ grow = 1 }, self.nodes.symbols) })
    self:renderTabs()
end

-- Problems and references come from the language servers, so their tabs show only while servers are active, and the search tab always shows.
function Panels:renderTabs()
    local items = {}
    local pages = {}

    if self.active then
        items[#items + 1] = { id = "problems", text = title("problems", self.problemCount or 0, self.problemsCapped) }
        pages[#pages + 1] = self.nodes.problemsPage
        items[#items + 1] = { id = "references", text = title(self.referenceKind, #self.references, self.referencesCapped) }
        pages[#pages + 1] = self.nodes.referencesPage
    end

    items[#items + 1] = { id = "search", text = title("search", self.searchCount, not self.searchComplete) }
    pages[#pages + 1] = self.nodes.searchPage
    local shown = { items = items, current = self.active and self.nodes.bottom:get("current") or "search" }
    local order = table.concat(self.active and { "problems", "references", "search" } or { "search" }, " ")

    if order == self.pageOrder then
        self.nodes.bottom:set(shown)
    else
        self.pageOrder = order
        self.nodes.bottom:setChildren(pages, shown)
    end

    self.nodes.symbolPanel:set({ visible = self.active })
end

function Panels:setActive(active)
    if self.active == active then
        return
    end

    self.active = active

    if not active then
        self.problems = {}
        self.references = {}
        self.outline = {}
        self.symbolResults = {}
        self:renderProblems()
        self:renderReferences()
        self:renderSymbols()
    end

    self:renderTabs()
end

-- The diagnostics of a file replace what the same server said about it before, and an empty list removes the file.
function Panels:setDiagnostics(path, language, list)
    self.problems[path] = self.problems[path] or {}
    self.problems[path][language] = #list > 0 and list or nil

    if next(self.problems[path]) == nil then
        self.problems[path] = nil
    end

    self:scheduleProblems()
end

-- The diagnostics a batch of messages brings are listed once, after the batch, rather than once per file.
function Panels:scheduleProblems()
    if self.problemsScheduled then
        return
    end

    self.problemsScheduled = true

    workpane.task(function()
        async.sleep(0):await()
        self.problemsScheduled = false
        self:renderProblems()
    end)
end

function Panels:dropLanguage(language)
    for path, byLanguage in pairs(self.problems) do
        byLanguage[language] = nil

        if next(byLanguage) == nil then
            self.problems[path] = nil
        end
    end

    self:renderProblems()
end

-- Each diagnostic of a file marks its range with its origin and code as detail, as Visual Studio Code writes them, and names its related places by file, line and column.
function Panels:markersOf(path)
    local markers = {}

    for _, entries in pairs(self.problems[path] or {}) do
        for _, entry in ipairs(entries) do
            local related = {}

            for _, information in ipairs(entry.related) do
                if information.message:find("%S") then
                    related[#related + 1] = { place = fileName(information.path) .. ":" .. information.line .. ":" .. information.column, message = information.message }
                end
            end

            -- The editor takes a bounded list of related places, so the last one it shows is the first of the places left out and counts them.
            if #related > relatedShown then
                local left = #related - relatedShown + 1

                for index = #related, relatedShown + 1, -1 do
                    related[index] = nil
                end

                related[relatedShown] = { place = related[relatedShown].place, message = translate("code-editor.problems.more-related", tostring(left)) }
            end

            local detail = entry.source ~= "" and entry.code ~= "" and entry.source .. "(" .. entry.code .. ")" or entry.source .. entry.code
            markers[#markers + 1] = { line = entry.line, column = entry.column, endLine = entry.endLine, endColumn = entry.endColumn, tone = severityTones[entry.severity] or "information", message = entry.message, detail = detail:find("%S") and detail or nil, related = related }
        end
    end

    return markers
end

-- Code a problem calls unnecessary is faded and code it calls deprecated is struck through, as the editors that read these tags draw them.
function Panels:decorationsOf(path)
    local decorations = {}

    for _, entries in pairs(self.problems[path] or {}) do
        for _, entry in ipairs(entries) do
            local style = entry.unnecessary and "faded" or entry.deprecated and "struck" or nil

            if style ~= nil and (entry.endLine > entry.line or entry.endColumn > entry.column) then
                decorations[#decorations + 1] = { line = entry.line, column = entry.column, endLine = entry.endLine, endColumn = entry.endColumn, style = style }
            end
        end
    end

    return decorations
end

function Panels:movePath(source, destination)
    local moved = {}

    for path, byLanguage in pairs(self.problems) do
        if paths.inside(source, path) then
            moved[destination .. path:sub(#source + 1)] = byLanguage
            self.problems[path] = nil
        end
    end

    for path, byLanguage in pairs(moved) do
        self.problems[path] = byLanguage
    end

    self:renderProblems()
end

-- Problems are ordered by file and line, filtered by file or message without regard to case, and bounded.
function Panels:renderProblems()
    local entries = {}
    local wanted = self.problemFilter:lower():match("^%s*(.-)%s*$")

    for path, byLanguage in pairs(self.problems) do
        for _, list in pairs(byLanguage) do
            for _, entry in ipairs(list) do
                if wanted == "" or path:lower():find(wanted, 1, true) or entry.message:lower():find(wanted, 1, true) then
                    entries[#entries + 1] = { path = path, entry = entry }
                end
            end
        end
    end

    table.sort(entries, function(first, second)
        if first.path ~= second.path then
            return first.path < second.path
        end

        return first.entry.line < second.entry.line
    end)

    local limit = catalog.limit("maximumProblems")
    local rows = {}
    self.problemRows = {}
    self.problemsCapped = #entries > limit
    self.problemCount = math.min(#entries, limit)

    for index = 1, math.min(#entries, limit) do
        local path, entry = entries[index].path, entries[index].entry
        local id = tostring(index)
        self.problemRows[id] = { path = path, line = entry.line, column = entry.column }
        rows[#rows + 1] = { id = id, cells = { fileName(path), { text = tostring(entry.line), monospace = true }, entry.code, entry.source, { text = entry.message, tone = severityTones[entry.severity] } } }

        for related, information in ipairs(entry.related) do
            local relatedId = id .. "." .. related
            self.problemRows[relatedId] = { path = information.path, line = information.line, column = information.column }
            rows[#rows + 1] = { id = relatedId, cells = { { text = fileName(information.path), muted = true }, { text = tostring(information.line), monospace = true, muted = true }, "", "", { text = information.message, muted = true } } }
        end
    end

    self.nodes.problems:set({ rows = rows })
    self:renderTabs()
end

-- References and calls share one tab, whose title says which of them it shows.
function Panels:setReferences(kind, places)
    local limit = catalog.limit("maximumReferences")
    self.referenceKind = kind
    self.referencesCapped = #places > limit
    self.references = {}

    for index = 1, math.min(#places, limit) do
        self.references[index] = places[index]
    end

    self:renderReferences()
    self.nodes.bottom:set({ current = "references" })
end

function Panels:renderReferences()
    local rows = {}

    for index, place in ipairs(self.references) do
        local context = place.name ~= nil and (place.detail ~= nil and place.detail ~= "" and (place.name .. "   " .. place.detail) or place.name) or (paths.inside(self.root, place.path) and paths.relative(self.root, place.path) or place.path)
        rows[index] = { id = tostring(index), cells = { fileName(place.path), { text = tostring(place.line), monospace = true }, context } }
    end

    self.nodes.references:set({ rows = rows })
    self:renderTabs()
end

-- A search runs on Enter, and a newer search replaces the results of an older one that answers late.
function Panels:search(query)
    local wanted = query:match("^%s*(.-)%s*$")
    self.revision = self.revision + 1
    local revision = self.revision

    if wanted == "" then
        self.searchRows = {}
        self.searchCount = 0
        self.searchComplete = true
        self.nodes.search:set({ rows = {} })
        self:renderTabs()
        return
    end

    local found = workpane.await(workpane.files.search(self.root, { text = wanted, maximumMatches = catalog.limit("maximumSearchMatches"), maximumFileBytes = catalog.limit("maximumFileBytes"), skip = { ".git" } }))

    if revision ~= self.revision then
        return
    end

    local rows = {}
    self.searchRows = {}

    for index, match in ipairs(found.matches) do
        self.searchRows[index] = { path = paths.join(self.root, match.path), line = match.line }
        rows[index] = { id = tostring(index), cells = { fileName(match.path), { text = tostring(match.line), monospace = true }, { text = match.text, monospace = true } } }
    end

    self.searchCount = #rows
    self.searchComplete = found.complete
    self.nodes.search:set({ rows = rows })
    self.nodes.bottom:set({ current = "search" })
    self:renderTabs()
end

-- The outline of the active document shows while the symbol search is empty, and the symbols of the folder replace it while the reader types.
function Panels:setOutline(items)
    self.outline = items
    self:renderSymbols()
end

function Panels:searchSymbols(query)
    self.symbolQuery = query:match("^%s*(.-)%s*$")
    self.symbolResults = {}
    self:renderSymbols()

    if self.symbolQuery ~= "" then
        self.handlers.workspaceSymbols(self.symbolQuery)
    end
end

function Panels:addSymbols(query, found)
    if query ~= self.symbolQuery then
        return
    end

    for _, symbol in ipairs(found) do
        self.symbolResults[#self.symbolResults + 1] = symbol
    end

    self:renderSymbols()
end

-- The tree receives only the fields it knows with the icon of the kind of each symbol, and the place of every symbol stays in the index beside it.
local function treeItems(symbols, index)
    local items = {}

    for position, symbol in ipairs(symbols) do
        local icon, tone = catalog.symbolIcon(symbol.kind)
        index[symbol.id] = { line = symbol.line, column = symbol.column }
        items[position] = { id = symbol.id, text = symbol.text, icon = icon, iconColor = tone, expanded = true, children = treeItems(symbol.children or {}, index) }
    end

    return items
end

function Panels:renderSymbols()
    if self.symbolQuery == "" then
        self.symbolIndex = {}
        self.nodes.symbols:set({ items = treeItems(self.outline, self.symbolIndex) })
        return
    end

    local items = {}
    self.symbolIndex = {}

    for index, symbol in ipairs(self.symbolResults) do
        local id = "symbol:" .. index
        local icon, tone = catalog.symbolIcon(symbol.kind)
        items[index] = { id = id, text = symbol.name, icon = icon, iconColor = tone }
        self.symbolIndex[id] = { path = symbol.path, line = symbol.line, character = symbol.character }
    end

    self.nodes.symbols:set({ items = items })
end

function Panels:openSymbol(id)
    local symbol = (self.symbolIndex or {})[id]

    if symbol == nil then
        return
    end

    if symbol.path ~= nil then
        self.handlers.open(symbol.path, symbol.line, (symbol.character or 0) + 1)
        return
    end

    self.handlers.reveal(symbol.line, symbol.column)
end

return panels
