-- Reads and writes JSON keeping what a Lua table cannot tell apart by itself: an empty list and null.
local codec = {}

local listMark = {}
local maximumDepth = 128

-- The null a document carried where the reader asked to keep it, which encodes back as null.
codec.null = setmetatable({}, { __tostring = function()
    return "null"
end })

-- Marks a table as a list, which is how an empty one is written as brackets.
function codec.list(items)
    return setmetatable(items or {}, listMark)
end

-- Answers whether a table is written as a list: one marked as such or a sequence from one without holes.
function codec.isList(value)
    if type(value) ~= "table" or value == codec.null then
        return false
    end

    if getmetatable(value) == listMark then
        return true
    end

    local count = 0

    for _ in pairs(value) do
        count = count + 1
    end

    return count > 0 and count == #value
end

local escapes = { ['"'] = '\\"', ["\\"] = "\\\\", ["\b"] = "\\b", ["\f"] = "\\f", ["\n"] = "\\n", ["\r"] = "\\r", ["\t"] = "\\t" }

-- Bytes that are not UTF-8 are written as the replacement character, so a document always leaves as valid text.
function codec.text(value)
    local valid = {}
    local position = 1

    while position <= #value do
        local length, invalid = utf8.len(value, position)

        if length ~= nil then
            valid[#valid + 1] = value:sub(position)
            break
        end

        valid[#valid + 1] = value:sub(position, invalid - 1) .. "\xEF\xBF\xBD"
        position = invalid + 1
    end

    return table.concat(valid)
end

local function quoted(value)
    return '"' .. codec.text(value):gsub('[%c"\\]', function(character)
        return escapes[character] or string.format("\\u%04x", character:byte())
    end) .. '"'
end

local encodeValue

local function encodeNumber(value)
    if value ~= value or value == math.huge or value == -math.huge then
        return "null"
    end

    if math.type(value) == "integer" then
        return tostring(value)
    end

    if value == math.floor(value) and math.abs(value) < 2 ^ 53 then
        return string.format("%d", value)
    end

    return string.format("%.17g", value)
end

encodeValue = function(value, depth, parts)
    local kind = type(value)

    if depth > maximumDepth then
        error("A JSON document nests deeper than the permitted depth", 0)
    end

    if value == nil or value == codec.null then
        parts[#parts + 1] = "null"
    elseif kind == "boolean" then
        parts[#parts + 1] = value and "true" or "false"
    elseif kind == "number" then
        parts[#parts + 1] = encodeNumber(value)
    elseif kind == "string" then
        parts[#parts + 1] = quoted(value)
    elseif kind == "table" and codec.isList(value) then
        parts[#parts + 1] = "["

        for index = 1, #value do
            if index > 1 then
                parts[#parts + 1] = ","
            end

            encodeValue(value[index], depth + 1, parts)
        end

        parts[#parts + 1] = "]"
    elseif kind == "table" then
        local keys = {}

        for key in pairs(value) do
            keys[#keys + 1] = tostring(key)
        end

        table.sort(keys)
        parts[#parts + 1] = "{"

        for index, key in ipairs(keys) do
            if index > 1 then
                parts[#parts + 1] = ","
            end

            parts[#parts + 1] = quoted(key) .. ":"
            local entry = value[key]

            if entry == nil then
                entry = value[math.tointeger(tonumber(key)) or key]
            end

            encodeValue(entry, depth + 1, parts)
        end

        parts[#parts + 1] = "}"
    else
        error("A JSON document cannot carry a value of type \"" .. kind .. "\"", 0)
    end
end

function codec.encode(value)
    local parts = {}
    encodeValue(value, 0, parts)

    return table.concat(parts)
end

-- Writes a document indented by two spaces per level, the way a reader inspects an exchanged payload.
function codec.pretty(value, depth)
    local level = depth or 0
    local indent = string.rep("  ", level + 1)
    local closing = string.rep("  ", level)

    if type(value) ~= "table" or value == codec.null or next(value) == nil then
        return codec.encode(value)
    end

    local lines = {}

    if codec.isList(value) then
        for index, entry in ipairs(value) do
            lines[index] = indent .. codec.pretty(entry, level + 1)
        end

        return "[\n" .. table.concat(lines, ",\n") .. "\n" .. closing .. "]"
    end

    local keys = {}

    for key in pairs(value) do
        keys[#keys + 1] = tostring(key)
    end

    table.sort(keys)

    for index, key in ipairs(keys) do
        lines[index] = indent .. quoted(key) .. ": " .. codec.pretty(value[key], level + 1)
    end

    return "{\n" .. table.concat(lines, ",\n") .. "\n" .. closing .. "}"
end

-- The decoder walks the text once, keeps lists marked and keeps null only where the reader asked for it.
local Decoder = {}
Decoder.__index = Decoder

local function fail(position)
    error({ code = "ai_json_invalid", message = "The JSON document is invalid", detail = tostring(position) }, 0)
end

function Decoder:space()
    self.position = self.text:find("[^ \t\r\n]", self.position) or #self.text + 1
end

function Decoder:string()
    local parts = {}
    local position = self.position + 1

    while true do
        local stop = self.text:find('["\\]', position)

        if stop == nil then
            fail(position)
        end

        parts[#parts + 1] = self.text:sub(position, stop - 1)

        if self.text:sub(stop, stop) == '"' then
            self.position = stop + 1
            return table.concat(parts)
        end

        local escape = self.text:sub(stop + 1, stop + 1)
        local simple = ({ ['"'] = '"', ["\\"] = "\\", ["/"] = "/", b = "\b", f = "\f", n = "\n", r = "\r", t = "\t" })[escape]

        if simple ~= nil then
            parts[#parts + 1] = simple
            position = stop + 2
        elseif escape == "u" then
            local code = tonumber(self.text:sub(stop + 2, stop + 5), 16)

            if code == nil then
                fail(stop)
            end

            position = stop + 6

            -- A character beyond the first plane arrives as a pair of surrogates, which join into one.
            if code >= 0xD800 and code <= 0xDBFF and self.text:sub(position, position + 1) == "\\u" then
                local low = tonumber(self.text:sub(position + 2, position + 5), 16)

                if low ~= nil and low >= 0xDC00 and low <= 0xDFFF then
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00)
                    position = position + 6
                end
            end

            parts[#parts + 1] = (code >= 0xD800 and code <= 0xDFFF) and "\xEF\xBF\xBD" or utf8.char(code)
        else
            fail(stop)
        end
    end
end

function Decoder:value(depth)
    if depth > maximumDepth then
        fail(self.position)
    end

    self:space()
    local character = self.text:sub(self.position, self.position)

    if character == "{" then
        local object = {}
        self.position = self.position + 1
        self:space()

        if self.text:sub(self.position, self.position) == "}" then
            self.position = self.position + 1
            return object
        end

        while true do
            self:space()

            if self.text:sub(self.position, self.position) ~= '"' then
                fail(self.position)
            end

            local key = self:string()
            self:space()

            if self.text:sub(self.position, self.position) ~= ":" then
                fail(self.position)
            end

            self.position = self.position + 1
            local entry = self:value(depth + 1)

            if entry ~= codec.null or self.keepNull then
                object[key] = entry
            end

            self:space()
            local separator = self.text:sub(self.position, self.position)
            self.position = self.position + 1

            if separator == "}" then
                return object
            end

            if separator ~= "," then
                fail(self.position)
            end
        end
    end

    if character == "[" then
        local list = codec.list()
        self.position = self.position + 1
        self:space()

        if self.text:sub(self.position, self.position) == "]" then
            self.position = self.position + 1
            return list
        end

        while true do
            list[#list + 1] = self:value(depth + 1)
            self:space()
            local separator = self.text:sub(self.position, self.position)
            self.position = self.position + 1

            if separator == "]" then
                return list
            end

            if separator ~= "," then
                fail(self.position)
            end
        end
    end

    if character == '"' then
        return self:string()
    end

    for literal, value in pairs({ ["true"] = true, ["false"] = false, ["null"] = codec.null }) do
        if self.text:sub(self.position, self.position + #literal - 1) == literal then
            self.position = self.position + #literal
            return value
        end
    end

    local number = self.text:match("^-?%d+%.?%d*[eE]?[-+]?%d*", self.position)

    if number == nil or number == "" or number == "-" then
        fail(self.position)
    end

    self.position = self.position + #number
    local parsed = tonumber(number)

    if parsed == nil then
        fail(self.position)
    end

    return math.tointeger(parsed) or parsed
end

-- Decodes a document and raises a structured failure on anything that is not JSON, keeping null only when asked, since a missing field reads the same way.
function codec.decode(text, keepNull)
    if type(text) ~= "string" then
        fail(0)
    end

    local decoder = setmetatable({ text = text, position = 1, keepNull = keepNull == true }, Decoder)
    local value = decoder:value(0)
    decoder:space()

    if decoder.position <= #text then
        fail(decoder.position)
    end

    return value
end

-- Answers a value as plain Lua data for a reader outside this plugin, with every list a plain sequence and every null left out.
function codec.plain(value)
    if value == codec.null then
        return nil
    end

    if type(value) ~= "table" then
        return value
    end

    local copy = {}

    for key, entry in pairs(value) do
        copy[key] = codec.plain(entry)
    end

    return copy
end

-- Answers the decoded value, or nil and the failure, for a reader that must not raise.
function codec.read(text, keepNull)
    local decoded, value = pcall(codec.decode, text, keepNull)

    if not decoded then
        return nil, value
    end

    return value
end

return codec
