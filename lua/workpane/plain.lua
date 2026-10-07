-- Recognizes the tables of plain data, which the JSON reader of the runtime marks as arrays or objects without making them anything but data.
local json = require("json")

local plain = {}

local markers = { [getmetatable(json.array())] = true, [getmetatable(json.object())] = true }

-- Answers whether a metatable is one the JSON reader puts on the tables it reads, which carries no behavior of its own.
function plain.marker(metatable)
    return markers[metatable] == true
end

-- Answers whether a value is a table of data, one without a metatable or with only the mark of the JSON reader.
function plain.table(value)
    if type(value) ~= "table" then
        return false
    end

    local metatable = getmetatable(value)

    return metatable == nil or markers[metatable] == true
end

return plain
