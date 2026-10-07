-- The rules preference values follow, declared once and then used to prove a whole value with the path of its first problem.
local bridge = require("workpane.bridge")
local plain = require("workpane.plain")

local rules = {}

local common = { type = true, default = true, check = true }

local specific = {
    boolean = {},
    integer = { minimum = true, maximum = true },
    number = { minimum = true, maximum = true },
    string = { choices = true, maxLength = true },
    list = { items = true, maxItems = true, unique = true },
    record = { fields = true },
    map = { values = true, maxEntries = true },
    any = {},
}

-- A list, a record and a map may leave their default out, because an empty one is what they hold before anything is written.
local containers = { list = true, record = true, map = true }

local function refuse(path, message)
    error(bridge.failure({ code = "preferences_rule_invalid", message = message, detail = path }), 0)
end

local function finite(value)
    return type(value) == "number" and value == value and value ~= math.huge and value ~= -math.huge
end

local function isList(value)
    if not plain.table(value) then
        return false
    end

    local count = 0

    for key in pairs(value) do
        if math.type(key) ~= "integer" or key < 1 then
            return false
        end

        count = count + 1
    end

    return count == #value
end

local function copy(value)
    if type(value) ~= "table" then
        return value
    end

    local result = {}

    for key, item in pairs(value) do
        result[key] = copy(item)
    end

    return result
end

-- Two values are the same when they hold the same plain data, whatever tables carry it.
function rules.same(first, second)
    if type(first) ~= "table" or type(second) ~= "table" then
        return first == second
    end

    for key, item in pairs(first) do
        if not rules.same(item, second[key]) then
            return false
        end
    end

    for key in pairs(second) do
        if first[key] == nil then
            return false
        end
    end

    return true
end

-- Plain data is text, numbers, booleans and tables of them keyed by text or numbers, without functions, cycles or metatables other than the mark of the JSON reader.
local function plainCopy(value, seen)
    local kind = type(value)

    if kind == "boolean" or kind == "string" or (kind == "number" and finite(value)) then
        return value
    end

    if not plain.table(value) or seen[value] then
        return nil
    end

    seen[value] = true
    local result = {}

    for key, item in pairs(value) do
        local keyKind = type(key)

        if keyKind ~= "string" and keyKind ~= "number" then
            return nil
        end

        local copied = plainCopy(item, seen)

        if copied == nil then
            return nil
        end

        result[key] = copied
    end

    seen[value] = nil

    return result
end

local accept

