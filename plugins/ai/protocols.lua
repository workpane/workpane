-- The shapes tools and conversations take on the wire of each protocol, the tool schemas arguments are checked against, the calls a streamed answer carries and the reason it ended.
local crypto = require("crypto")
local codec = include("codec")

local protocols = {}

local maximumReportedArguments = 2000

local jsonTypes = {
    string = function(value)
        return type(value) == "string"
    end,
    integer = function(value)
        return type(value) == "number"
    end,
    number = function(value)
        return type(value) == "number"
    end,
    boolean = function(value)
        return type(value) == "boolean"
    end,
    array = function(value)
        return codec.isList(value) or (type(value) == "table" and next(value) == nil)
    end,
    object = function(value)
        return type(value) == "table" and not codec.isList(value) and value ~= codec.null
    end,
}

-- A tool of the plugin names its description by a key of the catalog, and a tool a server publishes brings its description written.
function protocols.validSchema(schema)
    local described = (type(schema.descriptionKey) == "string" and schema.descriptionKey ~= "") or (type(schema.description) == "string" and schema.description ~= "")

    return type(schema.name) == "string" and schema.name:match("^[a-z][a-z0-9_]*$") ~= nil and #schema.name <= 64 and described and type(schema.parameters) == "table" and schema.parameters.type == "object" and type(schema.parameters.properties) == "table"
end

-- An argument is judged only by what the schema declares, so a server tool is checked exactly like a native one.
function protocols.argumentError(schema, arguments)
    local properties = schema.parameters.properties

    for _, name in ipairs(schema.parameters.required or {}) do
        if arguments[name] == nil then
            local declared = type(properties[name]) == "table" and properties[name].type or ""
            return { argument = name, expected = type(declared) == "string" and declared or "" }
        end
    end

    for name, value in pairs(arguments) do
        local declared = type(properties[name]) == "table" and properties[name].type or nil
        local check = type(declared) == "string" and jsonTypes[declared] or nil

        if check ~= nil and not check(value) then
            return { argument = name, expected = declared }
        end
    end

    return nil
end

-- A tool of the plugin names its description by a key of the catalog, and a tool another plugin declares brings its description written.
function protocols.serializeTools(protocol, tools, translate)
    local serialized = codec.list()

    for index, tool in ipairs(tools) do
        local description = tool.description or translate(tool.descriptionKey)

        if protocol == "anthropic" then
            serialized[index] = { name = tool.name, description = description, input_schema = tool.parameters }
        else
            serialized[index] = { type = "function", ["function"] = { name = tool.name, description = description, parameters = tool.parameters } }
        end
    end

    return serialized
end

local function dataAddress(entry)
    return "data:" .. entry.mediaType .. ";base64," .. crypto.base64Encode(entry.data)
end

