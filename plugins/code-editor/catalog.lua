-- The languages, language servers and color schemes the editor knows, read from the assets of the plugin and checked whole before anything opens.
local fs = require("fs")
local json = require("json")

local catalog = {}

local roles = { text = true, keyword = true, declaration = true, number = true, string = true, punctuation = true, preprocessor = true, identifier = true, knownIdentifier = true, comment = true }
local schemeColors = { "text", "keyword", "declaration", "number", "string", "punctuation", "preprocessor", "identifier", "knownIdentifier", "comment", "background", "cursor", "selection", "lineNumber", "currentLineNumber", "guide", "activeGuide", "currentLine", "occurrence" }
local limitNames = { "maximumFileBytes", "maximumSemanticTokenLines", "maximumSearchMatches", "changeDebounceMs", "analysisDebounceMs", "maximumRestarts", "restartWindowMs", "initializeTimeoutMs", "maximumReferences", "maximumWorkspaceFiles", "maximumProblems", "maximumCompletions" }

-- Each kind of symbol sits at the number the protocol gives it, where types take the warning tone, callables the information tone, values the success tone and the places that hold them stay muted.
local symbolIcons = {
    { "file", "text-muted" },
    { "module", "text-muted" },
    { "namespace", "text-muted" },
    { "module", "text-muted" },
    { "class", "warning" },
    { "function", "information" },
    { "tool", "success" },
    { "field", "success" },
    { "function", "information" },
    { "enumeration", "warning" },
    { "interface", "warning" },
    { "function", "information" },
    { "variable", "success" },
    { "constant", "success" },
    { "string", "success" },
    { "number", "success" },
    { "boolean", "success" },
    { "array", "success" },
    { "object", "success" },
    { "key", "success" },
    { "null", "success" },
    { "enumerator", "success" },
    { "structure", "warning" },
    { "event", "information" },
    { "operator", "information" },
    { "type-parameter", "warning" },
}

local byId = {}
local byExtension = {}
local byFileName = {}
local servers = {}
local semantic = {}
local schemes = {}
local limits = {}

local function invalid(code, detail)
    error({ code = code, message = "The catalog of the code editor is invalid", detail = detail }, 0)
end

local function isList(value)
    if type(value) ~= "table" then
        return false
    end

    local count = 0

    for _ in pairs(value) do
        count = count + 1
    end

    return count == #value
end

local function strings(value)
    if not isList(value) then
        return false
    end

    for _, item in ipairs(value) do
        if type(item) ~= "string" or item == "" then
            return false
        end
    end

    return true
end

local function read(name)
    local text, failure = fs.readFile(workpane.plugin.directory .. "/assets/" .. name):await()

    if failure ~= nil then
        invalid("code_editor_catalog_invalid", name)
    end

    local decoded, value = pcall(json.decode, text)

    if not decoded or type(value) ~= "table" then
        invalid("code_editor_catalog_invalid", name)
    end

    return value
end

-- The identifiers a language gives some of its extensions for the language servers name extensions of that language with a text each.
local function protocolIds(entry)
    if entry.protocolIds == nil then
        return true
    end

    if type(entry.protocolIds) ~= "table" or isList(entry.protocolIds) then
        return false
    end

    for extension, id in pairs(entry.protocolIds) do
        local named = false

        for _, owned in ipairs(entry.extensions) do
            named = named or owned == extension
        end

        if not named or type(id) ~= "string" or id == "" then
            return false
        end
    end

    return true
end

-- Every extension belongs to one language, and Plain Text closes the list as the language of everything else.
local function readLanguages(entries)
    if not isList(entries) or #entries == 0 or entries[#entries].id ~= "plaintext" then
        invalid("code_editor_catalog_invalid", "languages")
    end

    for _, entry in ipairs(entries) do
        if type(entry.id) ~= "string" or entry.id == "" or byId[entry.id] ~= nil or type(entry.name) ~= "string" or entry.name == "" or not strings(entry.extensions) or not strings(entry.fileNames) or not strings(entry.keywords) or not strings(entry.types) or type(entry.strings) ~= "boolean" or (entry.constructs ~= nil and not strings(entry.constructs)) or not protocolIds(entry) then
            invalid("code_editor_catalog_invalid", tostring(entry.id))
        end

        for _, extension in ipairs(entry.extensions) do
            if byExtension[extension:lower()] ~= nil then
                invalid("code_editor_catalog_invalid", extension)
            end

            byExtension[extension:lower()] = entry
        end

        for _, fileName in ipairs(entry.fileNames) do
            byFileName[fileName:lower()] = entry
        end

        byId[entry.id] = entry
    end
