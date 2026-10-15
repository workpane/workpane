-- A connection is one configured provider and model pair, named by the key agents store, and a secret is either literal or a reference to an environment variable.
local process = require("process")
local catalog = include("catalog")
local codec = include("codec")

local connections = {}

local maximumFieldDepth = 16

function connections.key(connection)
    return connection.providerId .. "/" .. connection.modelId
end

-- The display name is what the reader sees, and the key stands in when they named nothing.
function connections.label(connection)
    local trimmed = (connection.displayName or ""):match("^%s*(.-)%s*$")
    return trimmed ~= "" and trimmed or connections.key(connection)
end

local function isReference(secret)
    return type(secret) == "string" and secret:match("^{env%.[A-Za-z_][A-Za-z0-9_]*}$") ~= nil
end

function connections.reference(variable)
    return variable ~= "" and ("{env." .. variable .. "}") or ""
end

-- A key is there when it is written or when the variable it refers to is set, which is read at the moment it is asked.
function connections.available(secret)
    if not isReference(secret) then
        return secret ~= ""
    end

    local value = process.getenv(secret:match("^{env%.(.-)}$"))
    return value ~= nil and value ~= ""
end

-- A reference is read when the request leaves, so a key exported after the product started is still found.
function connections.secret(secret)
    if not isReference(secret) then
        return secret
    end

    local name = secret:match("^{env%.(.-)}$")
    local value = process.getenv(name)

    if value == nil or value == "" then
        error({ code = "ai_secret_environment_missing", message = "The referenced environment variable is not set", detail = name }, 0)
    end

    return value
end

function connections.find(list, key)
    for _, connection in ipairs(list) do
        if connections.key(connection) == key then
            return connection
        end
    end

    return nil
end

function connections.protocol(connection)
    local provider = catalog.provider(connection.providerId)
    return provider ~= nil and provider.protocol or "openai-compatible"
end

-- Only a self-hosted service carries its own address, so every other connection speaks to the address its provider publishes.
function connections.address(connection)
    local provider = catalog.provider(connection.providerId)

    if provider == nil then
        return ""
    end

    return provider.addressConfigurable and (connection.address or "") ~= "" and connection.address or provider.baseUrl
end

-- Every modality is reached through its endpoint, so a provider that answers one is data rather than a path written into a tool.
function connections.endpoint(providerId, address, modality)
    local provider = catalog.provider(providerId)

    if provider == nil or provider.endpoints[modality] == nil then
        return nil
    end

    local base = (address ~= nil and address ~= "") and address or provider.baseUrl

    if base == "" then
        return nil
    end

    return base .. provider.endpoints[modality].path
end

function connections.declared(provider, modelId)
    local model = modelId

    if (model == nil or model == "") and provider.models[1] ~= nil then
        model = provider.models[1].id
    end

    return { providerId = provider.id, modelId = model or "", displayName = "", apiKey = connections.reference(provider.apiKeyVariable), address = provider.addressConfigurable and provider.baseUrl or "", parameters = catalog.defaults(provider, model or ""), extraParameters = "" }
end

-- A zero budget lets the service answer with everything the model allows, so what it is really worth is the maximum of that model.
function connections.outputBudget(connection)
    local provider = catalog.provider(connection.providerId)

    if provider == nil then
        return 0
    end

    local parameter = catalog.outputParameter(provider, connection.modelId)
    local declared = parameter ~= nil and connection.parameters[parameter.id] or 0

    if math.type(declared) == "integer" and declared > 0 then
        return declared
    end

    local model = catalog.model(provider, connection.modelId)
    return model ~= nil and model.output or 0
end

-- A borrowed budget still fits what the model accepts, so it is clamped instead of making the connection invalid.
function connections.withBudget(connection, tokens)
    local copy = {}

    for key, value in pairs(connection) do
        copy[key] = value
    end

    copy.parameters = {}

    for key, value in pairs(connection.parameters) do
        copy.parameters[key] = value
    end

    local provider = catalog.provider(connection.providerId)
    local parameter = provider ~= nil and catalog.outputParameter(provider, connection.modelId) or nil

    if parameter ~= nil then
        copy.parameters[parameter.id] = math.max(math.tointeger(parameter.minimum) or 0, math.min(tokens, math.tointeger(parameter.maximum) or tokens))
    end

    return copy
end

local function refuse(code, message, detail)
    error({ code = code, message = message, detail = detail or "" }, 0)
end

local function validParameter(parameter, value)
    if parameter.type == "enumeration" then
        for _, option in ipairs(parameter.options) do
            if option.id == value then
                return true
            end
        end

        return false
    end

    if type(value) ~= "number" or value < parameter.minimum or value > parameter.maximum then
        return false
    end

    return parameter.type ~= "integer" or math.tointeger(value) ~= nil
end

