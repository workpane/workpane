-- The conversation every protocol is built from: messages whose content is a list of typed parts, checked whole and fitted to what each model reads.
local codec = include("codec")

local messages = {}

local charactersPerToken = 4
local imageTokens = 1600
local pageTokens = 3000
local audioBytesPerToken = 1024
local messageTokens = 4
local callTokens = 8

local imageTypes = { ["image/png"] = true, ["image/jpeg"] = true, ["image/gif"] = true, ["image/webp"] = true }
local textTypes = { ["text/plain"] = true, ["text/markdown"] = true, ["text/csv"] = true, ["text/html"] = true, ["text/xml"] = true, ["application/json"] = true, ["application/xml"] = true }
local audioFormats = { wav = true, mp3 = true }
local details = { low = true, high = true, auto = true }
local largest = { image = 20 * 1024 * 1024, document = 32 * 1024 * 1024, audio = 25 * 1024 * 1024 }

-- Each role carries the parts it may hold, so an image in an assistant turn or a reasoning in a user turn is refused by name.
local carried = {
    system = { text = true },
    user = { text = true, image = true, document = true, audio = true },
    assistant = { text = true, reasoning = true },
    tool = { text = true, image = true },
}

local fields = {
    text = { type = true, text = true },
    image = { type = true, mediaType = true, data = true, url = true, detail = true },
    document = { type = true, mediaType = true, data = true, name = true },
    audio = { type = true, format = true, data = true },
    reasoning = { type = true, text = true, signature = true, redacted = true },
}

local function refuse(code, message, detail)
    error({ code = code, message = message, detail = detail }, 0)
end

local function object(value)
    return type(value) == "table" and value ~= codec.null and not codec.isList(value)
end

local function exactly(value, allowed, path)
    for key in pairs(value) do
        if not allowed[key] then
            refuse("ai_message_part_invalid", "A message part carries a field its type does not declare", path .. "." .. tostring(key))
        end
    end
end

local function bytes(value, kind, path)
    if type(value) ~= "string" or value == "" or #value > largest[kind] then
        refuse("ai_message_part_invalid", "A message part carries no data or more than its type allows", path .. ".data")
    end

    return value
end

-- A part is copied field by field, so what a caller still holds never changes the conversation afterwards.
local readers = {
    text = function(value, path)
        if type(value.text) ~= "string" then
            refuse("ai_message_part_invalid", "A text part carries no text", path .. ".text")
        end

        return { type = "text", text = value.text }
    end,
    image = function(value, path)
        local linked = value.url ~= nil

        if linked == (value.data ~= nil) then
            refuse("ai_message_part_invalid", "An image carries exactly one of its data and its address", path)
        end

        if linked and (type(value.url) ~= "string" or value.url:match("^https?://[^%s]+$") == nil) then
            refuse("ai_message_part_invalid", "An image address is not a web address", path .. ".url")
        end

        if (value.mediaType ~= nil or not linked) and not imageTypes[value.mediaType] then
            refuse("ai_message_part_invalid", "An image is not a PNG, JPEG, GIF or WebP image", path .. ".mediaType")
        end

        if value.detail ~= nil and not details[value.detail] then
            refuse("ai_message_part_invalid", "An image detail is not low, high or auto", path .. ".detail")
        end

        return { type = "image", mediaType = value.mediaType, data = not linked and bytes(value.data, "image", path) or nil, url = value.url, detail = value.detail }
    end,
    document = function(value, path)
        local data = bytes(value.data, "document", path)

        if value.mediaType ~= "application/pdf" and not textTypes[value.mediaType] then
            refuse("ai_message_part_invalid", "A document is not a PDF or a text", path .. ".mediaType")
        end

        if textTypes[value.mediaType] and utf8.len(data) == nil then
            refuse("ai_message_part_invalid", "A text document is not written in UTF-8", path .. ".data")
        end

        if type(value.name) ~= "string" or value.name:match("%S") == nil then
            refuse("ai_message_part_invalid", "A document carries no name", path .. ".name")
        end

        return { type = "document", mediaType = value.mediaType, data = data, name = value.name }
    end,
    audio = function(value, path)
        if not audioFormats[value.format] then
            refuse("ai_message_part_invalid", "An audio is not WAV or MP3", path .. ".format")
        end

        return { type = "audio", format = value.format, data = bytes(value.data, "audio", path) }
    end,
    reasoning = function(value, path)
        if type(value.text) ~= "string" or (value.signature ~= nil and type(value.signature) ~= "string") or (value.redacted ~= nil and type(value.redacted) ~= "string") then
            refuse("ai_message_part_invalid", "A reasoning part carries no text, or a signature or redacted data that is not a text", path)
        end

        return { type = "reasoning", text = value.text, signature = value.signature, redacted = value.redacted }
    end,
}

