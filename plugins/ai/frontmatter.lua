-- Reads the YAML front matter that opens the Markdown documents of the agent ecosystems, such as skills, commands and agents, in the subset those documents use.
local spaces = include("spaces")

local frontmatter = {}

local trimmed = spaces.trim

local function indentation(line)
    return #line:match("^( *)")
end

-- A quoted value keeps its escapes the way YAML reads them, and a plain value ends before a comment.
local function scalar(raw)
    local value = trimmed(raw)
    local quoted = value:match('^("[^"]*")%s+#') or value:match("^('[^']*')%s+#")
    value = quoted or value
    local double = value:match('^"(.*)"$')

    if double ~= nil then
        return (double:gsub('\\(.)', { n = "\n", t = "\t", ['"'] = '"', ["\\"] = "\\" }))
    end

    local single = value:match("^'(.*)'$")

    if single ~= nil then
        return (single:gsub("''", "'"))
    end

    return trimmed(value:gsub("%s+#.*$", ""))
end

local function inlineList(raw)
    local list = {}

    for item in trimmed(raw):sub(2, -2):gmatch("[^,]+") do
        if trimmed(item) ~= "" then
            list[#list + 1] = scalar(item)
        end
    end

    return list
end

-- A block scalar keeps its lines when literal and joins them with spaces when folded, and a trailing dash drops the final line break.
local function block(lines, index, style)
    local collected = {}
    local depth = nil
    local following = index + 1

    while following <= #lines do
        local line = lines[following]

        if trimmed(line) ~= "" and indentation(line) == 0 then
            break
        end

        depth = depth or (trimmed(line) ~= "" and indentation(line) or nil)
        collected[#collected + 1] = depth ~= nil and line:sub(depth + 1) or ""
        following = following + 1
    end

    while #collected > 0 and trimmed(collected[#collected]) == "" do
        collected[#collected] = nil
    end

    local text = style:sub(1, 1) == "|" and table.concat(collected, "\n") or trimmed(table.concat(collected, " "):gsub("%s+", " "))
    local keep = style:sub(2, 2) ~= "-" and style:sub(1, 1) == "|"

    return keep and text .. "\n" or text, following
end

-- A key without a value opens either a list of dashed items or a map of indented keys, one level deep.
local function nested(lines, index)
    local list = {}
    local map = {}
    local isList = false
    local following = index + 1

    while following <= #lines do
        local line = lines[following]

        if trimmed(line) ~= "" and indentation(line) == 0 then
            break
        end

        local item = line:match("^%s+%-%s*(.*)$")
        local key, value = line:match("^%s+([%w_%-]+)%s*:%s*(.-)%s*$")

        if item ~= nil then
            isList = true
            list[#list + 1] = scalar(item)
        elseif key ~= nil then
            map[key] = scalar(value)
        end

        following = following + 1
    end

    return isList and list or map, following
end

-- Answers the fields of the front matter and the body that follows it, or no fields and the whole text when the document opens without one.
function frontmatter.read(text)
    local normalized = text:gsub("^\239\187\191", ""):gsub("\r\n", "\n")

    if normalized:match("^%-%-%-[ \t]*\n%-%-%-[ \t]*\n?") ~= nil then
        return {}, (normalized:gsub("^%-%-%-[ \t]*\n%-%-%-[ \t]*\n?", ""))
    end

    local header, body = normalized:match("^%-%-%-[ \t]*\n(.-)\n%-%-%-[ \t]*\n?(.*)$")

    if header == nil then
        return {}, normalized
    end

    local lines = {}

    for line in (header .. "\n"):gmatch("(.-)\n") do
        lines[#lines + 1] = line
    end

    local fields = {}
    local index = 1

    while index <= #lines do
        local key, value = lines[index]:match("^([%w_%-]+)%s*:%s*(.-)%s*$")
        local following = index + 1

        if key ~= nil and (value == "|" or value == ">" or value == "|-" or value == ">-") then
            fields[key], following = block(lines, index, value)
        elseif key ~= nil and value == "" then
            fields[key], following = nested(lines, index)
        elseif key ~= nil and value:match("^%[.*%]$") ~= nil then
            fields[key] = inlineList(value)
        elseif key ~= nil then
            fields[key] = scalar(value)
        end

        index = following
    end

    return fields, body
end

-- Answers a field as text, joining a list with spaces, and an empty text for a field the document does not declare.
function frontmatter.text(fields, key)
    local value = fields[key]

    if type(value) == "table" then
        return table.concat(value, " ")
    end

    return type(value) == "string" and value or ""
end

return frontmatter
