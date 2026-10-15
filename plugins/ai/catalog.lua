-- The providers, models and limits the plugin carries in its assets, checked whole before anything runs.
local fs = require("fs")
local codec = include("codec")

local catalog = {}

local traits = { sampling = true, reasoning = true, ["function-calling"] = true, vision = true, ["system-prompt"] = true, pdf = true, audio = true }
local protocols = { ["openai-compatible"] = true, anthropic = true, ["command-line"] = true }
local endpointNames = { chat = true, image = true, speech = true }
local parameterTypes = { integer = true, number = true, enumeration = true }
local titleFamilies = { provider = "ai.provider.", parameter = "ai.parameter.", option = "ai.option." }
local limitBounds = {
    repeatedToolCallLimit = { 1, 100 },
    summaryMaximumTokens = { 128, 100000 },
    toolDeadlineMs = { 1000, 3600000 },
    requestTimeoutMs = { 1000, 3600000 },
    discoveryTimeoutMs = { 1000, 600000 },
    serverStartTimeoutMs = { 1000, 600000 },
    scheduleWakeupMs = { 1000, 3600000 },
    maximumAgentIterations = { 1, 100000 },
    maximumCommandTimeoutSeconds = { 1, 604800 },
    maximumParallelExecutions = { 1, 1024 },
    maximumSamplingTokens = { 128, 10000000 },
    maximumRequestDelayMs = { 1000, 3600000 },
    maximumRequestsPerMinute = { 1, 1000000 },
    maximumConcurrentRequests = { 1, 1024 },
    retryBackoffMs = { 100, 60000 },
    maximumRetryBackoffMs = { 1000, 600000 },
}

local providers = {}
local byId = {}
local limits = {}

local function invalid(message, detail)
    error({ code = "ai_catalog_invalid", message = message, detail = detail or "" }, 0)
end

local function object(value)
    return type(value) == "table" and value ~= codec.null and not codec.isList(value) and getmetatable(value) == nil
end

local function list(value)
    return type(value) == "table" and (codec.isList(value) or getmetatable(value) ~= nil) and value ~= codec.null
end

local function known(document, keys, message, detail)
    for key in pairs(document) do
        if not keys[key] then
            invalid(message, detail .. "." .. tostring(key))
        end
    end
end

local function keySet(names)
    local set = {}

    for _, name in ipairs(names) do
        set[name] = true
    end

    return set
end