local function acceptList(rule, value, path)
    if not isList(value) or (rule.maxItems ~= nil and #value > rule.maxItems) then
        return nil, path
    end

    local result = {}

    for index, item in ipairs(value) do
        local itemPath = path .. "[" .. index .. "]"
        local accepted, problem = accept(rule.items, item, itemPath)

        if problem ~= nil then
            return nil, problem
        end

        for earlier = 1, #result do
            if rule.unique and rules.same(result[earlier], accepted) then
                return nil, itemPath
            end
        end

        result[index] = accepted
    end

    return result
end

-- A record holds only its declared fields, and a field it leaves out takes its default.
local function acceptRecord(rule, value, path)
    if not plain.table(value) then
        return nil, path
    end

    for key in pairs(value) do
        if type(key) ~= "string" or rule.fields[key] == nil then
            return nil, path .. "." .. tostring(key)
        end
    end

    local result = {}

    for name, field in pairs(rule.fields) do
        if value[name] == nil then
            result[name] = copy(field.default)
        else
            local accepted, problem = accept(field, value[name], path .. "." .. name)

            if problem ~= nil then
                return nil, problem
            end

            result[name] = accepted
        end
    end

    return result
end

local function acceptMap(rule, value, path)
    if not plain.table(value) then
        return nil, path
    end

    local result = {}
    local count = 0

    for key, item in pairs(value) do
        if type(key) ~= "string" then
            return nil, path .. "." .. tostring(key)
        end

        local accepted, problem = accept(rule.values, item, path .. "." .. key)

        if problem ~= nil then
            return nil, problem
        end

        result[key] = accepted
        count = count + 1
    end

    if rule.maxEntries ~= nil and count > rule.maxEntries then
        return nil, path
    end

    return result
end

-- Answers whether a boolean, a number or a text follows its rule, and the value it keeps, where an integer written as a whole float becomes an integer.
local function acceptScalar(rule, value)
    local kind = rule.type

    if kind == "boolean" then
        return type(value) == "boolean", value
    end

    if kind == "integer" then
        local integer = type(value) == "number" and math.tointeger(value) or nil
        return integer ~= nil and (rule.minimum == nil or integer >= rule.minimum) and (rule.maximum == nil or integer <= rule.maximum), integer
    end

    if kind == "number" then
        return finite(value) and (rule.minimum == nil or value >= rule.minimum) and (rule.maximum == nil or value <= rule.maximum), value
    end

    if type(value) ~= "string" or (rule.maxLength ~= nil and #value > rule.maxLength) then
        return false, nil
    end

    for _, choice in ipairs(rule.choices or { value }) do
        if choice == value then
            return true, value
        end
    end

    return false, nil
end

-- Answers a copy of the value that follows the rule, with the defaults of missing record fields in place, or nil and the path of the first problem.
accept = function(rule, value, path)
    local kind = rule.type
    local result
    local problem

    if kind == "list" then
        result, problem = acceptList(rule, value, path)
    elseif kind == "record" then
        result, problem = acceptRecord(rule, value, path)
    elseif kind == "map" then
        result, problem = acceptMap(rule, value, path)
    elseif kind == "any" then
        result = plainCopy(value, {})
        problem = result == nil and path or nil
    else
        local accepted
        accepted, result = acceptScalar(rule, value)
        problem = not accepted and path or nil
    end

    if problem ~= nil then
        return nil, problem
    end

    if rule.check ~= nil then
        local checked, verdict = pcall(rule.check, copy(result))

        if not checked or verdict ~= true then
            return nil, path
        end
    end

    return result, nil
end

rules.accept = accept

local function declareBounds(rule, path, integer)
    for _, bound in ipairs({ "minimum", "maximum" }) do
        local value = rule[bound]

        if value ~= nil and ((integer and math.type(value) ~= "integer") or not finite(value)) then
            refuse(path, "A bound of a rule is a finite number, and an integer for an integer")
        end
    end

    if rule.minimum ~= nil and rule.maximum ~= nil and rule.minimum > rule.maximum then
        refuse(path, "The bounds of a rule form no range")
    end
end

local function declareCount(rule, name, path)
    local value = rule[name]

    if value ~= nil and (math.type(value) ~= "integer" or value < 0) then
        refuse(path, "A count of a rule is an integer of zero or more")
    end
end

local function declareChoices(rule, path)
    if rule.choices == nil then
        return
    end

    if not isList(rule.choices) or #rule.choices == 0 then
        refuse(path, "The choices of a rule are a list of texts")
    end

    local seen = {}

    for _, choice in ipairs(rule.choices) do
        if type(choice) ~= "string" or seen[choice] then
            refuse(path, "The choices of a rule are distinct texts")
        end

        seen[choice] = true
    end
end

-- Proves a declared rule and answers a sealed copy of it, with the default of a container made from its empty value.
function rules.declare(rule, path, needsDefault)
    if not plain.table(rule) or type(rule.type) ~= "string" or specific[rule.type] == nil then
        refuse(path, "A rule is a table whose type is boolean, integer, number, string, list, record, map or any")
    end

    for field in pairs(rule) do
        if not common[field] and not specific[rule.type][field] then
            refuse(path .. "." .. tostring(field), "A rule declares a field its type does not take")
        end
    end

    if rule.check ~= nil and type(rule.check) ~= "function" then
        refuse(path, "The check of a rule is a function")
    end

    local declared = { type = rule.type, check = rule.check }

    if rule.type == "integer" or rule.type == "number" then
        declareBounds(rule, path, rule.type == "integer")
        declared.minimum, declared.maximum = rule.minimum, rule.maximum
    elseif rule.type == "string" then
        declareChoices(rule, path)
        declareCount(rule, "maxLength", path)
        declared.choices, declared.maxLength = copy(rule.choices), rule.maxLength
    elseif rule.type == "list" then
        declareCount(rule, "maxItems", path)

        if rule.unique ~= nil and type(rule.unique) ~= "boolean" then
            refuse(path, "Whether a list is unique is a boolean")
        end

        declared.items = rules.declare(rule.items, path .. "[]", false)
        declared.maxItems, declared.unique = rule.maxItems, rule.unique == true
    elseif rule.type == "record" then
        if not plain.table(rule.fields) then
            refuse(path, "A record declares its fields in a table")
        end

        declared.fields = {}

        for name, field in pairs(rule.fields) do
            if type(name) ~= "string" then
                refuse(path, "A record names its fields with texts")
            end

            declared.fields[name] = rules.declare(field, path .. "." .. name, true)
        end
    elseif rule.type == "map" then
        declareCount(rule, "maxEntries", path)
        declared.values = rules.declare(rule.values, path .. "{}", false)
        declared.maxEntries = rule.maxEntries
    end

    local default = rule.default

    if default == nil and containers[rule.type] then
        default = {}
    end

    if default == nil and needsDefault then
        refuse(path, "A rule of a value that may be missing declares its default")
    end

    if default ~= nil then
        local accepted, problem = accept(declared, default, path)

        if problem ~= nil then
            refuse(problem, "A rule declares a default it refuses itself")
        end

        declared.default = accepted
    end

    return declared
end

rules.copy = copy

return rules