local function part(value, role, path)
    if not object(value) or type(value.type) ~= "string" or fields[value.type] == nil then
        refuse("ai_message_part_invalid", "A message part has no known type", path .. ".type")
    end

    if not carried[role][value.type] then
        refuse("ai_message_part_invalid", "A message part has a type its role does not carry", path .. ".type")
    end

    exactly(value, fields[value.type], path)

    return readers[value.type](value, path)
end

-- Content is a text or a list of parts, and a text reads as one text part.
local function content(value, role, path)
    if type(value) == "string" then
        return { { type = "text", text = value } }
    end

    if value == nil then
        return {}
    end

    if type(value) ~= "table" or not (codec.isList(value) or next(value) == nil) then
        refuse("ai_message_content_invalid", "A message content is neither a text nor a list of parts", path)
    end

    local parts = {}

    for index, entry in ipairs(value) do
        parts[index] = part(entry, role, path .. "[" .. index .. "]")
    end

    return parts
end

local function said(parts)
    for _, entry in ipairs(parts) do
        if entry.type ~= "text" or entry.text:match("%S") ~= nil then
            return true
        end
    end

    return false
end

-- A call carries an identifier, a name without spaces and its arguments as an object, and no identifier repeats within its turn.
local function calls(value, path)
    if value == nil then
        return {}
    end

    if type(value) ~= "table" or not (codec.isList(value) or next(value) == nil) then
        refuse("ai_message_tool_call_invalid", "The tool calls are not a list", path)
    end

    local read = {}
    local seen = {}

    for index, call in ipairs(value) do
        local at = path .. "[" .. index .. "]"
        local valid = object(call) and type(call.id) == "string" and call.id ~= "" and type(call.name) == "string" and call.name:match("^%S+$") ~= nil and #call.name <= 256

        if not valid or not object(call.arguments) or seen[call.id] then
            refuse("ai_message_tool_call_invalid", "A tool call carries no identifier, an invalid name, arguments that are not an object or an identifier its turn already used", at)
        end

        seen[call.id] = true
        read[index] = { id = call.id, name = call.name, arguments = call.arguments }
    end

    return read
end

local messageFields = { role = true, content = true, toolCalls = true, toolCallId = true, failed = true }

