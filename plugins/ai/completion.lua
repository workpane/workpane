-- Answers one conversation for whoever asks, the way an agent is answered: the messages are checked whole, fitted to what the model reads and sent to the connection named or to the default one.
local catalog = include("catalog")
local chat = include("chat")
local cli = include("cli")
local codec = include("codec")
local connections = include("connections")
local messages = include("messages")
local preferences = include("preferences")

local translate = workpane.i18n.translate

local completion = {}

local fields = { messages = true, connection = true, maximumTokens = true, tools = true }
local toolFields = { name = true, description = true, parameters = true }
local silent = { requestSent = function() end, content = function() end, throttled = function() end }

local function refuse(code, message, detail)
    error({ code = code, message = message, detail = detail or "" }, 0)
end

-- A connection answers over its protocol, and a command line agent runs its own program.
function completion.client(connection, handlers)
    return connections.protocol(connection) == "command-line" and cli.new(handlers) or chat.new(handlers)
end

-- A tool the caller declares is named the way every protocol accepts, described in words and given the object schema of its arguments.
local function declaredTools(value)
    if value == nil then
        return {}
    end

    if type(value) ~= "table" then
        refuse("ai_completion_invalid", "The tools of a completion are not a list", "tools")
    end

    local declared = {}
    local seen = {}

    for index, tool in ipairs(value) do
        local path = "tools[" .. index .. "]"
        local named = type(tool) == "table" and type(tool.name) == "string" and tool.name:match("^[%a_][%w_%-]*$") ~= nil and #tool.name <= 64 and not seen[tool.name]
        local described = named and type(tool.description) == "string" and tool.description:match("%S") ~= nil
        local shaped = described and type(tool.parameters) == "table" and tool.parameters.type == "object" and type(tool.parameters.properties) == "table"

        if not shaped then
            refuse("ai_completion_invalid", "A tool carries no valid name, description or object schema, or repeats a name", path)
        end

        for key in pairs(tool) do
            if not toolFields[key] then
                refuse("ai_completion_invalid", "A tool carries a field it does not declare", path .. "." .. tostring(key))
            end
        end

        seen[tool.name] = true
        declared[index] = { name = tool.name, description = tool.description, parameters = tool.parameters }
    end

    return declared
end

-- The connection named by its key answers, or the default one when none is named, within the budget the caller gives it.
local function chosenConnection(payload)
    local named = payload.connection ~= nil
    local connection = named and type(payload.connection) == "string" and connections.find(preferences.connections(), payload.connection) or nil

    if named and connection == nil then
        refuse("ai_connection_unknown", "The connection the completion names is not configured", tostring(payload.connection))
    end

    connection = connection or preferences.defaultConnection()

    if connection == nil then
        refuse("ai_provider_unconfigured", "No AI provider is configured", "")
    end

    -- A command-line agent runs a task of its own in a folder with the permissions of the reader, so it never answers a completion another plugin or a server asks for.
    if connections.protocol(connection) == "command-line" then
        refuse("ai_completion_command_line", "A command-line connection answers tasks, not completions", connections.key(connection))
    end

    if payload.maximumTokens == nil then
        return connection
    end

    if math.type(payload.maximumTokens) ~= "integer" or payload.maximumTokens < 1 or payload.maximumTokens > catalog.limit("maximumSamplingTokens") then
        refuse("ai_completion_invalid", "The answer budget is not a whole number within the permitted range", "maximumTokens")
    end

    return connections.withBudget(connection, payload.maximumTokens)
end

-- Answers the text, the reasoning, the tool calls, the usage and the reason the answer ended, with the changes the model forced on the conversation, or raises the first rule broken.
function completion.answer(payload)
    if type(payload) ~= "table" then
        refuse("ai_completion_invalid", "A completion is asked with a table", "")
    end

    for key in pairs(payload) do
        if not fields[key] then
            refuse("ai_completion_invalid", "A completion carries a field it does not declare", tostring(key))
        end
    end

    local connection = chosenConnection(payload)
    local traits = catalog.traits(catalog.provider(connection.providerId), connection.modelId)
    local tools = declaredTools(payload.tools)

    if #tools > 0 and not traits["function-calling"] then
        refuse("ai_tools_unsupported", "The model of the connection calls no tool", connections.key(connection))
    end

    local fitted, adjustments = messages.fit(messages.normalize(payload.messages), traits, translate)
    local result = completion.client(connection, silent):send({ connection = connection, address = connections.address(connection), messages = fitted, tools = tools, workdir = workpane.system.home() }, translate)
    local calls = {}

    for index, call in ipairs(result.calls) do
        calls[index] = { id = call.id, name = call.name, arguments = codec.plain(call.arguments), unreadable = call.unreadable }
    end

    -- The reasoning a caller reads is the text of every block the model wrote, where a redacted block carries none.
    local thoughts = {}

    for _, block in ipairs(result.reasoning) do
        if block.text ~= "" then
            thoughts[#thoughts + 1] = block.text
        end
    end

    return { content = result.content, reasoning = table.concat(thoughts, "\n\n"), toolCalls = calls, finishReason = result.finishReason, usage = { input = result.usage.input, output = result.usage.output }, connection = connections.key(connection), model = connection.modelId, adjustments = adjustments }
end

return completion