local function joinedText(message)
    local texts = {}

    for _, entry in ipairs(message.content) do
        if entry.type == "text" and entry.text ~= "" then
            texts[#texts + 1] = entry.text
        end
    end

    return table.concat(texts, "\n\n")
end

local function unsupported(entry)
    error({ code = "ai_message_part_unsupported", message = "The protocol carries no part of this type", detail = entry.type }, 0)
end

-- A part reaches the OpenAI protocol in its own shape, and a reasoning is never sent back because that protocol reads none.
local function openaiPart(entry)
    if entry.type == "text" then
        return { type = "text", text = entry.text }
    end

    if entry.type == "image" then
        return { type = "image_url", image_url = { url = entry.url or dataAddress(entry), detail = entry.detail } }
    end

    if entry.type == "audio" then
        return { type = "input_audio", input_audio = { data = crypto.base64Encode(entry.data), format = entry.format } }
    end

    if entry.type == "document" and entry.mediaType == "application/pdf" then
        return { type = "file", file = { filename = entry.name, file_data = dataAddress(entry) } }
    end

    if entry.type == "reasoning" then
        return nil
    end

    unsupported(entry)
end

-- A content of text alone travels as one text, which every server of the protocol reads, and any other content as its list of parts.
local function openaiContent(message)
    local parts = codec.list()
    local plain = true

    for _, entry in ipairs(message.content) do
        local encoded = openaiPart(entry)

        if encoded ~= nil then
            parts[#parts + 1] = encoded
            plain = plain and entry.type == "text"
        end
    end

    return plain and joinedText(message) or parts
end

-- A tool message of the OpenAI protocol carries text alone, so the images tools returned follow their results in one user turn that says what they are.
local function openaiMessages(list, translate)
    local wire = codec.list()
    local images = codec.list()

    local function release()
        if #images == 0 then
            return
        end

        table.insert(images, 1, { type = "text", text = translate("ai.message.tool-images") })
        wire[#wire + 1] = { role = "user", content = images }
        images = codec.list()
    end

    for _, message in ipairs(list) do
        if message.role ~= "tool" then
            release()
        end

        if message.role == "tool" then
            wire[#wire + 1] = { role = "tool", tool_call_id = message.toolCallId, content = joinedText(message) }

            for _, entry in ipairs(message.content) do
                if entry.type == "image" then
                    images[#images + 1] = openaiPart(entry)
                end
            end
        elseif message.role == "assistant" then
            local encoded = { role = "assistant", content = joinedText(message) }

            if #message.toolCalls > 0 then
                encoded.tool_calls = codec.list()

                for index, call in ipairs(message.toolCalls) do
                    encoded.tool_calls[index] = { id = call.id, type = "function", ["function"] = { name = call.name, arguments = codec.encode(call.arguments) } }
                end
            end

            wire[#wire + 1] = encoded
        else
            wire[#wire + 1] = { role = message.role, content = message.role == "system" and joinedText(message) or openaiContent(message) }
        end
    end

    release()

    return { messages = wire }
end

-- A part reaches the Anthropic protocol as a block, and a reasoning goes back only with the signature or the redacted data that proves it came from the model.
local function anthropicBlock(entry)
    if entry.type == "text" then
        return entry.text ~= "" and { type = "text", text = entry.text } or nil
    end

    if entry.type == "image" and entry.url ~= nil then
        return { type = "image", source = { type = "url", url = entry.url } }
    end

    if entry.type == "image" then
        return { type = "image", source = { type = "base64", media_type = entry.mediaType, data = crypto.base64Encode(entry.data) } }
    end

    if entry.type == "document" and entry.mediaType == "application/pdf" then
        return { type = "document", source = { type = "base64", media_type = entry.mediaType, data = crypto.base64Encode(entry.data) }, title = entry.name }
    end

    if entry.type == "reasoning" and entry.redacted ~= nil then
        return { type = "redacted_thinking", data = entry.redacted }
    end

    if entry.type == "reasoning" then
        return entry.signature ~= nil and { type = "thinking", thinking = entry.text, signature = entry.signature } or nil
    end

    unsupported(entry)
end

local function anthropicBlocks(message)
    local blocks = codec.list()

    for _, entry in ipairs(message.content) do
        blocks[#blocks + 1] = anthropicBlock(entry)
    end

    return blocks
end

-- The instructions at the head of the conversation become the system prompt, and one written later is read as a user turn.
-- Turns of one role beside each other are joined, a result is a block of the user turn that follows its call, and a conversation that opens with an answer is opened for it.
local function anthropicMessages(list, translate)
    local wire = codec.list()
    local instructions = {}

    local function append(role, blocks)
        local last = wire[#wire]

        if #blocks == 0 then
            return
        end

        if last ~= nil and last.role == role then
            for _, block in ipairs(blocks) do
                last.content[#last.content + 1] = block
            end

            return
        end

        wire[#wire + 1] = { role = role, content = blocks }
    end

    for _, message in ipairs(list) do
        if message.role == "system" and #wire == 0 then
            instructions[#instructions + 1] = joinedText(message)
        elseif message.role == "assistant" then
            local blocks = anthropicBlocks(message)

            for _, call in ipairs(message.toolCalls) do
                blocks[#blocks + 1] = { type = "tool_use", id = call.id, name = call.name, input = call.arguments }
            end

            if #wire == 0 then
                append("user", codec.list({ { type = "text", text = translate("ai.message.conversation-start") } }))
            end

            append("assistant", blocks)
        elseif message.role == "tool" then
            local result = { type = "tool_result", tool_use_id = message.toolCallId, content = anthropicBlocks(message) }

            if message.failed then
                result.is_error = true
            end

            append("user", codec.list({ result }))
        else
            append("user", anthropicBlocks(message))
        end
    end

    local system = table.concat(instructions, "\n\n")

    return { system = system ~= "" and system or nil, messages = wire }
end

-- Answers the fields a request of the protocol carries for a canonical conversation already fitted to its model.
function protocols.encode(protocol, list, translate)
    if protocol == "anthropic" then
        return anthropicMessages(list, translate)
    end

    return openaiMessages(list, translate)
end

-- Every protocol names the reason an answer ended its own way, and each one reads as stop, length, tool calls or content filter.
local finishReasons = {
    anthropic = { end_turn = "stop", stop_sequence = "stop", pause_turn = "stop", max_tokens = "length", tool_use = "tool_calls", refusal = "content_filter" },
    openai = { stop = "stop", length = "length", tool_calls = "tool_calls", function_call = "tool_calls", content_filter = "content_filter" },
}

function protocols.finishReason(protocol, reason)
    local known = finishReasons[protocol == "anthropic" and "anthropic" or "openai"]
    return known[reason] or (reason == "" and "" or "stop")
end

-- Both protocols stream the arguments of a call as fragments, which are joined and read once the answer ends.
local Accumulator = {}
Accumulator.__index = Accumulator

function protocols.accumulator(protocol)
    return setmetatable({ protocol = protocol, pending = {}, order = {} }, Accumulator)
end

function Accumulator:entry(index)
    if self.pending[index] == nil then
        self.pending[index] = { id = "", name = "", arguments = {} }
        self.order[#self.order + 1] = index
        table.sort(self.order)
    end

    return self.pending[index]
end

function Accumulator:consume(event)
    if self.protocol == "anthropic" then
        local index = math.tointeger(event.index)

        if index == nil then
            return
        end

        if event.type == "content_block_start" and type(event.content_block) == "table" and event.content_block.type == "tool_use" then
            local target = self:entry(index)
            target.id = event.content_block.id or ""
            target.name = event.content_block.name or ""
        elseif event.type == "content_block_delta" and self.pending[index] ~= nil and type(event.delta) == "table" and event.delta.type == "input_json_delta" then
            local target = self.pending[index].arguments
            target[#target + 1] = event.delta.partial_json or ""
        end

        return
    end

    local choice = type(event.choices) == "table" and event.choices[1] or nil
    local delta = type(choice) == "table" and choice.delta or nil

    for _, call in ipairs(type(delta) == "table" and type(delta.tool_calls) == "table" and delta.tool_calls or {}) do
        local index = math.tointeger(call.index)

        if index ~= nil then
            local target = self:entry(index)
            local fn = type(call["function"]) == "table" and call["function"] or {}
            target.id = type(call.id) == "string" and call.id or target.id
            target.name = type(fn.name) == "string" and fn.name or target.name

            if type(fn.arguments) == "string" then
                target.arguments[#target.arguments + 1] = fn.arguments
            elseif type(fn.arguments) == "table" then
                target.arguments[#target.arguments + 1] = codec.encode(fn.arguments)
            end
        end
    end
end

-- A call whose arguments cannot be read keeps what arrived, so the model is told instead of the run ending.
function Accumulator:calls()
    local calls = {}

    for _, index in ipairs(self.order) do
        local pending = self.pending[index]

        if pending.id == "" or pending.name == "" then
            return nil, { code = "ai_tool_call_invalid", message = "The model returned an incomplete tool call", detail = pending.name }
        end

        local text = table.concat(pending.arguments)
        text = text == "" and "{}" or text
        local arguments = codec.read(text, true)

        if type(arguments) ~= "table" or codec.isList(arguments) or arguments == codec.null or getmetatable(arguments) ~= nil then
            calls[#calls + 1] = { id = pending.id, name = pending.name, arguments = {}, unreadable = text:sub(1, maximumReportedArguments) }
        else
            calls[#calls + 1] = { id = pending.id, name = pending.name, arguments = arguments }
        end
    end

    return calls
end

return protocols