end

local function readServers(entries)
    if not isList(entries) then
        invalid("code_editor_catalog_invalid", "servers")
    end

    for _, entry in ipairs(entries) do
        if type(entry.language) ~= "string" or byId[entry.language] == nil or not isList(entry.candidates) or #entry.candidates == 0 then
            invalid("code_editor_catalog_invalid", tostring(entry.language))
        end

        for _, candidate in ipairs(entry.candidates) do
            if type(candidate.executable) ~= "string" or candidate.executable == "" or (candidate.arguments ~= nil and not strings(candidate.arguments)) then
                invalid("code_editor_catalog_invalid", entry.language)
            end
        end

        servers[#servers + 1] = entry
    end
end

local function readSchemes(entries)
    if not isList(entries) or #entries == 0 then
        invalid("code_editor_schemes_invalid", "schemes")
    end

    for _, entry in ipairs(entries) do
        if type(entry.id) ~= "string" or entry.id == "" or type(entry.name) ~= "string" or type(entry.colors) ~= "table" then
            invalid("code_editor_schemes_invalid", tostring(entry.id))
        end

        for _, color in ipairs(schemeColors) do
            if type(entry.colors[color]) ~= "string" or not entry.colors[color]:match("^#%x%x%x%x%x%x$") then
                invalid("code_editor_schemes_invalid", entry.id .. "." .. color)
            end
        end

        schemes[#schemes + 1] = entry
    end
end

function catalog.load()
    local languageCatalog = read("languages.json")
    readLanguages(languageCatalog.languages)
    readServers(languageCatalog.servers)

    if type(languageCatalog.semantic) ~= "table" or type(languageCatalog.limits) ~= "table" then
        invalid("code_editor_catalog_invalid", "languages.json")
    end

    for tokenType, role in pairs(languageCatalog.semantic) do
        if not roles[role] then
            invalid("code_editor_catalog_invalid", tokenType)
        end

        semantic[tokenType] = role
    end

    for _, name in ipairs(limitNames) do
        if math.type(languageCatalog.limits[name]) ~= "integer" or languageCatalog.limits[name] <= 0 then
            invalid("code_editor_catalog_invalid", name)
        end

        limits[name] = languageCatalog.limits[name]
    end

    readSchemes(read("schemes.json").schemes)
end

function catalog.limit(name)
    return limits[name]
end

function catalog.language(id)
    return byId[id]
end

-- A file name decides before the extension, and anything else is Plain Text.
function catalog.detect(path)
    local name = path:match("[^/\\]+$") or path
    local extension = name:match("%.([^.]+)$")

    return byFileName[name:lower()] or (extension ~= nil and byExtension[extension:lower()]) or byId.plaintext
end

-- The identifier a language server knows a file by, which some extensions name apart from their language, such as C among C++ or TSX among TypeScript.
function catalog.protocolId(language, path)
    local extension = (path:match("[^/\\]+$") or path):match("%.([^.]+)$")
    local named = extension ~= nil and language.protocolIds ~= nil and language.protocolIds[extension:lower()] or nil

    return named or language.id
end

-- The definition the editor colors with, where Plain Text colors nothing.
function catalog.definition(language)
    if language.id == "plaintext" then
        return "none"
    end

    return {
        name = language.name,
        lineComment = language.lineComment,
        blockComment = language.blockComment,
        singleQuotes = language.strings,
        doubleQuotes = language.strings,
        escape = language.strings and "\\" or nil,
        preprocessor = language.preprocessor,
        keywords = language.keywords,
        declarations = language.types,
        constructs = language.constructs,
    }
end

function catalog.servers()
    return servers
end

function catalog.semanticRole(tokenType)
    return semantic[tokenType]
end

-- Answers the icon of what a symbol of a kind the language server protocol numbers is and the tone of its family, or nil for a kind the protocol does not number.
function catalog.symbolIcon(kind)
    local icon = symbolIcons[kind]

    if icon == nil then
        return nil, nil
    end

    return icon[1], icon[2]
end

function catalog.schemes()
    return schemes
end

function catalog.scheme(id)
    for _, scheme in ipairs(schemes) do
        if scheme.id == id then
            return scheme
        end
    end

    return nil
end

return catalog
