-- Reads a file the reader chose into the part of a message that carries it, by the kind its name says, so an agent receives it the way its model reads it.
local fs = require("fs")
local messages = include("messages")

local attachments = {}

local image = function(mediaType)
    return { type = "image", mediaType = mediaType }
end

local document = function(mediaType)
    return { type = "document", mediaType = mediaType }
end

local kinds = {
    png = image("image/png"),
    jpg = image("image/jpeg"),
    jpeg = image("image/jpeg"),
    gif = image("image/gif"),
    webp = image("image/webp"),
    pdf = document("application/pdf"),
    md = document("text/markdown"),
    markdown = document("text/markdown"),
    csv = document("text/csv"),
    json = document("application/json"),
    xml = document("application/xml"),
    html = document("text/html"),
    htm = document("text/html"),
    wav = { type = "audio", format = "wav" },
    mp3 = { type = "audio", format = "mp3" },
}

local plainExtensions = { "txt", "log", "yaml", "yml", "toml", "ini", "cfg", "conf", "lua", "py", "js", "jsx", "ts", "tsx", "c", "h", "cc", "cpp", "hpp", "cs", "java", "kt", "go", "rs", "rb", "php", "swift", "sh", "sql", "css", "scss" }

for _, extension in ipairs(plainExtensions) do
    kinds[extension] = document("text/plain")
end

-- Answers the name patterns of every file an agent reads, for the filter of the file picker.
function attachments.patterns()
    local patterns = {}

    for extension in pairs(kinds) do
        patterns[#patterns + 1] = "*." .. extension
    end

    table.sort(patterns)

    return patterns
end

-- Answers the part a file becomes and its name, or nil, the key that says why it cannot and its name.
function attachments.read(path)
    local name = path:match("[^/\\]+$") or path
    local kind = kinds[(name:match("%.([^.]+)$") or ""):lower()]

    if kind == nil then
        return nil, "ai.error.attachment-unsupported", name
    end

    local info = fs.stat(path):await()

    if info == nil or not info.isFile then
        return nil, "ai.error.attachment-unreadable", name
    end

    if info.size > messages.largestBytes(kind.type) then
        return nil, "ai.error.attachment-too-large", name
    end

    local data = fs.readFile(path):await()

    if data == nil or data == "" or (kind.type == "document" and kind.mediaType ~= "application/pdf" and utf8.len(data) == nil) then
        return nil, "ai.error.attachment-unreadable", name
    end

    return { type = kind.type, mediaType = kind.mediaType, format = kind.format, name = kind.type == "document" and name or nil, data = data }, nil, name
end

return attachments
