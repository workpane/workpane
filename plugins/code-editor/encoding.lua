-- Reads the bytes of a file into the text the editor shows and writes that text back in the encoding and line ending the file keeps.
local crypto = require("crypto")

local encoding = {}

local names = { ["utf-8"] = "UTF-8", ["utf-8-bom"] = "UTF-8 BOM", ["utf-16le"] = "UTF-16 LE", ["utf-16be"] = "UTF-16 BE", latin1 = "Latin-1" }
local order = { "utf-8", "utf-8-bom", "utf-16le", "utf-16be", "latin1" }
local marks = { ["utf-8-bom"] = "\xEF\xBB\xBF", ["utf-16le"] = "\xFF\xFE", ["utf-16be"] = "\xFE\xFF" }

local function failure(code, detail)
    return { code = code, detail = detail or "" }
end

function encoding.charsets()
    return order
end

function encoding.label(charset)
    return names[charset]
end

function encoding.known(charset)
    return names[charset] ~= nil
end

local function utf8Valid(bytes)
    return utf8.len(bytes) ~= nil
end

-- Two bytes make one unit, and a pair of surrogates makes one character beyond the first plane.
local function fromUtf16(bytes, format)
    if #bytes % 2 ~= 0 then
        return nil
    end

    local characters = {}
    local index = 1

    while index <= #bytes do
        local unit = string.unpack(format, bytes, index)
        index = index + 2

        if unit >= 0xDC00 and unit <= 0xDFFF then
            return nil
        end

        if unit >= 0xD800 and unit <= 0xDBFF then
            if index > #bytes then
                return nil
            end

            local low = string.unpack(format, bytes, index)

            if low < 0xDC00 or low > 0xDFFF then
                return nil
            end

            index = index + 2
            characters[#characters + 1] = utf8.char(0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00))
        else
            characters[#characters + 1] = utf8.char(unit)
        end
    end

    return table.concat(characters)
end

local function fromLatin1(bytes)
    local characters = {}

    for index = 1, #bytes do
        characters[index] = utf8.char(bytes:byte(index))
    end

    return table.concat(characters)
end

local function decodeWith(bytes, charset)
    local mark = marks[charset]

    if mark ~= nil and bytes:sub(1, #mark) == mark then
        bytes = bytes:sub(#mark + 1)
    end

    if charset == "utf-8" or charset == "utf-8-bom" then
        return utf8Valid(bytes) and bytes or nil
    end

    if charset == "utf-16le" then
        return fromUtf16(bytes, "<I2")
    end

    if charset == "utf-16be" then
        return fromUtf16(bytes, ">I2")
    end

    return fromLatin1(bytes)
end

local function lineEnding(text)
    if text:find("\r\n", 1, true) then
        return "crlf"
    end

    return text:find("\r", 1, true) and "cr" or "lf"
end

-- A file is refused when it is UTF-32 or UTF-7, which the editor cannot write back, or binary, and otherwise read in the encoding chosen by hand, the one its mark names, UTF-8 or the default.
function encoding.decode(bytes, manual, default)
    if bytes:sub(1, 4) == "\0\0\xFE\xFF" or bytes:sub(1, 4) == "\xFF\xFE\0\0" then
        return nil, failure("code-editor.error.encoding-unsupported", " UTF-32")
    end

    if bytes:sub(1, 3) == "\x2B\x2F\x76" then
        return nil, failure("code-editor.error.encoding-unsupported", " UTF-7")
    end

    local charset = manual

    for _, candidate in ipairs({ "utf-8-bom", "utf-16le", "utf-16be" }) do
        if charset == nil and bytes:sub(1, #marks[candidate]) == marks[candidate] then
            charset = candidate
        end
    end

    if charset == nil and bytes:find("\0", 1, true) then
        return nil, failure("code-editor.error.binary-file")
    end

    if charset == nil then
        charset = utf8Valid(bytes) and "utf-8" or default
    end

    local text = decodeWith(bytes, charset)

    if text == nil then
        return nil, failure("code-editor.error.encoding")
    end

    return {
        text = text:gsub("\r\n", "\n"):gsub("\r", "\n"),
        charset = charset,
        lineEnding = lineEnding(text),
        digest = crypto.digest("SHA256", bytes),
    }
end

local function toUtf16(text, format)
    local units = {}

    for _, code in utf8.codes(text) do
        if code >= 0x10000 then
            local shifted = code - 0x10000
            units[#units + 1] = string.pack(format, 0xD800 + (shifted >> 10))
            units[#units + 1] = string.pack(format, 0xDC00 + (shifted & 0x3FF))
        else
            units[#units + 1] = string.pack(format, code)
        end
    end

    return table.concat(units)
end

local function toLatin1(text)
    local bytes = {}

    for _, code in utf8.codes(text) do
        if code > 0xFF then
            return nil
        end

        bytes[#bytes + 1] = string.char(code)
    end

    return table.concat(bytes)
end

-- The text becomes lines joined by the line ending, trimmed and closed as the EditorConfig asks, and then bytes in the charset with its mark.
function encoding.encode(text, charset, ending, options)
    local lines = {}

    for line in (text .. "\n"):gmatch("(.-)\n") do
        lines[#lines + 1] = options.trimTrailingWhitespace and line:gsub("[ \t\r]+$", "") or line
    end

    local separator = ({ lf = "\n", crlf = "\r\n", cr = "\r" })[ending]
    local joined = table.concat(lines, separator)

    if options.insertFinalNewline and joined ~= "" and joined:sub(-#separator) ~= separator then
        joined = joined .. separator
    end

    local bytes

    if charset == "utf-16le" then
        bytes = toUtf16(joined, "<I2")
    elseif charset == "utf-16be" then
        bytes = toUtf16(joined, ">I2")
    elseif charset == "latin1" then
        bytes = toLatin1(joined)
    else
        bytes = joined
    end

    if bytes == nil then
        return nil
    end

    return (marks[charset] or "") .. bytes
end

function encoding.digest(bytes)
    return crypto.digest("SHA256", bytes)
end

return encoding
