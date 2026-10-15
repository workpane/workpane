local crypto = require("crypto")
local http = require("http")
local paths = require("workpane.paths")
local platform = require("platform").os()

local images = {}

local maximumBytes = 4 * 1024 * 1024
local maximumAddressBytes = maximumBytes * 4 // 3 + 128

local function refuse()
    error({ code = "ui_markdown_image_unreadable", message = "The Markdown image could not be loaded", detail = "" }, 0)
end

local function mediaType(bytes)
    if bytes:sub(1, 8) == "\137PNG\13\10\26\10" then
        return "image/png"
    end

    if bytes:sub(1, 3) == "\255\216\255" then
        return "image/jpeg"
    end

    if bytes:sub(1, 6) == "GIF87a" or bytes:sub(1, 6) == "GIF89a" then
        return "image/gif"
    end

    return nil
end

function images.load(event, backend)
    if type(event) ~= "table" or math.type(event.request) ~= "integer" or event.request < 1 or event.basePath ~= nil and (type(event.basePath) ~= "string" or #event.basePath > 4096 or event.basePath:find("%z") ~= nil or utf8.len(event.basePath) == nil) then
        refuse()
    end

    if backend ~= nil and (type(backend) ~= "table" or type(backend.readBounded) ~= "function" or backend.platform ~= "macos" and backend.platform ~= "linux" and backend.platform ~= "windows") then
        refuse()
    end

    local source = event.source

    if type(source) ~= "string" or source == "" or source:find("\0", 1, true) ~= nil or #source > maximumAddressBytes or utf8.len(source) == nil then
        refuse()
    end

    local reply = { source = source, request = event.request }

    if source:match("^data:image/") then
        reply.data = source
        return reply
    end

    if source:match("^https?://") then
        local response, failure = http.client.requestRaw({ url = source, method = "GET", timeoutSeconds = 15, maxResponseBytes = maximumBytes, maxRedirects = 5 }):await()

        if failure ~= nil or response.status < 200 or response.status >= 300 then
            refuse()
        end

        local kind = mediaType(response.body)

        if kind == nil then
            refuse()
        end

        reply.data = "data:" .. kind .. ";base64," .. crypto.base64Encode(response.body)
        return reply
    end

    local system = backend ~= nil and backend.platform or platform
    local path = paths.path(system, source)

    if path == nil then
        if source:match("^%a[%w+.-]*:") and not paths.absolute(system, source) then
            refuse()
        end

        path = source:gsub("%%(%x%x)", function(hex)
            return string.char(tonumber(hex, 16))
        end)
    end

    if path:find("\0", 1, true) ~= nil then
        refuse()
    end

    if not paths.absolute(system, path) then
        if not paths.absolute(system, event.basePath) then
            refuse()
        end

        path = event.basePath .. "/" .. path
    end

    if backend ~= nil then
        local read, problem = backend.readBounded(path, maximumBytes):await()
        local kind = problem == nil and type(read) == "table" and type(read.bytes) == "string" and #read.bytes <= maximumBytes and read.complete == true and mediaType(read.bytes) or nil

        if kind == nil then
            refuse()
        end

        reply.data = "data:" .. kind .. ";base64," .. crypto.base64Encode(read.bytes)
        return reply
    end

    reply.file = path

    return reply
end

return images
