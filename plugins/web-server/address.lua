-- Tells a numeric IP address from anything else and writes the address a browser opens for a server.
local address = {}

local function ipv4(host)
    local parts = { host:match("^(%d+)%.(%d+)%.(%d+)%.(%d+)$") }

    if #parts ~= 4 then
        return false
    end

    for _, part in ipairs(parts) do
        if #part > 3 or tonumber(part) > 255 or (#part > 1 and part:sub(1, 1) == "0") then
            return false
        end
    end

    return true
end

-- Counts the groups of an IPv6 part written between single colons, and answers nil when one of them is not one to four hexadecimal digits.
local function groups(part)
    if part == "" then
        return 0
    end

    local count = 0

    for group in (part .. ":"):gmatch("(.-):") do
        if group:match("^%x%x?%x?%x?$") == nil then
            return nil
        end

        count = count + 1
    end

    return count
end

-- An IPv4 address closing an IPv6 address stands for its last two groups, and a double colon stands for at least one group of zeros.
local function ipv6(host)
    local body = host
    local embedded = host:match(":(%d+%.%d+%.%d+%.%d+)$")

    if embedded ~= nil then
        if not ipv4(embedded) then
            return false
        end

        body = host:sub(1, #host - #embedded) .. "0:0"
    end

    local left, right = body:match("^(.-)::(.*)$")

    if left == nil then
        return groups(body) == 8
    end

    if right:find("::", 1, true) ~= nil then
        return false
    end

    local before, after = groups(left), groups(right)
    return before ~= nil and after ~= nil and before + after <= 7
end

function address.numeric(host)
    if type(host) ~= "string" then
        return false
    end

    return ipv4(host) or (host:find(":", 1, true) ~= nil and ipv6(host))
end

-- An IPv6 host is written in brackets, which is how an address tells its host from its port.
function address.url(host, port)
    local written = host:find(":", 1, true) ~= nil and ("[" .. host .. "]") or host
    return "http://" .. written .. ":" .. tostring(port) .. "/"
end

return address