-- A title names a key of the catalog of the plugin in the family its kind is translated from.
local function titled(value, kind)
    return type(value) == "string" and value:sub(1, #titleFamilies[kind]) == titleFamilies[kind] and #value > #titleFamilies[kind]
end

local function text(value)
    return type(value) == "string" and value ~= ""
end

local function strings(value, detail)
    if not list(value) then
        invalid("The declared list is not a list", detail)
    end

    local seen = {}

    for _, entry in ipairs(value) do
        if not text(entry) or seen[entry] then
            invalid("The declared list carries an invalid entry", detail)
        end

        seen[entry] = true
    end

    return value
end

local function stringMap(value, detail)
    if value == nil then
        return {}
    end

    if not object(value) then
        invalid("The declared map is not a map", detail)
    end

    for key, entry in pairs(value) do
        if key == "" or type(entry) ~= "string" then
            invalid("The declared map carries an invalid entry", detail)
        end
    end

    return value
end

-- A parameter set built for a sampling model excludes the one built for a reasoning model, so no model carries both.
local function traitSet(value, detail)
    if value == nil then
        return {}
    end

    if not list(value) then
        invalid("The declared trait set is not a list", detail)
    end

    local set = {}

    for _, trait in ipairs(value) do
        if not traits[trait] then
            invalid("The declared trait is unknown", detail)
        end

        set[trait] = true
    end

    if set.sampling and set.reasoning then
        invalid("The declared traits combine sampling and reasoning", detail)
    end

    return set
end

local function parameter(document, providerId)
    known(document, keySet({ "id", "title", "type", "field", "trait", "boundByModelOutput", "modelMaximumWhenZero", "minimum", "maximum", "default", "options" }), "A declared parameter carries an unknown value", providerId)
    local detail = providerId .. "." .. tostring(document.id)

    if not text(document.id) or not titled(document.title, "parameter") or not text(document.field) or not parameterTypes[document.type] or document.field:find("%.%.") or document.field:find("^%.") or document.field:find("%.$") then
        invalid("A declared parameter is invalid", detail)
    end

    if document.trait ~= nil and not traits[document.trait] then
        invalid("A declared parameter names an unknown trait", detail)
    end

    local numeric = document.type ~= "enumeration"

    if not numeric and (document.minimum ~= nil or document.maximum ~= nil) then
        invalid("A parameter that is not numeric declares bounds", detail)
    end

    if numeric and document.options ~= nil then
        invalid("A parameter that is not an enumeration declares options", detail)
    end

    if numeric then
        local value = document.default

        if type(document.minimum) ~= "number" or type(document.maximum) ~= "number" or document.minimum > document.maximum or type(value) ~= "number" or value < document.minimum or value > document.maximum then
            invalid("The numeric parameter default is outside its bounds", detail)
        end

        if document.type == "integer" and math.tointeger(value) == nil then
            invalid("The integer parameter default is not whole", detail)
        end
    else
        if not list(document.options) or #document.options == 0 then
            invalid("The enumeration declares no option", detail)
        end

        local found = false
        local seen = {}

        for _, option in ipairs(document.options) do
            if not object(option) or not text(option.id) or not titled(option.title, "option") or seen[option.id] then
                invalid("The enumeration option is invalid", detail)
            end

            known(option, keySet({ "id", "title" }), "The enumeration option is invalid", detail)
            seen[option.id] = true
            found = found or option.id == document.default
        end

        if not found then
            invalid("The enumeration default is not one of its options", detail)
        end
    end

    return {
        id = document.id,
        title = document.title,
        type = document.type,
        field = document.field,
        trait = document.trait,
        boundByModelOutput = document.boundByModelOutput == true,
        modelMaximumWhenZero = document.modelMaximumWhenZero == true,
        minimum = document.minimum,
        maximum = document.maximum,
        default = document.default,
        options = document.options,
    }
end

-- Two entries may share one identifier only when a model reaches exactly one of them, which distinct required traits guarantee.
local function parameterSet(value, providerId)
    if not list(value) or #value == 0 then
        invalid("The provider declares no parameter", providerId)
    end

    local parameters = {}

    for _, entry in ipairs(value) do
        if not object(entry) then
            invalid("A declared parameter is not an object", providerId)
        end

        local descriptor = parameter(entry, providerId)

        for _, existing in ipairs(parameters) do
            if existing.id == descriptor.id and (existing.trait == nil or descriptor.trait == nil or existing.trait == descriptor.trait) then
                invalid("A parameter identifier is declared twice for the same model", providerId .. "." .. existing.id)
            end
        end

        parameters[#parameters + 1] = descriptor
    end

    return parameters
end

local function endpointSet(value, providerId)
    local endpoints = {}

    if value == nil then
        return endpoints
    end

    if not object(value) then
        invalid("A provider declares endpoints that are not a set", providerId)
    end

    for name, document in pairs(value) do
        if not endpointNames[name] or not object(document) then
            invalid("A provider declares an endpoint nobody answers", name)
        end

        known(document, keySet({ "path", "voices", "defaultVoice", "voiceCatalogPath", "authHeader", "authPrefix", "textField", "voiceField", "model", "body" }), "A declared endpoint carries an unknown value", name)

        if not text(document.path) or document.path:sub(1, 1) ~= "/" then
            invalid("A declared endpoint carries no path", name)
        end

        local descriptor = {
            path = document.path,
            voices = document.voices ~= nil and strings(document.voices, providerId) or {},
            defaultVoice = document.defaultVoice or "",
            voiceCatalogPath = document.voiceCatalogPath or "",
            authHeader = document.authHeader or "",
            authPrefix = document.authPrefix or "",
            textField = document.textField or "",
            voiceField = document.voiceField or "",
            model = document.model or "",
            body = document.body or {},
        }

        -- A voice belongs to the endpoint that speaks, so no other one declares any.
        if name ~= "speech" and (#descriptor.voices > 0 or descriptor.defaultVoice ~= "" or descriptor.voiceCatalogPath ~= "" or descriptor.voiceField ~= "") then
            invalid("An endpoint that does not speak declares a voice", name)
        end

        local offersDefault = false

        for _, voice in ipairs(descriptor.voices) do
            offersDefault = offersDefault or voice == descriptor.defaultVoice
        end

        if #descriptor.voices > 0 and not offersDefault then
            invalid("A speaking endpoint declares a default voice it does not offer", providerId)
        end

        if name == "speech" and (#descriptor.voices == 0) == (descriptor.voiceCatalogPath == "") then
            invalid("A speaking endpoint declares neither a voice set nor a voice catalog, or declares both", providerId)
        end

        local carriesRequest = name ~= "chat"

        if carriesRequest ~= (descriptor.authHeader ~= "" and descriptor.textField ~= "") then
            invalid("An endpoint disagrees with itself about carrying a request of its own", name)
        end

        if not carriesRequest and (next(descriptor.body) ~= nil or descriptor.model ~= "") then
            invalid("A conversation endpoint declares a request body", name)
        end

        endpoints[name] = descriptor
    end

    return endpoints
end

-- The prompt and the model are declared where they go, so neither is spliced into a string a shell would read, and a program that reads its prompt from its input receives it there instead of as an argument.
local function commandLine(document, providerId)
    local declared = document.command

    if not object(declared) then
        invalid("A command line provider declares no program to run", providerId)
    end

    known(declared, keySet({ "program", "arguments", "clearedVariables", "promptInput" }), "A command line provider declares no program to run", providerId)

    if not text(declared.program) or not list(declared.arguments) or not list(declared.clearedVariables) or (declared.promptInput ~= nil and type(declared.promptInput) ~= "boolean") then
        invalid("A command line provider declares no program to run", providerId)
    end

    local prompt, model = false, false

    for _, argument in ipairs(declared.arguments) do
        if not text(argument) then
            invalid("A command line provider declares an empty argument", providerId)
        end

        prompt = prompt or argument == "{prompt}"
        model = model or argument == "{model}"
    end

    for _, variable in ipairs(declared.clearedVariables) do
        if not text(variable) then
            invalid("A command line provider declares an empty variable to clear", providerId)
        end
    end

    if prompt == (declared.promptInput == true) then
        invalid("A command line provider passes the prompt either as an argument or on its input", providerId)
    end

    if not model then
        invalid("A command line provider never passes the model", providerId)
    end

    return { program = declared.program, arguments = declared.arguments, clearedVariables = declared.clearedVariables, promptInput = declared.promptInput == true }
end

local function provider(document)
    known(document, keySet({ "id", "title", "protocol", "baseUrl", "addressConfigurable", "apiKeyVariable", "requiresApiKey", "userDefinedTraits", "preferredModels", "requestMaxRetries", "streamIdleTimeoutMs", "headers", "queryParameters", "parameters", "command", "endpoints" }), "A declared provider carries an unknown value", tostring(document.id))
    local id = document.id
    local retries = document.requestMaxRetries == nil and 2 or document.requestMaxRetries
    local idle = document.streamIdleTimeoutMs == nil and 60000 or document.streamIdleTimeoutMs
    local conversing = document.protocol ~= nil

    if not text(id) or not titled(document.title, "provider") or (conversing and not protocols[document.protocol]) or math.type(retries) ~= "integer" or retries < 0 or retries > 10 or math.type(idle) ~= "integer" or idle < 1000 or idle > 600000 then
        invalid("A declared provider is invalid", tostring(id))
    end

    local descriptor = {
        id = id,
        title = document.title,
        protocol = document.protocol,
        baseUrl = document.baseUrl or "",
        addressConfigurable = document.addressConfigurable == true,
        apiKeyVariable = document.apiKeyVariable or "",
        requiresApiKey = document.requiresApiKey ~= false,
        requestMaxRetries = retries,
        streamIdleTimeoutMs = idle,
        models = {},
    }

    if descriptor.requiresApiKey and descriptor.apiKeyVariable == "" then
        invalid("A provider requiring a credential names no environment variable", id)
    end

    if descriptor.protocol == "command-line" then
        descriptor.commandLine = commandLine(document, id)
    elseif not descriptor.baseUrl:match("^%a[%w+.-]*://.+") or document.command ~= nil then
        invalid("A provider reached over a wire declares no address or declares a program", id)
    end

    local wired = conversing and descriptor.protocol ~= "command-line"
    descriptor.userDefinedTraits = traitSet(document.userDefinedTraits, id)

    if wired and not descriptor.userDefinedTraits["function-calling"] then
        invalid("A provider declares a user-defined model that calls no tool", id)
    end

    descriptor.preferredModels = conversing and strings(document.preferredModels or codec.list(), id) or {}
    descriptor.headers = stringMap(document.headers, id)
    descriptor.queryParameters = stringMap(document.queryParameters, id)
    descriptor.parameters = wired and parameterSet(document.parameters, id) or {}
    descriptor.endpoints = endpointSet(document.endpoints, id)

    if not wired and document.parameters ~= nil then
        invalid("A provider that holds no conversation declares a parameter", id)
    end

    if not conversing and (document.preferredModels ~= nil or document.userDefinedTraits ~= nil) then
        invalid("A provider that holds no conversation declares a model or a trait", id)
    end

    if conversing ~= (descriptor.protocol == "command-line" or descriptor.endpoints.chat ~= nil) then
        invalid("A provider disagrees with itself about holding a conversation", id)
    end

    if not conversing and next(descriptor.endpoints) == nil then
        invalid("A provider answers no modality at all", id)
    end

    if descriptor.protocol == "command-line" and (next(descriptor.endpoints) ~= nil or #descriptor.parameters > 0 or descriptor.requiresApiKey or descriptor.addressConfigurable or descriptor.baseUrl ~= "") then
        invalid("A command line provider declares an address, a credential, a parameter or an endpoint", id)
    end

    return descriptor
end

-- A model reached over a wire that calls no tool cannot run a task, while a command line agent runs its own.
local function models(entries, providerId, wired)
    local imported = {}

    for _, entry in ipairs(entries) do
        if not object(entry) then
            invalid("A catalog model is invalid", providerId)
        end

        known(entry, keySet({ "id", "name", "context", "output", "traits", "inputCost", "outputCost" }), "A catalog model carries an unknown value", providerId)

        if not text(entry.id) or math.type(entry.context) ~= "integer" or entry.context <= 0 or math.type(entry.output) ~= "integer" or entry.output <= 0 or (entry.name ~= nil and not text(entry.name)) then
            invalid("A catalog model is invalid", tostring(entry.id or providerId))
        end

        for _, cost in ipairs({ "inputCost", "outputCost" }) do
            if entry[cost] ~= nil and (type(entry[cost]) ~= "number" or entry[cost] < 0) then
                invalid("A catalog model carries an invalid price", entry.id)
            end
        end

        local model = { id = entry.id, name = entry.name or entry.id, context = entry.context, output = entry.output, traits = traitSet(entry.traits, entry.id), inputCost = entry.inputCost, outputCost = entry.outputCost }

        if wired and not model.traits["function-calling"] then
            invalid("A catalog model declares no tool calling", entry.id)
        end

        imported[#imported + 1] = model
    end

    return imported
end

local function read(name)
    local bytes, failure = fs.readFile(workpane.plugin.directory .. "/assets/" .. name):await()

    if failure ~= nil then
        invalid("The AI catalog file is unavailable", name)
    end

    local document, refused = codec.read(bytes)

    if document == nil or not object(document) then
        invalid("The AI catalog file is not a catalog", refused ~= nil and refused.detail or name)
    end

    return document
end

-- The model file owns what every model is, while a provider declares only which of them it opens with.
function catalog.load()
    local document = read("providers.json")
    known(document, keySet({ "limits", "providers" }), "The AI provider catalog is not a catalog", "providers.json")

    if not list(document.providers) or not object(document.limits) then
        invalid("The AI provider catalog is not a catalog", "providers.json")
    end

    for name, bounds in pairs(limitBounds) do
        local value = document.limits[name]

        if math.type(value) ~= "integer" or value < bounds[1] or value > bounds[2] then
            invalid("The AI catalog limits are invalid", name)
        end

        limits[name] = value
    end

    known(document.limits, limitBounds, "The AI catalog limits are incomplete", "limits")

    for _, entry in ipairs(document.providers) do
        if not object(entry) then
            invalid("A declared provider is not an object", "providers")
        end

        local descriptor = provider(entry)

        if byId[descriptor.id] ~= nil then
            invalid("A provider identifier is declared twice", descriptor.id)
        end

        byId[descriptor.id] = descriptor
        providers[#providers + 1] = descriptor
    end

    if #providers == 0 then
        invalid("The AI provider catalog declares no provider", "providers")
    end

    local modelDocument = read("models.json")
    known(modelDocument, keySet({ "providers" }), "The AI model catalog is not a catalog", "models.json")

    if not object(modelDocument.providers) then
        invalid("The AI model catalog is not a catalog", "models.json")
    end

    for providerId, entries in pairs(modelDocument.providers) do
        local target = byId[providerId]

        if target == nil then
            invalid("The AI model catalog names an unknown provider", providerId)
        end

        if not list(entries) then
            invalid("The AI model catalog entry is not a list", providerId)
        end

        local imported = models(entries, providerId, target.protocol ~= "command-line")
        local indexed = {}

        for _, model in ipairs(imported) do
            indexed[model.id] = model
        end

        for _, preferred in ipairs(target.preferredModels) do
            if indexed[preferred] == nil then
                invalid("A provider prefers a model the catalog does not declare", preferred)
            end

            target.models[#target.models + 1] = indexed[preferred]
        end

        for _, model in ipairs(imported) do
            local isPreferred = false

            for _, preferred in ipairs(target.preferredModels) do
                isPreferred = isPreferred or preferred == model.id
            end

            if not isPreferred then
                target.models[#target.models + 1] = model
            end
        end
    end
end

function catalog.provider(id)
    return byId[id]
end

function catalog.limit(name)
    return limits[name]
end

function catalog.model(provider, modelId)
    for _, model in ipairs(provider.models) do
        if model.id == modelId then
            return model
        end
    end

    return nil
end

-- A model outside the catalog keeps the traits its provider declares for the models the reader names.
function catalog.traits(provider, modelId)
    local model = catalog.model(provider, modelId)
    return model ~= nil and model.traits or provider.userDefinedTraits
end

-- The answer budget a model accepts is its own, so its declared maximum bounds the parameter instead of a shared ceiling.
function catalog.parameters(provider, modelId)
    local modelTraits = catalog.traits(provider, modelId)
    local model = catalog.model(provider, modelId)
    local applicable = {}

    for _, parameter in ipairs(provider.parameters) do
        if parameter.trait == nil or modelTraits[parameter.trait] then
            local descriptor = setmetatable({}, { __index = parameter })

            if parameter.boundByModelOutput and model ~= nil then
                descriptor.maximum = model.output
                descriptor.default = parameter.default == 0 and 0 or math.min(parameter.default, model.output)
            end

            applicable[#applicable + 1] = descriptor
        end
    end

    return applicable
end

function catalog.defaults(provider, modelId)
    local defaults = {}

    for _, parameter in ipairs(catalog.parameters(provider, modelId)) do
        defaults[parameter.id] = parameter.default
    end

    return defaults
end

function catalog.outputParameter(provider, modelId)
    for _, parameter in ipairs(catalog.parameters(provider, modelId)) do
        if parameter.boundByModelOutput then
            return parameter
        end
    end

    return nil
end

-- A price nobody published is absent rather than free, so a model without one reports no cost.
function catalog.cost(providerId, modelId, inputTokens, outputTokens)
    local descriptor = byId[providerId]
    local model = descriptor ~= nil and catalog.model(descriptor, modelId) or nil

    if model == nil or model.inputCost == nil or model.outputCost == nil then
        return nil
    end

    return inputTokens * model.inputCost + outputTokens * model.outputCost
end

-- A command line agent answers a conversation by being invoked, so it answers the chat modality without an endpoint.
function catalog.answering(modality)
    local answering = {}

    for _, descriptor in ipairs(providers) do
        if descriptor.endpoints[modality] ~= nil or (modality == "chat" and descriptor.protocol == "command-line") then
            answering[#answering + 1] = descriptor
        end
    end

    return answering
end

function catalog.sortedTraits(set)
    local names = {}

    for name in pairs(set) do
        names[#names + 1] = name
    end

    table.sort(names)

    return names
end

return catalog
