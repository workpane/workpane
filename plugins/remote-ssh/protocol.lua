local json = require("json")

local protocol = {}

protocol.maximumBody = 8 * 1024 * 1024
protocol.maximumDepth = 32

function protocol.failure(code)
    return { code = code, message = "The remote SSH operation could not be completed", detail = "" }
end

local function boundedJson(source)
    if #source == 0 or #source > protocol.maximumBody or utf8.len(source) == nil then
        return false
    end

    local depth = 0
    local quoted = false
    local position = 1

    while position <= #source do
        local found = source:find(quoted and '["\\]' or '[{}%[%]"]', position)

        if found == nil then
            break
        end

        local character = source:sub(found, found)
        position = found + 1

        if quoted and character == "\\" then
            position = position + 1
        elseif character == '"' then
            quoted = not quoted
        elseif not quoted and (character == "{" or character == "[") then
            depth = depth + 1
        elseif not quoted then
            depth = depth - 1
        end

        if depth < 0 or depth > protocol.maximumDepth then
            return false
        end
    end

    return depth == 0 and not quoted
end

local function plain(value, depth, seen)
    if value == json.null or type(value) == "boolean" then
        return true
    end

    if type(value) == "number" then
        return value == value and value ~= math.huge and value ~= -math.huge
    end

    if type(value) == "string" then
        return #value <= protocol.maximumBody and utf8.len(value) ~= nil
    end

    if type(value) ~= "table" or depth > protocol.maximumDepth or seen[value] then
        return false
    end

    seen[value] = true

    for key, item in pairs(value) do
        if (type(key) ~= "string" and (math.type(key) ~= "integer" or key < 1)) or (type(key) == "string" and utf8.len(key) == nil) or not plain(item, depth + 1, seen) then
            seen[value] = nil
            return false
        end
    end

    seen[value] = nil

    return true
end

function protocol.request(method, parameters)
    if type(method) ~= "string" or #method > 128 or method:match("^[a-z][a-z0-9.-]*$") == nil or type(parameters) ~= "table" then
        return nil, protocol.failure("ssh_request_invalid")
    end

    if not plain(parameters, 2, {}) then
        return nil, protocol.failure("ssh_request_invalid")
    end

    for key in pairs(parameters) do
        if type(key) ~= "string" then
            return nil, protocol.failure("ssh_request_invalid")
        end
    end

    local accepted, body = pcall(json.encode, { method = method, parameters = json.object(parameters) })

    if not accepted or not boundedJson(body) then
        return nil, protocol.failure("ssh_request_invalid")
    end

    return body
end

function protocol.decode(body)
    if type(body) ~= "string" or not boundedJson(body) then
        return nil, protocol.failure("ssh_response_invalid")
    end

    local accepted, value = pcall(json.decode, body, { nullValue = json.null })

    if not accepted then
        return nil, protocol.failure("ssh_response_invalid")
    end

    return value
end

return protocol
