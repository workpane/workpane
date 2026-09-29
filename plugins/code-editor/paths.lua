-- The paths the host answers use forward slashes and a root keeps its separator, so joining and comparing them never doubles the separator of a root.
local paths = {}

local function withSeparator(folder)
    return folder:sub(-1) == "/" and folder or folder .. "/"
end

function paths.join(folder, name)
    return withSeparator(folder) .. name
end

-- Answers whether a path is the folder itself or lies inside it.
function paths.inside(folder, path)
    local prefix = withSeparator(folder)
    return path == folder or path:sub(1, #prefix) == prefix
end

-- The folder holding a path, which for an entry of a root is the root with its separator, and nothing for a root.
function paths.parent(path)
    local folder = path:match("^(.*)/[^/]+/?$")

    if folder == nil then
        return nil
    end

    if folder == "" or folder:match("^%a:$") then
        return folder .. "/"
    end

    return folder
end

-- The path of an entry relative to a folder that holds it.
function paths.relative(folder, path)
    return path:sub(#withSeparator(folder) + 1)
end

return paths