-- Answers the conversation in its canonical form or raises the first rule it breaks, naming where, so a request never reaches a provider that would refuse it.
-- A call is answered by tool messages before anything else follows, since every protocol refuses a result far from its call.
function messages.normalize(list)
    if type(list) ~= "table" or not codec.isList(list) then
        refuse("ai_conversation_empty", "The conversation holds no message", "messages")
    end

    local normalized = {}
    local open = {}
    local unanswered = 0
    local spoken = false

    for index, value in ipairs(list) do
        local path = "messages[" .. index .. "]"
        local role = object(value) and value.role or nil

        if carried[role] == nil then
            refuse("ai_message_role_invalid", "A message has no role among system, user, assistant and tool", path .. ".role")
        end

        for key in pairs(value) do
            if not messageFields[key] then
                refuse("ai_message_content_invalid", "A message carries a field it does not declare", path .. "." .. tostring(key))
            end
        end

        if unanswered > 0 and role ~= "tool" then
            refuse("ai_message_tool_result_invalid", "A tool call is not answered before the next message", path)
        end

        local message = { role = role, content = content(value.content, role, path .. ".content") }

        if role == "assistant" then
            message.toolCalls = calls(value.toolCalls, path .. ".toolCalls")

            for _, call in ipairs(message.toolCalls) do
                open[call.id] = call.name
                unanswered = unanswered + 1
            end
        elseif value.toolCalls ~= nil then
            refuse("ai_message_tool_call_invalid", "Only an assistant turn calls tools", path .. ".toolCalls")
        end

        if role == "tool" then
            if type(value.toolCallId) ~= "string" or open[value.toolCallId] == nil then
                refuse("ai_message_tool_result_invalid", "A tool result answers no open call", path .. ".toolCallId")
            end

            if value.failed ~= nil and type(value.failed) ~= "boolean" then
                refuse("ai_message_tool_result_invalid", "A tool result says whether it failed with a boolean", path .. ".failed")
            end

            open[value.toolCallId] = nil
            unanswered = unanswered - 1
            message.toolCallId = value.toolCallId
            message.failed = value.failed == true
        elseif value.toolCallId ~= nil or value.failed ~= nil then
            refuse("ai_message_tool_result_invalid", "Only a tool message answers a call", path)
        end

        if not said(message.content) and (message.toolCalls == nil or #message.toolCalls == 0) then
            refuse("ai_message_content_invalid", "A message says nothing", path .. ".content")
        end

        spoken = spoken or role ~= "system"
        normalized[index] = message
    end

    if unanswered > 0 then
        refuse("ai_message_tool_result_invalid", "The conversation ends with a tool call nobody answered", "messages")
    end

    if not spoken then
        refuse("ai_conversation_empty", "The conversation holds no message besides its instructions", "messages")
    end

    return normalized
end

-- Answers the largest part of a type a message carries, which a reader of files checks before reading one.
function messages.largestBytes(kind)
    return largest[kind]
end

-- Answers the text a message says, its parts joined by blank lines.
function messages.text(message)
    local texts = {}

    for _, entry in ipairs(message.content) do
        if entry.type == "text" and entry.text ~= "" then
            texts[#texts + 1] = entry.text
        end
    end

    return table.concat(texts, "\n\n")
end

local function note(text)
    return { type = "text", text = text }
end

-- Answers a part the way the model reads it, where what it cannot read becomes a note and a document written in text becomes its text headed by its name.
local function fittedPart(entry, traits, translate, adjust)
    if entry.type == "image" and not traits.vision then
        adjust("image-omitted")
        return note(translate("ai.message.image-omitted"))
    end

    if entry.type == "document" and textTypes[entry.mediaType] then
        return note(translate("ai.message.document-text", entry.name) .. "\n\n" .. entry.data)
    end

    if entry.type == "document" and not traits.pdf then
        adjust("document-omitted")
        return note(translate("ai.message.document-omitted", entry.name))
    end

    if entry.type == "audio" and not traits.audio then
        adjust("audio-omitted")
        return note(translate("ai.message.audio-omitted"))
    end

    return entry
end

-- Answers the conversation the way the model reads it and the kinds of change that took, each once, so a part the model cannot read becomes a note the model can.
-- Every message keeps its place, so a list kept beside the conversation still lines up with it.
function messages.fit(list, traits, translate)
    local fitted = {}
    local kinds = {}
    local names = {}
    local tools = traits["function-calling"] == true

    local function adjust(kind)
        if not kinds[kind] then
            kinds[kind] = true
            kinds[#kinds + 1] = kind
        end
    end

    for index, message in ipairs(list) do
        local copy = { role = message.role, content = {}, toolCalls = message.toolCalls, toolCallId = message.toolCallId, failed = message.failed }

        for position, entry in ipairs(message.content) do
            copy.content[position] = fittedPart(entry, traits, translate, adjust)
        end

        if message.role == "system" and not traits["system-prompt"] then
            adjust("system-as-user")
            copy.role = "user"
        end

        for _, call in ipairs(message.toolCalls or {}) do
            names[call.id] = call.name
        end

        -- A model that calls no tool reads the calls and results of an earlier model as what was said.
        if not tools and message.role == "assistant" and #(message.toolCalls or {}) > 0 then
            adjust("tools-as-text")

            for _, call in ipairs(message.toolCalls) do
                copy.content[#copy.content + 1] = note(translate("ai.message.tool-call", call.name, codec.encode(call.arguments)))
            end

            copy.toolCalls = {}
        end

        if not tools and message.role == "tool" then
            adjust("tools-as-text")
            table.insert(copy.content, 1, note(translate(message.failed and "ai.message.tool-failed" or "ai.message.tool-result", names[message.toolCallId] or "")))
            copy.role = "user"
            copy.toolCallId = nil
            copy.failed = nil
        end

        fitted[index] = copy
    end

    local adjustments = {}

    for index = 1, #kinds do
        adjustments[index] = kinds[index]
    end

    return fitted, adjustments
end

-- Answers the tokens a text takes, estimated the same way for every protocol.
function messages.textTokens(text)
    return #text // charactersPerToken
end

-- A PDF is counted by its pages, each read as text and as a picture, and every other part by what it carries.
local function partTokens(entry)
    if entry.type == "text" or entry.type == "reasoning" then
        return messages.textTokens(entry.text)
    end

    if entry.type == "image" then
        return imageTokens
    end

    if entry.type == "audio" then
        return #entry.data // audioBytesPerToken
    end

    if entry.mediaType ~= "application/pdf" then
        return messages.textTokens(entry.data)
    end

    local _, pages = entry.data:gsub("/Type%s*/Page%f[^%w]", "")
    return math.max(1, pages) * pageTokens
end

-- Answers how many tokens a message takes in the window of a model, estimated the same way for every protocol.
function messages.tokens(message)
    local total = messageTokens

    for _, entry in ipairs(message.content) do
        total = total + partTokens(entry)
    end

    for _, call in ipairs(message.toolCalls or {}) do
        total = total + callTokens + messages.textTokens(call.name .. codec.encode(call.arguments))
    end

    return total
end

-- Answers what a message said in words, naming what it carried besides text, which is how a summary reads a conversation.
function messages.describe(message, translate)
    local lines = {}

    for _, entry in ipairs(message.content) do
        if entry.type == "text" and entry.text ~= "" then
            lines[#lines + 1] = entry.text
        elseif entry.type == "image" then
            lines[#lines + 1] = translate("ai.message.image-described")
        elseif entry.type == "document" then
            lines[#lines + 1] = translate("ai.message.document-described", entry.name)
        elseif entry.type == "audio" then
            lines[#lines + 1] = translate("ai.message.audio-described")
        end
    end

    for _, call in ipairs(message.toolCalls or {}) do
        lines[#lines + 1] = translate("ai.message.tool-call", call.name, codec.encode(call.arguments))
    end

    return table.concat(lines, "\n")
end

return messages
