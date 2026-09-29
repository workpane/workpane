-- The language servers of one open folder: one per language, started with the first document of that language, fed every change and asked for what the editor shows.
local async = require("async")
local catalog = include("catalog")
local lsp = include("lsp")
local paths = include("paths")
local preferences = include("preferences")
local servers = include("servers")

local translate = workpane.i18n.translate

local analysis = {}

local Analysis = {}
Analysis.__index = Analysis

local maximumSymbolDepth = 64

-- Handlers hear diagnostics per file, the outline and highlights of a document, the analysis progress and any change of what the servers offer.
function analysis.new(root, handlers)
    return setmetatable({ root = root, handlers = handlers, clients = {}, stopping = {}, tracked = {}, revisions = {}, analyzed = {}, stopped = false }, Analysis)
end

local function lines(text)
    local list = {}

    for line in (text .. "\n"):gmatch("(.-)\n") do
        list[#list + 1] = line
    end

    return list
end

-- Answers a reader of the lines of a document that finds each line it is asked for once, so a request near the cursor never splits the whole text.
local function lineReader(document)
    local found = {}

    return function(number)
        if found[number] == nil then
            found[number] = document:line(number) or ""
        end

        return found[number]
    end
end

-- A position of the protocol counts from zero in UTF-16 units, and a position of the editor counts from one in characters.
local function toEditor(text, position)
    return position.line + 1, lsp.fromUnits(text or "", position.character)
end

local function location(entry)
    local uri = entry.targetUri or entry.uri
    local range = entry.targetSelectionRange or entry.range

    local path = workpane.files.path(uri)

    if path == nil or type(range) ~= "table" then
        return nil
    end

    return { path = path, line = range.start.line + 1, character = range.start.character }
end

-- A stopped analysis starts no server, since the folder it belongs to closed.
function Analysis:client(document)
    local id = document.language.id

    if self.stopped or not preferences.get("languageServers") or id == "plaintext" then
        return nil
    end

    if self.clients[id] ~= nil then
        return self.clients[id]
    end

    local executable = servers.executable(id)

    if executable == nil then
        return nil
    end

    local client

    client = lsp.new(self.root, id, executable.path, executable.arguments, {
        notification = function(method, params)
            self:notification(method, params, id)
        end,
        ready = function()
            self.handlers.changed()

            for tracked in pairs(self.tracked) do
                if tracked.language.id == id then
                    self:analyze(tracked)
                end
            end
        end,
        log = function(text)
            workpane.log.debug("lsp", text, { language = id })
        end,
        failed = function(failure)
            workpane.log.error("lsp", failure.detail, { language = id })
            workpane.notify.error(translate("code-editor.error.title"), failure.argument ~= nil and translate(failure.key, failure.argument) or translate(failure.key))
            self:forget(id)
        end,
        stopped = function() end,
    })

    self.clients[id] = client
    local started, failure = pcall(client.start, client)

    if not started then
        self.clients[id] = nil
        workpane.log.error("lsp", "The language server could not be started", { language = id, code = failure.code, detail = failure.detail })
        workpane.notify.error(translate("code-editor.error.title"), translate("code-editor.error.language-server-start", executable.path))
        return nil
    end

    return client
end

-- A server given up takes its diagnostics with it, and the next document of its language starts a new one.
function Analysis:forget(id)
    local client = self.clients[id]

    if client == nil then
        return
    end

    -- A client going away reaches the view no more, a progress it began ends with it and its documents open again with the next server of their language.
    self.clients[id] = nil
    self.stopping[client] = true
    client.handlers.notification = function() end
    client.handlers.ready = function() end
    client.handlers.failed = function() end
    client.handlers.stopped = function()
        self.stopping[client] = nil
    end

    for document in pairs(self.tracked) do
        if document.language.id == id then
            self.tracked[document] = nil
        end
    end

    client:stop()
    self.handlers.progress("")
    self.handlers.dropped(id)
    self.handlers.changed()
end

local function sameArguments(first, second)
    if #first ~= #second then
        return false
    end

    for index, argument in ipairs(first) do
        if second[index] ~= argument then
            return false
        end
    end

    return true
end

-- The servers follow the settings: every server goes when they are turned off, a server whose program changed starts again, and a document without a server opens with one.
function Analysis:refresh(documents)
    for id, client in pairs(self.clients) do
        local executable = preferences.get("languageServers") and servers.executable(id) or nil

        if executable == nil or executable.path ~= client.executable or not sameArguments(executable.arguments, client.arguments) then
            self:forget(id)
        end
    end

    for _, document in ipairs(documents) do
        if not self.tracked[document] then
            self:open(document)
        end
    end
end

function Analysis:open(document)
    local client = self:client(document)

    if client == nil then
        return
    end

    self.tracked[document] = true
    client:open(workpane.files.uri(document.path), catalog.protocolId(document.language, document.path), document.text)
    self:analyze(document)
end

-- An edit reaches the server once typing pauses, and the analysis follows once it pauses a little longer.
function Analysis:changed(document)
    local client = self.clients[document.language.id]

    if client == nil or not self.tracked[document] then
        return
    end

    local revision = (self.revisions[document] or 0) + 1
    self.revisions[document] = revision

    workpane.task(function()
        async.sleep(catalog.limit("changeDebounceMs")):await()

        if self.revisions[document] ~= revision then
            return
        end

        client:change(workpane.files.uri(document.path), document.text)
        async.sleep(catalog.limit("analysisDebounceMs") - catalog.limit("changeDebounceMs")):await()

        if self.revisions[document] == revision then
            self:analyze(document)
        end
    end)
end

function Analysis:flush(document)
    local client = self.clients[document.language.id]

    if client ~= nil and self.tracked[document] then
        client:change(workpane.files.uri(document.path), document.text)
    end

    return client
end

function Analysis:saved(document)
    local client = self:flush(document)

    if client ~= nil then
        client:saved(workpane.files.uri(document.path))
    end
end

function Analysis:closed(document)
    local client = self.clients[document.language.id]
    self.tracked[document] = nil
    self.revisions[document] = nil
    self.analyzed[document] = nil

    if client ~= nil then
        client:close(workpane.files.uri(document.path))
    end
end

-- A document that moved closes under its old address and opens again under the new one, with the server of its new language.
function Analysis:moved(document, previousPath, previousLanguage)
    local client = self.clients[previousLanguage]
    self.tracked[document] = nil
    self.revisions[document] = nil
    self.analyzed[document] = nil

    if client ~= nil then
        client:close(workpane.files.uri(previousPath))
    end

    self:open(document)
end

function Analysis:supports(document, method)
    local client = self.clients[document.language.id]
    return client ~= nil and client:provides(method)
end

function Analysis:active()
    return preferences.get("languageServers") and servers.any()
end

-- A symbol is named by the name of its parent and its own place among its siblings, so two symbols never share a name in the outline.
local function symbols(entries, text, depth, parent)
    local items = {}

    for index, entry in ipairs(entries or {}) do
        if depth > maximumSymbolDepth then
            break
        end

        local range = entry.selectionRange or (entry.location or {}).range

        if type(entry.name) == "string" and type(range) == "table" then
            local line = range.start.line + 1
            local column = lsp.fromUnits(text[line] or "", range.start.character)
            local label = entry.detail ~= nil and entry.detail ~= "" and (entry.name .. "  " .. entry.detail) or entry.name
            local id = parent .. "/" .. index .. ":" .. entry.name
            items[#items + 1] = { id = id, text = label, kind = entry.kind, line = line, column = column, expanded = true, children = symbols(entry.children, text, depth + 1, id) }
        end
    end

    return items
end

-- Semantic tokens arrive as five numbers per token relative to the one before, only token types the catalog maps to a role are painted, and a token marked deprecated is struck through.
local function highlights(data, legend, text, maximumLines)
    local list = {}
    local struck = {}
    local line = 0
    local start = 0
    local deprecated = 0

    for index, modifier in ipairs(type(legend.tokenModifiers) == "table" and legend.tokenModifiers or {}) do
        deprecated = modifier == "deprecated" and 1 << (index - 1) or deprecated
    end

    for index = 1, #data - 4, 5 do
        local deltaLine, deltaStart, length, tokenType, modifiers = data[index], data[index + 1], data[index + 2], data[index + 3], data[index + 4]
        start = deltaLine == 0 and start + deltaStart or deltaStart
        line = line + deltaLine

        if line >= maximumLines then
            break
        end

        local role = catalog.semanticRole(legend.tokenTypes[tokenType + 1] or "")
        local content = text[line + 1] or ""
        local first = lsp.fromUnits(content, start)
        local last = math.max(first + 1, lsp.fromUnits(content, start + length))

        if role ~= nil and length > 0 then
            list[#list + 1] = { line = line + 1, column = first, length = last - first, role = role }
        end

        if deprecated ~= 0 and math.type(modifiers) == "integer" and modifiers & deprecated ~= 0 and length > 0 then
            struck[#struck + 1] = { line = line + 1, column = first, endLine = line + 1, endColumn = last, style = "struck" }
        end
    end

    return list, struck
end

-- The outline, the pulled diagnostics and the semantic tokens of a document are asked for together whenever it settles.
function Analysis:analyze(document)
    local client = self.clients[document.language.id]

    if client == nil or not client.ready or not self.tracked[document] then
        return
    end

    local uri = workpane.files.uri(document.path)
    local text = lines(document.text)
    self.analyzed[document] = self.revisions[document] or 0

    if client:provides("textDocument/documentSymbol") then
        client:request("textDocument/documentSymbol", { textDocument = { uri = uri } }, function(result)
            self.handlers.outline(document, symbols(result, text, 1, ""))
        end, "symbols:" .. uri)
    end

    if client:provides("textDocument/diagnostic") then
        client:request("textDocument/diagnostic", { textDocument = { uri = uri } }, function(result)
            if type(result) == "table" and result.kind == "full" then
                self:diagnostics(document.path, document.language.id, result.items, text)
            end
        end, "diagnostic:" .. uri)
    end

    local provider = client:provider("textDocument/semanticTokens/full")

    if client:provides("textDocument/semanticTokens/full") and #text <= catalog.limit("maximumSemanticTokenLines") then
        client:request("textDocument/semanticTokens/full", { textDocument = { uri = uri } }, function(result)
            if type(result) == "table" and type(result.data) == "table" then
                local painted, struck = highlights(result.data, provider.legend, text, catalog.limit("maximumSemanticTokenLines"))
                self.handlers.highlights(document, painted, struck)
            end
        end, "tokens:" .. uri)
    end
end

-- A document coming on screen is analyzed again only when it changed since it was last analyzed.
function Analysis:revisit(document)
    if self.analyzed[document] ~= (self.revisions[document] or 0) then
        self:analyze(document)
    end
end

-- Diagnostics keep their range, severity, code, origin and related places, and positions are counted in characters when the file is open.
function Analysis:diagnostics(path, language, items, text)
    if not paths.inside(self.root, path) then
        return
    end

    local list = {}

    for _, item in ipairs(items or {}) do
        local range = item.range or { start = { line = 0, character = 0 } }
        local finish = range["end"] or range.start
        local line, column = toEditor(text and text[range.start.line + 1], range.start)
        local endLine, endColumn = toEditor(text and text[finish.line + 1], finish)
        local related = {}

        for _, information in ipairs(item.relatedInformation or {}) do
            local place = location(information.location or {})

            if place ~= nil then
                related[#related + 1] = { path = place.path, line = place.line, column = place.character + 1, message = information.message or "" }
            end
        end

        local tags = {}

        for _, tag in ipairs(type(item.tags) == "table" and item.tags or {}) do
            tags[tag] = true
        end

        list[#list + 1] = { line = line, column = column, endLine = endLine, endColumn = endColumn, severity = item.severity or 1, code = item.code ~= nil and tostring(item.code) or "", source = item.source or "", message = item.message or "", related = related, unnecessary = tags[1] == true, deprecated = tags[2] == true }
    end

    self.handlers.diagnostics(path, language, list)
end

function Analysis:notification(method, params, language)
    if method == "textDocument/publishDiagnostics" then
        local path = workpane.files.path(params.uri)

        if path == nil then
            return
        end

        local text

        for tracked in pairs(self.tracked) do
            text = tracked.path == path and lines(tracked.text) or text
        end

        self:diagnostics(path, language, params.diagnostics, text)
    elseif method == "$/progress" and type(params.value) == "table" then
        local value = params.value
        self.handlers.progress(value.kind ~= "end" and ((value.title or "") .. " " .. (value.message or "")) or "")
    elseif method == "window/showMessage" and params.type == 1 then
        workpane.notify.error(translate("code-editor.error.title"), translate("code-editor.error.language-server") .. "\n" .. tostring(params.message))
    elseif method == "window/showMessage" or method == "window/logMessage" then
        workpane.log.debug("lsp", tostring(params.message), { language = language })
    end
end

local function position(document)
    return { line = document.cursor.line - 1, character = lsp.toUnits(document:line(document.cursor.line) or "", document.cursor.column) }
end

-- A range of the protocol becomes the lines and columns of the editor, and anything that is not one becomes nothing.
local function placed(text, range)
    if type(range) ~= "table" or type(range.start) ~= "table" or type(range["end"]) ~= "table" then
        return nil
    end

    local line, column = toEditor(text[range.start.line + 1], range.start)
    local endLine, endColumn = toEditor(text[range["end"].line + 1], range["end"])
    return { line = line, column = column, endLine = endLine, endColumn = endColumn }
end

-- Proposals come in the order the server sorts them, bounded, each with its label, the text it inserts, its detail, its documentation and the range it replaces.
function Analysis:complete(document, request, callback)
    local client = self:flush(document)

    if client == nil or not client:provides("textDocument/completion") then
        callback({})
        return
    end

    local text = lines(document.text)
    local at = { line = request.line - 1, character = lsp.toUnits(text[request.line] or "", request.column) }

    client:request("textDocument/completion", { textDocument = { uri = workpane.files.uri(document.path) }, position = at, context = { triggerKind = 1 } }, function(result)
        local entries = type(result) == "table" and (result.items or result) or {}
        local sorted = {}

        for _, entry in ipairs(entries) do
            if type(entry.label) == "string" and entry.label ~= "" then
                sorted[#sorted + 1] = entry
            end
        end

        table.sort(sorted, function(first, second)
            return (first.sortText or first.label) < (second.sortText or second.label)
        end)

        local proposals = {}
        local seen = {}

        for _, entry in ipairs(sorted) do
            local edit = type(entry.textEdit) == "table" and entry.textEdit or nil
            local insert = (edit ~= nil and edit.newText) or entry.insertText or entry.label
            local documentation = type(entry.documentation) == "table" and entry.documentation.value or entry.documentation
            local range = edit ~= nil and (edit.range or edit.insert) or nil

            if not seen[entry.label .. "\0" .. insert] and #proposals < catalog.limit("maximumCompletions") then
                seen[entry.label .. "\0" .. insert] = true
                proposals[#proposals + 1] = { label = entry.label, insert = insert, detail = type(entry.detail) == "string" and entry.detail or "", documentation = type(documentation) == "string" and documentation or "", range = placed(text, range) }
            end
        end

        callback(proposals)
    end, "completion:" .. workpane.files.uri(document.path))
end

-- The characters the server names for completion ask for proposals as soon as they are typed, bounded to the few the editor accepts.
function Analysis:completionTriggers(document)
    local client = self.clients[document.language.id]

    if client == nil or not client:provides("textDocument/completion") then
        return {}
    end

    local triggers = {}

    for _, trigger in ipairs(client:provider("textDocument/completion").triggerCharacters or {}) do
        if type(trigger) == "string" and trigger ~= "" and #trigger <= 4 and #triggers < 16 then
            triggers[#triggers + 1] = trigger
        end
    end

    return triggers
end

-- Hover text is plain, from a string, a value or a list of them, and signature help adds the active signature below it.
function Analysis:hover(document, request, callback)
    local client = self:flush(document)

    if client == nil or not client:provides("textDocument/hover") then
        return
    end

    local at = { line = request.line - 1, character = lsp.toUnits(document:line(request.line) or "", request.column) }

    client:request("textDocument/hover", { textDocument = { uri = workpane.files.uri(document.path) }, position = at }, function(result)
        local contents = type(result) == "table" and result.contents or nil
        local parts = {}

        for _, part in ipairs(type(contents) == "table" and contents[1] ~= nil and contents or { contents }) do
            local value = type(part) == "string" and part or type(part) == "table" and part.value or nil

            if value ~= nil and value ~= "" then
                parts[#parts + 1] = value
            end
        end

        if #parts > 0 then
            callback(table.concat(parts, "\n"))
        end
    end, "hover:" .. workpane.files.uri(document.path))
end

-- The other uses of the symbol under the cursor come from the server as ranges, which the editor tints, asked of the text the server already has so a move of the cursor never sends the document again.
function Analysis:occurrences(document, callback)
    local client = self.clients[document.language.id]

    if client == nil or not self.tracked[document] or not client:provides("textDocument/documentHighlight") then
        callback({})
        return
    end

    local lineOf = lineReader(document)

    client:request("textDocument/documentHighlight", { textDocument = { uri = workpane.files.uri(document.path) }, position = position(document) }, function(result)
        local ranges = {}

        for _, highlight in ipairs(type(result) == "table" and result or {}) do
            local range = highlight.range

            if type(range) == "table" and type(range.start) == "table" and type(range["end"]) == "table" then
                local line, column = toEditor(lineOf(range.start.line + 1), range.start)
                local endLine, endColumn = toEditor(lineOf(range["end"].line + 1), range["end"])
                ranges[#ranges + 1] = { line = line, column = column, endLine = endLine, endColumn = endColumn, style = "occurrence" }
            end
        end

        callback(ranges)
    end, "occurrences:" .. workpane.files.uri(document.path))
end

-- Answers whether a character the reader typed is one the server of the document names for signature help.
function Analysis:triggersSignature(document, character)
    local client = self.clients[document.language.id]

    if client == nil or not client:provides("textDocument/signatureHelp") then
        return false
    end

    for _, trigger in ipairs(client:provider("textDocument/signatureHelp").triggerCharacters or {}) do
        if trigger == character then
            return true
        end
    end

    return false
end

-- The active signature is clamped to the signatures the server answered, and an answer without one clears it.
function Analysis:signature(document, callback)
    local client = self:flush(document)

    if client == nil or not client:provides("textDocument/signatureHelp") then
        return
    end

    client:request("textDocument/signatureHelp", { textDocument = { uri = workpane.files.uri(document.path) }, position = position(document) }, function(result)
        local signatures = type(result) == "table" and type(result.signatures) == "table" and result.signatures or {}
        local active = math.max(1, math.min(#signatures, (type(result) == "table" and math.type(result.activeSignature) == "integer" and result.activeSignature or 0) + 1))
        callback(signatures[active] ~= nil and tostring(signatures[active].label) or "")
    end, "signature:" .. workpane.files.uri(document.path))
end

-- The first place a server names is opened for a definition, a declaration, a type or an implementation.
function Analysis:locate(document, method, callback)
    local client = self:flush(document)

    if client == nil or not client:provides(method) then
        return
    end

    client:request(method, { textDocument = { uri = workpane.files.uri(document.path) }, position = position(document) }, function(result)
        local entries = type(result) == "table" and (result.uri ~= nil and { result } or result) or {}
        local place = entries[1] ~= nil and location(entries[1]) or nil

        if place ~= nil then
            callback(place)
        end
    end)
end

function Analysis:references(document, callback)
    local client = self:flush(document)

    if client == nil or not client:provides("textDocument/references") then
        return
    end

    client:request("textDocument/references", { textDocument = { uri = workpane.files.uri(document.path) }, position = position(document), context = { includeDeclaration = true } }, function(result)
        local places = {}

        for _, entry in ipairs(type(result) == "table" and result or {}) do
            places[#places + 1] = location(entry)
        end

        callback(places)
    end)
end

-- Calls are asked of the first item the server prepares at the cursor, from its callers or to its callees.
function Analysis:calls(document, incoming, callback)
    local client = self:flush(document)

    if client == nil or not client:provides("textDocument/prepareCallHierarchy") then
        return
    end

    client:request("textDocument/prepareCallHierarchy", { textDocument = { uri = workpane.files.uri(document.path) }, position = position(document) }, function(prepared)
        local item = type(prepared) == "table" and prepared[1] or nil

        if item == nil then
            callback({})
            return
        end

        client:request(incoming and "callHierarchy/incomingCalls" or "callHierarchy/outgoingCalls", { item = item }, function(result)
            local calls = {}

            for _, call in ipairs(type(result) == "table" and result or {}) do
                local target = incoming and call.from or call.to
                local path = type(target) == "table" and workpane.files.path(target.uri) or nil

                if path ~= nil then
                    local range = target.selectionRange or target.range
                    calls[#calls + 1] = { name = target.name, detail = target.detail, path = path, line = range.start.line + 1, character = range.start.character }
                end
            end

            callback(calls)
        end)
    end)
end

-- Workspace symbols are asked of every server of the folder, and each answer adds to the list as it arrives.
function Analysis:workspaceSymbols(query, callback)
    for id, client in pairs(self.clients) do
        if client:provides("workspace/symbol") then
            client:request("workspace/symbol", { query = query }, function(result)
                local found = {}

                for _, entry in ipairs(type(result) == "table" and result or {}) do
                    local place = location(entry.location or {})

                    if place ~= nil then
                        found[#found + 1] = { name = entry.containerName ~= nil and entry.containerName ~= "" and (entry.containerName .. "::" .. entry.name) or entry.name, kind = entry.kind, path = place.path, line = place.line, character = place.character }
                    end
                end

                callback(id, found)
            end, "workspace-symbols")
        end
    end
end

-- Files created, removed or moved in the tree are told to every server, which watches its folder through the editor.
function Analysis:watched(changes)
    for _, client in pairs(self.clients) do
        if client.ready then
            client:notify("workspace/didChangeWatchedFiles", { changes = changes })
        end
    end
end

-- Answers whether every server this folder asked to stop was told to exit or ended.
function Analysis:settled()
    return next(self.stopping) == nil
end

-- A stopped analysis ends its servers and starts no other, since its folder closed.
function Analysis:stop()
    self.stopped = true

    for id in pairs(self.clients) do
        self:forget(id)
    end

    self.tracked = {}
    self.revisions = {}
    self.analyzed = {}
end

return analysis
