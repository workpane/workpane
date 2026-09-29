-- A client of one language server for one open folder: it starts the server, speaks JSON-RPC in Content-Length frames, initializes it, keeps its documents in step and restarts it within a budget.
local async = require("async")
local json = require("json")
local catalog = include("catalog")

local lsp = {}

local Client = {}
Client.__index = Client

local maximumHeaderBytes = 8192
local maximumMessageBytes = 16 * 1024 * 1024
local stopGraceMilliseconds = 2000
local maximumLogLine = 65536

local tokenTypes = { "namespace", "type", "class", "enum", "interface", "struct", "typeParameter", "parameter", "variable", "property", "enumMember", "event", "function", "method", "macro", "keyword", "modifier", "comment", "string", "number", "regexp", "operator", "decorator", "label", "concept" }
local tokenModifiers = { "declaration", "definition", "readonly", "static", "deprecated", "abstract", "async", "modification", "documentation", "defaultLibrary" }
local symbolKinds = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26 }

-- A position in the editor counts characters, and the protocol counts UTF-16 units, which differ for characters beyond the first plane.
function lsp.toUnits(text, column)
    local units = 0
    local counted = 1

    for _, code in utf8.codes(text) do
        if counted >= column then
            break
        end

        units = units + (code >= 0x10000 and 2 or 1)
        counted = counted + 1
    end

    return units + math.max(0, column - counted)
end

function lsp.fromUnits(text, character)
    local units = 0
    local column = 1

    for _, code in utf8.codes(text) do
        if units >= character then
            return column
        end

        units = units + (code >= 0x10000 and 2 or 1)
        column = column + 1
    end

    return column + math.max(0, character - units)
end

-- What the server writes to its error stream reaches the log one line at a time, so an entry never stops in the middle of a line, and a line without end is cut at a bound.
local function logLines(client, text)
    local buffer = client.errors .. text
    local start = 1

    while true do
        local newline = buffer:find("\n", start, true)

        if newline == nil then
            break
        end

        local line = buffer:sub(start, newline - 1):gsub("\r$", "")
        start = newline + 1

        if line:match("%S") then
            client.handlers.log(line)
        end
    end

    client.errors = buffer:sub(start)

    if #client.errors > maximumLogLine then
        client.handlers.log(client.errors)
        client.errors = ""
    end
end

