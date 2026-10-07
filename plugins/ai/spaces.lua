-- Trims the white space around a text with one scan from each end, so a text padded with a long run of spaces costs its length once.
local spaces = {}

function spaces.trim(text)
    local first = text:find("%S")

    if first == nil then
        return ""
    end

    return text:sub(first, (text:find("%S%s*$")))
end

return spaces