-- What the reader adds beyond the parameters the catalog knows is a JSON object whose dotted keys reach nested fields and whose null removes one.
function connections.extras(text)
    if text == nil or text:match("^%s*$") then
        return {}
    end

    local document = codec.read(text, true)

    if type(document) ~= "table" or codec.isList(document) or document == codec.null or getmetatable(document) ~= nil then
        refuse("ai_extra_parameter_invalid", "The extra provider parameters are not a JSON object", "")
    end

    for key in pairs(document) do
        if key:match("^%s*$") or #key - #key:gsub("%.", "") >= maximumFieldDepth then
            refuse("ai_extra_parameter_invalid", "The extra provider parameter is invalid", key)
        end
    end

    return document
end

-- A connection is refused where it is typed, with the first rule it breaks, and answered trimmed as it is stored.
function connections.validate(connection)
    local provider = catalog.provider(connection.providerId)

    if provider == nil then
        refuse("ai_provider_unknown", "The selected AI provider is not supported", connection.providerId)
    end

    local validated = {
        providerId = connection.providerId,
        modelId = (connection.modelId or ""):match("^%s*(.-)%s*$"),
        displayName = (connection.displayName or ""):match("^%s*(.-)%s*$"),
        apiKey = connection.apiKey or "",
        address = (connection.address or ""):match("^%s*(.-)%s*$"),
        parameters = connection.parameters or {},
        extraParameters = connection.extraParameters or "",
    }

    if validated.modelId == "" then
        refuse("ai_model_invalid", "The AI model is required", provider.id)
    end

    if provider.requiresApiKey and validated.apiKey == "" then
        refuse("ai_api_key_missing", "The provider requires an API key", provider.id)
    end

    if not provider.addressConfigurable and validated.address ~= "" then
        refuse("ai_address_not_configurable", "The provider publishes its own address", provider.id)
    end

    if provider.addressConfigurable and not validated.address:match("^https?://[^/%s]+") then
        refuse("ai_address_invalid", "The service address is invalid", provider.id)
    end

    local applicable = catalog.parameters(provider, validated.modelId)
    local declared = {}

    for _, parameter in ipairs(applicable) do
        local value = validated.parameters[parameter.id]
        declared[parameter.id] = true

        if value == nil then
            refuse("ai_parameter_missing", "The provider parameter is missing", parameter.id)
        end

        if not validParameter(parameter, value) then
            refuse("ai_parameter_invalid", "The provider parameter value is invalid", parameter.id)
        end
    end

    for key in pairs(validated.parameters) do
        if not declared[key] then
            refuse("ai_parameter_unknown", "The provider parameter is not declared for this model", key)
        end
    end

    -- Asking for the maximum of a model the catalog does not declare has no answer, and a model whose maximum is its whole window leaves the conversation no room.
    local budget = catalog.outputParameter(provider, validated.modelId)
    local model = catalog.model(provider, validated.modelId)
    local asksForMaximum = budget ~= nil and budget.modelMaximumWhenZero and validated.parameters[budget.id] == 0

    if asksForMaximum and model == nil then
        refuse("ai_output_budget_unknown", "The catalog does not declare this model, so its answer budget has to be a number", validated.modelId)
    end

    if asksForMaximum and model.output >= model.context then
        refuse("ai_output_budget_whole_window", "The maximum this model declares is its whole window, so its answer budget has to be a number", validated.modelId)
    end

    connections.extras(validated.extraParameters)

    return validated
end

-- One key names one configuration, so the same provider and model pair is configured once.
function connections.validateSet(list)
    local keys = {}
    local validated = {}

    for index, connection in ipairs(list) do
        local checked = connections.validate(connection)
        local key = connections.key(checked)

        if keys[key] then
            refuse("ai_connection_duplicate", "The provider and model pair is already configured", key)
        end

        keys[key] = true
        validated[index] = checked
    end

    return validated
end

-- A field of the request is written at its dotted path, and a null removes it without leaving an empty parent behind.
function connections.applyField(body, field, value)
    local segments = {}

    for segment in field:gmatch("[^%.]+") do
        segments[#segments + 1] = segment
    end

    if #segments == 0 or #segments > maximumFieldDepth then
        return
    end

    local function remove(target, index)
        if index == #segments then
            target[segments[index]] = nil
            return
        end

        local nested = target[segments[index]]

        if type(nested) ~= "table" or codec.isList(nested) then
            return
        end

        remove(nested, index + 1)

        if next(nested) == nil then
            target[segments[index]] = nil
        end
    end

    if value == codec.null then
        remove(body, 1)
        return
    end

    local target = body

    for index = 1, #segments - 1 do
        local nested = target[segments[index]]

        if type(nested) ~= "table" or codec.isList(nested) or nested == codec.null then
            nested = {}
            target[segments[index]] = nested
        end

        target = nested
    end

    target[segments[#segments]] = value
end

return connections