local function exited(client, code)
    client.process = nil
    client.ready = false

    if client.errors:match("%S") then
        client.handlers.log(client.errors)
    end

    client.errors = ""

    for _, callback in pairs(client.pending) do
        workpane.task(callback, nil, { code = -32099, message = "The language server ended" })
    end

    client.pending = {}

    if client.stopping then
        client.handlers.stopped()
        return
    end

    -- A server that ends by itself is started again at most a few times within a window, and a server past its budget is given up.
    local now = os.time()
    local recent = {}

    for _, moment in ipairs(client.restarts) do
        if now - moment < catalog.limit("restartWindowMs") / 1000 then
            recent[#recent + 1] = moment
        end
    end

    client.restarts = recent

    if #recent >= catalog.limit("maximumRestarts") then
        client.handlers.failed({ key = "code-editor.lsp.restart-limit", argument = tostring(code), detail = "The language server exited with code " .. code .. " and reached its restart limit" })
        return
    end

    client.restarts[#client.restarts + 1] = now
    client.handlers.log("The language server exited with code " .. code .. " and is being started again")
    local started, failure = pcall(client.start, client)

    if not started then
        client.handlers.failed({ key = "code-editor.lsp.restart-failed", detail = type(failure) == "table" and tostring(failure.message) or tostring(failure) })
    end
end

local function answer(client, id, result)
    client:sendRaw('{"jsonrpc":"2.0","id":' .. json.encode(id) .. ',"result":' .. result .. "}")
end

local function refuse(client, id)
    client:send({ jsonrpc = "2.0", id = id, error = { code = -32601, message = "Method not found" } })
end

-- The client declares no dynamic registration, so a registration a server sends anyway is acknowledged and changes nothing.
local function serverRequest(client, message)
    local method = message.method

    if method == "workspace/configuration" then
        answer(client, message.id, "[" .. string.rep("null", #((message.params or {}).items or {}), ",") .. "]")
    elseif method == "client/registerCapability" or method == "client/unregisterCapability" then
        answer(client, message.id, "null")
    elseif method == "window/workDoneProgress/create" then
        answer(client, message.id, "null")
    elseif method == "workspace/workspaceFolders" then
        client:send({ jsonrpc = "2.0", id = message.id, result = { { uri = workpane.files.uri(client.root), name = client.root:match("[^/\\]+$") } } })
    else
        refuse(client, message.id)
    end
end

local function dispatch(client, message)
    if type(message) ~= "table" or message.jsonrpc ~= "2.0" then
        client:abort("The language server returned an invalid JSON-RPC version")
        return
    end

    if message.method ~= nil and message.id ~= nil then
        serverRequest(client, message)
        return
    end

    -- A notification and an answer run in a task of their own, so a handler that waits never holds the reading of the output and one that fails is written to the log.
    if message.method ~= nil then
        workpane.task(client.handlers.notification, message.method, message.params or {})
        return
    end

    local callback = math.type(message.id) == "integer" and client.pending[message.id] or nil

    if callback ~= nil then
        client.pending[message.id] = nil
        workpane.task(callback, message.result, message.error)
    end
end

-- The output is kept in pieces joined only once a whole frame can be cut from it, frames are cut from a moving offset, and a frame that breaks the protocol ends the server.
local function receive(client, text)
    client.pieces[#client.pieces + 1] = text
    client.received = client.received + #text

    if client.received < client.needed then
        return
    end

    local buffer = table.concat(client.pieces)
    local offset = 1
    client.needed = 0

    while true do
        local headerEnd = buffer:find("\r\n\r\n", offset, true)

        if headerEnd == nil then
            if #buffer - offset + 1 > maximumHeaderBytes then
                client:abort("The language server response header exceeded the permitted size")
                return
            end

            break
        end

        if headerEnd - offset > maximumHeaderBytes then
            client:abort("The language server response header exceeded the permitted size")
            return
        end

        local header = buffer:sub(offset, headerEnd - 1):lower()
        local lengths = {}

        for value in header:gmatch("content%-length:%s*([^\r\n]*)") do
            lengths[#lengths + 1] = value
        end

        local length = #lengths == 1 and math.tointeger(tonumber(lengths[1])) or nil

        if #lengths == 0 then
            client:abort("The language server response omitted its content length")
            return
        end

        if #lengths > 1 then
            client:abort("The language server returned duplicate content lengths")
            return
        end

        if length == nil or length < 0 or length > maximumMessageBytes then
            client:abort("The language server returned an invalid content length")
            return
        end

        local finish = headerEnd + 3 + length

        if #buffer < finish then
            client.needed = finish - offset + 1
            break
        end

        local body = buffer:sub(headerEnd + 4, finish)
        offset = finish + 1
        local decoded, message = pcall(json.decode, body)

        if not decoded then
            client:abort("The language server returned invalid JSON")
            return
        end

        local dispatched, failure = pcall(dispatch, client, message)

        if not dispatched then
            workpane.log.error("lsp", "A message of the language server could not be handled", { language = client.language, error = tostring(failure) })
        end
    end

    local rest = buffer:sub(offset)
    client.pieces = rest ~= "" and { rest } or {}
    client.received = #rest
end

-- Handlers receive notifications, the moment the server is ready, changes of its capabilities, log lines, a failure it cannot recover from and the moment it was told to exit or ended.
function lsp.new(root, language, executable, arguments, handlers)
    return setmetatable({ root = root, language = language, executable = executable, arguments = arguments, handlers = handlers, pending = {}, keyed = {}, documents = {}, restarts = {}, pieces = {}, received = 0, needed = 0, errors = "", nextId = 0, providers = {}, sync = 1, openClose = true, ready = false, stopping = false }, Client)
end

-- The chunks of one batch of output are joined before they are read, so the bytes of a later batch never land inside an earlier one.
function Client:start()
    self.pieces = {}
    self.received = 0
    self.needed = 0
    self.ready = false
    self.process = workpane.process.start({ program = self.executable, arguments = self.arguments, directory = self.root, onOutput = function(chunks)
        local output = {}
        local errors = {}

        for _, chunk in ipairs(chunks) do
            local list = chunk.stream == "output" and output or errors
            list[#list + 1] = chunk.text
        end

        if #output > 0 then
            receive(self, table.concat(output))
        end

        if #errors > 0 then
            logLines(self, table.concat(errors))
        end
    end, onExit = function(code)
        exited(self, code)
    end })

    self:initialize()
end

function Client:sendRaw(text)
    if self.process ~= nil then
        pcall(workpane.process.write, self.process, "Content-Length: " .. #text .. "\r\n\r\n" .. text)
    end
end

function Client:send(message)
    self:sendRaw(json.encode(message))
end

function Client:notify(method, params)
    self:send({ jsonrpc = "2.0", method = method, params = params })
end

-- A request with a key replaces the unanswered request of the same key, which the server is told to cancel.
function Client:request(method, params, callback, key)
    self.nextId = self.nextId + 1
    local id = self.nextId

    if key ~= nil and self.keyed[key] ~= nil and self.pending[self.keyed[key]] ~= nil then
        self.pending[self.keyed[key]] = nil
        self:notify("$/cancelRequest", { id = self.keyed[key] })
    end

    if key ~= nil then
        self.keyed[key] = id
    end

    self.pending[id] = callback
    self:send({ jsonrpc = "2.0", id = id, method = method, params = params })

    return id
end

-- A server that breaks the protocol is ended, and what it broke is written to the log for whoever looks.
function Client:abort(reason)
    self.pieces = {}
    self.received = 0
    self.needed = 0
    self.handlers.failed({ key = "code-editor.lsp.protocol", detail = reason })

    if self.process ~= nil then
        workpane.process.stop(self.process)
    end
end

local function capabilities()
    return {
        textDocument = {
            synchronization = { dynamicRegistration = false, didSave = true },
            completion = { dynamicRegistration = false, contextSupport = true, completionItem = { snippetSupport = false, documentationFormat = { "plaintext" } } },
            hover = { dynamicRegistration = false, contentFormat = { "plaintext" } },
            definition = { dynamicRegistration = false, linkSupport = true },
            declaration = { dynamicRegistration = false, linkSupport = true },
            typeDefinition = { dynamicRegistration = false, linkSupport = true },
            implementation = { dynamicRegistration = false, linkSupport = true },
            references = { dynamicRegistration = false },
            documentHighlight = { dynamicRegistration = false },
            documentSymbol = { dynamicRegistration = false, hierarchicalDocumentSymbolSupport = true, symbolKind = { valueSet = symbolKinds } },
            signatureHelp = { dynamicRegistration = false, signatureInformation = { documentationFormat = { "plaintext" }, parameterInformation = { labelOffsetSupport = false }, activeParameterSupport = true } },
            semanticTokens = { dynamicRegistration = false, requests = { full = true }, tokenTypes = tokenTypes, tokenModifiers = tokenModifiers, formats = { "relative" } },
            diagnostic = { dynamicRegistration = false, relatedDocumentSupport = false, relatedInformation = true, tagSupport = { valueSet = { 1, 2 } } },
            publishDiagnostics = { relatedInformation = true, tagSupport = { valueSet = { 1, 2 } } },
            callHierarchy = { dynamicRegistration = false },
        },
        workspace = { workspaceFolders = true, configuration = true, symbol = { dynamicRegistration = false, symbolKind = { valueSet = symbolKinds } }, didChangeWatchedFiles = { dynamicRegistration = false } },
        window = { workDoneProgress = true },
        general = { positionEncodings = { "utf-16" } },
    }
end

local function readCapabilities(client, server)
    local sync = server.textDocumentSync

    if math.type(sync) == "integer" then
        client.sync = sync
    elseif type(sync) == "table" then
        client.sync = sync.change or 1
        client.openClose = sync.openClose ~= false
        client.save = sync.save == true and { includeText = false } or type(sync.save) == "table" and { includeText = sync.save.includeText == true } or nil
    end

    local providers = {
        ["textDocument/completion"] = server.completionProvider,
        ["textDocument/hover"] = server.hoverProvider,
        ["textDocument/definition"] = server.definitionProvider,
        ["textDocument/declaration"] = server.declarationProvider,
        ["textDocument/typeDefinition"] = server.typeDefinitionProvider,
        ["textDocument/implementation"] = server.implementationProvider,
        ["textDocument/references"] = server.referencesProvider,
        ["textDocument/documentHighlight"] = server.documentHighlightProvider,
        ["textDocument/documentSymbol"] = server.documentSymbolProvider,
        ["workspace/symbol"] = server.workspaceSymbolProvider,
        ["textDocument/diagnostic"] = server.diagnosticProvider,
        ["textDocument/prepareCallHierarchy"] = server.callHierarchyProvider,
        ["textDocument/signatureHelp"] = server.signatureHelpProvider,
    }

    for method, provided in pairs(providers) do
        client.providers[method] = (provided == true and {}) or (type(provided) == "table" and provided) or nil
    end

    local tokens = server.semanticTokensProvider

    if type(tokens) == "table" and type(tokens.legend) == "table" and type(tokens.legend.tokenTypes) == "table" and tokens.full ~= nil and tokens.full ~= false then
        client.providers["textDocument/semanticTokens/full"] = tokens
    end
end

-- A server that does not answer initialization in time is ended and started again, which the restart budget counts.
function Client:initialize()
    local params = json.encode({ clientInfo = { name = "Workpane" }, rootUri = workpane.files.uri(self.root), workspaceFolders = { { uri = workpane.files.uri(self.root), name = self.root:match("[^/\\]+$") } }, capabilities = capabilities() })
    self.nextId = self.nextId + 1
    local id = self.nextId
    local answered = false

    self.pending[id] = function(result, failure)
        answered = true

        -- A server that ended before it answered is started again by its exit under the restart budget, so only a server that answered with a refusal is reported as refusing.
        if failure ~= nil and self.process == nil then
            return
        end

        if failure ~= nil then
            local reason = tostring(failure.message or "")
            self.handlers.failed({ key = "code-editor.lsp.initialize-rejected", argument = reason, detail = "The language server rejected initialization: " .. reason })
            workpane.process.stop(self.process)
            return
        end

        readCapabilities(self, result.capabilities or {})
        self.ready = true
        self:notify("initialized", {})

        for uri, document in pairs(self.documents) do
            document.version = 1
            self:opened(uri, document)
        end

        self.handlers.ready()
    end

    self:sendRaw('{"jsonrpc":"2.0","id":' .. id .. ',"method":"initialize","params":{"processId":' .. math.tointeger(workpane.app.processId) .. ',' .. params:sub(2) .. "}")

    workpane.task(function()
        async.sleep(catalog.limit("initializeTimeoutMs")):await()

        if not answered and self.pending[id] ~= nil and self.process ~= nil then
            self.pending[id] = nil
            self.handlers.log("The language server did not answer initialization and is ended")
            workpane.process.stop(self.process)
        end
    end)
end

function Client:provides(method)
    return self.ready and self.providers[method] ~= nil
end

function Client:provider(method)
    return self.providers[method]
end

function Client:opened(uri, document)
    if self.ready and self.openClose then
        self:notify("textDocument/didOpen", { textDocument = { uri = uri, languageId = document.languageId, version = document.version, text = document.text } })
    end
end

-- The documents a server follows are sent whole when they open and whole again after every change, which every server accepts.
function Client:open(uri, languageId, text)
    local document = { languageId = languageId, version = 1, text = text }
    self.documents[uri] = document
    self:opened(uri, document)
end

function Client:change(uri, text)
    local document = self.documents[uri]

    if document == nil or document.text == text then
        return
    end

    document.text = text
    document.version = document.version + 1

    if self.ready and self.sync ~= 0 then
        self:notify("textDocument/didChange", { textDocument = { uri = uri, version = document.version }, contentChanges = { { text = text } } })
    end
end

function Client:saved(uri)
    local document = self.documents[uri]

    if document ~= nil and self.ready and self.save ~= nil then
        self:notify("textDocument/didSave", { textDocument = { uri = uri }, text = self.save.includeText and document.text or nil })
    end
end

function Client:close(uri)
    if self.documents[uri] == nil then
        return
    end

    self.documents[uri] = nil

    if self.ready and self.openClose then
        self:notify("textDocument/didClose", { textDocument = { uri = uri } })
    end
end

-- Ending asks the server to shut down and exit, and ends its process once the grace has passed whatever it answered.
function Client:stop()
    self.stopping = true

    if self.process == nil then
        self.handlers.stopped()
        return
    end

    if not self.ready then
        workpane.process.stop(self.process)
        return
    end

    local process = self.process

    self:request("shutdown", nil, function()
        self:notify("exit", nil)
        self.handlers.stopped()
    end)

    workpane.task(function()
        async.sleep(stopGraceMilliseconds):await()

        if self.process == process then
            workpane.process.stop(process)
        end
    end)
end

return lsp
