-- Reads the EditorConfig files that apply to a document, from its folder up to the open folder, and answers the properties the editor honours.
local fs = require("fs")
local paths = include("paths")

local editorconfig = {}

local maximumBraceDepth = 16
local maximumRange = 4096
local defaultWidth = 4
local largestWidth = 16

local charsets = { ["utf-8"] = true, ["utf-8-bom"] = true, ["utf-16le"] = true, ["utf-16be"] = true, latin1 = true }
local endings = { lf = true, crlf = true, cr = true }

-- A glob becomes a list of tokens: literal characters, stars, double stars, single characters, classes, alternatives and numeric ranges.
local function compile(pattern, depth)
    local tokens = {}
    local index = 1

    while index <= #pattern do
        local character = pattern:sub(index, index)

        if character == "\\" and index < #pattern then
            tokens[#tokens + 1] = { kind = "literal", text = pattern:sub(index + 1, index + 1) }
            index = index + 2
        elseif character == "*" and pattern:sub(index + 1, index + 1) == "*" then
            tokens[#tokens + 1] = { kind = "globstar" }
            index = index + 2
        elseif character == "*" then
            tokens[#tokens + 1] = { kind = "star" }
            index = index + 1
        elseif character == "?" then
            tokens[#tokens + 1] = { kind = "any" }
            index = index + 1
        elseif character == "[" then
            local closing = pattern:find("]", index + 1, true)
            local body = closing and pattern:sub(index + 1, closing - 1) or nil

            if body == nil or body:find("/", 1, true) then
                tokens[#tokens + 1] = { kind = "literal", text = "[" }
                index = index + 1
            else
                local negated = body:sub(1, 1) == "!" or body:sub(1, 1) == "^"
                tokens[#tokens + 1] = { kind = "class", negated = negated, body = negated and body:sub(2) or body }
                index = closing + 1
            end
        elseif character == "{" then
            local level = 1
            local cursor = index + 1

            while cursor <= #pattern and level > 0 do
                local current = pattern:sub(cursor, cursor)
                level = level + (current == "{" and 1 or current == "}" and -1 or 0)
                cursor = cursor + (current == "\\" and 2 or 1)
            end

            local body = level == 0 and pattern:sub(index + 1, cursor - 2) or nil
            local low, high = (body or ""):match("^(%-?%d+)%.%.(%-?%d+)$")

            if body == nil or depth >= maximumBraceDepth then
                tokens[#tokens + 1] = { kind = "literal", text = "{" }
                index = index + 1
            elseif low ~= nil and math.abs(tonumber(high) - tonumber(low)) < maximumRange then
                tokens[#tokens + 1] = { kind = "range", low = math.min(tonumber(low), tonumber(high)), high = math.max(tonumber(low), tonumber(high)) }
                index = cursor
            else
                local alternatives = {}
                local start = 1
                local nested = 0

                for position = 1, #body + 1 do
                    local current = position <= #body and body:sub(position, position) or ","
                    nested = nested + (current == "{" and 1 or current == "}" and -1 or 0)

                    if current == "," and nested == 0 then
                        alternatives[#alternatives + 1] = compile(body:sub(start, position - 1), depth + 1)
                        start = position + 1
                    end
                end

                -- Braces holding a single choice are no alternatives, so they read as the characters they are.
                if #alternatives == 1 then
                    tokens[#tokens + 1] = { kind = "literal", text = "{" }
                    index = index + 1
                else
                    tokens[#tokens + 1] = { kind = "alternatives", options = alternatives }
                    index = cursor
                end
            end
        else
            tokens[#tokens + 1] = { kind = "literal", text = character }
            index = index + 1
        end
    end

    return tokens
end

local function inClass(body, character)
    local index = 1

    while index <= #body do
        local first = body:sub(index, index)

        if body:sub(index + 1, index + 1) == "-" and index + 2 <= #body then
            if character >= first and character <= body:sub(index + 2, index + 2) then
                return true
            end

            index = index + 3
        else
            if character == first then
                return true
            end

            index = index + 1
        end
    end

    return false
end

-- Matches the tokens from a position against the text from a position, backtracking over stars and alternatives.
local function match(tokens, tokenIndex, text, textIndex)
    if tokenIndex > #tokens then
        return textIndex > #text
    end

    local token = tokens[tokenIndex]

    if token.kind == "literal" then
        return text:sub(textIndex, textIndex + #token.text - 1) == token.text and match(tokens, tokenIndex + 1, text, textIndex + #token.text)
    end

    if token.kind == "any" then
        return textIndex <= #text and text:sub(textIndex, textIndex) ~= "/" and match(tokens, tokenIndex + 1, text, textIndex + 1)
    end

    if token.kind == "class" then
        local character = text:sub(textIndex, textIndex)
        return textIndex <= #text and character ~= "/" and inClass(token.body, character) ~= token.negated and match(tokens, tokenIndex + 1, text, textIndex + 1)
    end

    -- A double star between two separators also stands for no folder at all, so `a/**/b` reaches `a/b`.
    local following = tokens[tokenIndex + 1]
    local between = token.kind == "globstar" and following ~= nil and following.kind == "literal" and following.text == "/" and (textIndex == 1 or text:sub(textIndex - 1, textIndex - 1) == "/")

    if between and match(tokens, tokenIndex + 2, text, textIndex) then
        return true
    end

    if token.kind == "star" or token.kind == "globstar" then
        for last = textIndex - 1, #text do
            local skipped = text:sub(textIndex, last)

            if token.kind == "star" and skipped:find("/", 1, true) then
                return false
            end

            if match(tokens, tokenIndex + 1, text, last + 1) then
                return true
            end
        end

        return false
    end

    if token.kind == "range" then
        local digits = text:match("^%-?%d+", textIndex)
        local value = digits and tonumber(digits)
        return value ~= nil and value >= token.low and value <= token.high and match(tokens, tokenIndex + 1, text, textIndex + #digits)
    end

    for _, option in ipairs(token.options) do
        local joined = {}

        for _, item in ipairs(option) do
            joined[#joined + 1] = item
        end

        for index = tokenIndex + 1, #tokens do
            joined[#joined + 1] = tokens[index]
        end

        if match(joined, 1, text, textIndex) then
            return true
        end
    end

    return false
end

-- A section without a slash applies in every folder below the file, and a leading slash anchors it to the folder of the file.
function editorconfig.matches(section, relative)
    local pattern = section

    if not section:find("/", 1, true) then
        pattern = "**/" .. section
        relative = "/" .. relative
    elseif section:sub(1, 1) == "/" then
        pattern = section:sub(2)
    end

    return match(compile(pattern, 0), 1, relative, 1)
end

local function parse(text)
    local file = { root = false, sections = {} }
    local current

    for raw in (text .. "\n"):gmatch("(.-)\r?\n") do
        local line = raw:match("^%s*(.-)%s*$")
        local section = line:match("^%[(.*)%]$")
        local key, value = line:match("^([^=]-)%s*=%s*(.-)$")

        if line == "" or line:sub(1, 1) == "#" or line:sub(1, 1) == ";" then
            goto continue
        end

        if section ~= nil then
            current = { pattern = section, properties = {} }
            file.sections[#file.sections + 1] = current
        elseif key ~= nil and current == nil and key:lower() == "root" then
            file.root = value:lower() == "true"
        elseif key ~= nil and current ~= nil then
            current.properties[#current.properties + 1] = { key = key:lower(), value = value:lower() }
        end

        ::continue::
    end

    return file
end

local function positive(value)
    local number = math.tointeger(tonumber(value))
    return number ~= nil and number > 0 and number or nil
end

local propertyNames = { indent_style = "indentStyle", indent_size = "indentSize", tab_width = "tabWidth", end_of_line = "endOfLine", charset = "charset", trim_trailing_whitespace = "trimTrailingWhitespace", insert_final_newline = "insertFinalNewline" }

-- A property naming a value outside its set is left unset, and unset clears whatever a farther file set under the name the editor reads it by.
local function apply(properties, key, value)
    if value == "unset" then
        if propertyNames[key] ~= nil then
            properties[propertyNames[key]] = nil
        end

        properties.unsupportedCharsets = key == "charset" and {} or properties.unsupportedCharsets
        return
    end

    if key == "indent_style" and (value == "tab" or value == "space") then
        properties.indentStyle = value
    elseif key == "indent_size" then
        properties.indentSize = value == "tab" and "tab" or positive(value)
    elseif key == "tab_width" then
        properties.tabWidth = positive(value)
    elseif key == "end_of_line" and endings[value] then
        properties.endOfLine = value
    elseif key == "charset" and charsets[value] then
        properties.charset = value
    elseif key == "charset" then
        properties.unsupportedCharsets[#properties.unsupportedCharsets + 1] = value
    elseif key == "trim_trailing_whitespace" and (value == "true" or value == "false") then
        properties.trimTrailingWhitespace = value == "true"
    elseif key == "insert_final_newline" and (value == "true" or value == "false") then
        properties.insertFinalNewline = value == "true"
    end
end

-- The files are read from the folder of the document up to the open folder, stopping at the first that declares itself the root, and applied farthest first.
function editorconfig.resolve(path, root)
    local files = {}
    local directory = paths.parent(path)

    while directory ~= nil and paths.inside(root, directory) do
        local found = paths.join(directory, ".editorconfig")
        local info = fs.stat(found):await()
        local text = info ~= nil and not info.isDir and fs.readFile(found):await() or nil
        local file = text ~= nil and parse(text) or { root = false, sections = {} }
        file.directory = directory
        table.insert(files, 1, file)

        if file.root or directory == root then
            break
        end

        directory = paths.parent(directory)
    end

    local properties = { unsupportedCharsets = {} }

    for _, file in ipairs(files) do
        local relative = paths.relative(file.directory, path)

        for _, section in ipairs(file.sections) do
            if editorconfig.matches(section.pattern, relative) then
                for _, property in ipairs(section.properties) do
                    apply(properties, property.key, property.value)
                end
            end
        end
    end

    -- An indent size of tab takes the width of a tab, and every width stays within the columns the editor draws.
    local style = properties.indentStyle or "space"
    local size = properties.indentSize == "tab" and (properties.tabWidth or defaultWidth) or properties.indentSize
    properties.indentStyle = style
    properties.indentWidth = math.max(1, math.min(largestWidth, style == "tab" and (properties.tabWidth or size or defaultWidth) or (size or properties.tabWidth or defaultWidth)))

    return properties
end

-- The folders whose EditorConfig files matter to a document, which the editor checks again when one of them changes.
function editorconfig.watched(path, root)
    local files = {}
    local directory = paths.parent(path)

    while directory ~= nil and paths.inside(root, directory) do
        files[#files + 1] = paths.join(directory, ".editorconfig")

        if directory == root then
            break
        end

        directory = paths.parent(directory)
    end

    return files
end

return editorconfig
