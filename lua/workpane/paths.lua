-- Reads and writes the paths of files the way the platform the product runs on writes them, as the host checks them again.
local paths = {}

local function encoded(text)
    return (text:gsub("[^%w%-%._~/:]", function(character)
        return string.format("%%%02X", character:byte())
    end))
end

local function decoded(text)
    return (text:gsub("%%(%x%x)", function(hex)
        return string.char(tonumber(hex, 16))
    end))
end

-- A path is absolute from the root on macOS and Linux, and from a drive or a network share on Windows.
function paths.absolute(platform, path)
    if type(path) ~= "string" then
        return false
    end

    if platform == "windows" then
        return path:match("^%a:[/\\]") ~= nil or path:match("^[/\\][/\\][^/\\]") ~= nil
    end

    return path:sub(1, 1) == "/"
end

-- The address of a file encodes every byte outside the unreserved set and the separators, a Windows drive gains the slash an address needs and a network share names its server as the host.
function paths.uri(path)
    local normalized = path:gsub("\\", "/")

    if normalized:sub(1, 2) == "//" then
        return "file:" .. encoded(normalized)
    end

    if normalized:match("^%a:") then
        return "file:///" .. encoded(normalized)
    end

    return "file://" .. encoded(normalized)
end

-- The path a file address names, or nil for another kind of address, where a Windows drive loses the slash before it and a server other than this machine is a network share on Windows.
function paths.path(platform, uri)
    if type(uri) ~= "string" then
        return nil
    end

    local host, rest = uri:match("^file://([^/]*)(.*)$")

    if host == nil then
        return nil
    end

    local path = decoded(rest)

    if platform == "windows" and host ~= "" and host ~= "localhost" then
        return "//" .. decoded(host) .. path
    end

    if path:match("^/%a:") then
        return path:sub(2)
    end

    return path
end

return paths
