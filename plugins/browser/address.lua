-- Turns what the reader typed into an address the browser accepts, giving HTTPS to an address written without a scheme.
local address = {}

local schemes = { about = true, file = true, http = true, https = true }

-- The host of an address is what its authority names after any user and before any port, and an IPv6 host keeps its brackets.
local function host(rest)
    local authority = rest:match("^//([^/?#]*)")

    if authority == nil then
        return ""
    end

    local server = authority:match("^.*@(.*)$") or authority
    return server:match("^%[[%x:%.]+%]") or server:match("^([^:]*)")
end

-- Answers the address in its one written form, or nil for an address the browser refuses.
function address.normalize(typed)
    if type(typed) ~= "string" then
        return nil
    end

    local written = typed:match("^%s*(.-)%s*$")

    if written == "" or written:find("[%s%c]") ~= nil then
        return nil
    end

    if written:find("://", 1, true) == nil and written:lower():match("^about:") == nil then
        written = "https://" .. written
    end

    local scheme, rest = written:match("^(%a[%w+.-]*):(.+)$")

    if scheme == nil or not schemes[scheme:lower()] then
        return nil
    end

    scheme = scheme:lower()

    if (scheme == "http" or scheme == "https") and host(rest) == "" then
        return nil
    end

    if scheme == "file" and rest:match("^//") == nil then
        return nil
    end

    return scheme .. ":" .. rest
end

-- The name a page offers when it has no title of its own, which is its host or its whole address.
function address.name(url)
    return url:match("^%a[%w+.-]*://([^/?#:]+)") or url
end

return address
