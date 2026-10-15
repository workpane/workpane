-- The prompt templates an agent starts from, each composed of the sections every agent shares and the sections of its platform, read from the assets and checked whole before anything runs.
local codec = include("codec")
local fs = require("fs")

local templates = {}

local identifiers = {}
local bodies = {}

local function invalid(message, detail)
    error({ code = "ai_template_invalid", message = message, detail = detail or "" }, 0)
end

local function read(name)
    local bytes, failure = fs.readFile(workpane.plugin.directory .. "/assets/templates/" .. name):await()

    if failure ~= nil or type(bytes) ~= "string" then
        invalid("A template file of the AI plugin is unavailable", name)
    end

    return bytes
end

-- A template names its sections in order, each a Markdown file of the templates folder, and its body is those sections joined by a blank line.
function templates.load()
    local document = codec.read(read("catalog.json"))

    if type(document) ~= "table" or type(document.templates) ~= "table" or #document.templates == 0 then
        invalid("The template catalog of the AI plugin declares no template", "catalog.json")
    end

    local sections = {}
    identifiers = {}
    bodies = {}

    for _, entry in ipairs(document.templates) do
        local id = type(entry) == "table" and entry.id or nil

        if type(id) ~= "string" or id:match("^[a-z0-9][a-z0-9-]*$") == nil or id:sub(-1) == "-" or bodies[id] ~= nil or type(entry.sections) ~= "table" or #entry.sections == 0 then
            invalid("A template of the AI plugin is declared wrongly", tostring(id))
        end

        local parts = {}

        for index, name in ipairs(entry.sections) do
            if type(name) ~= "string" or name:match("^[a-z0-9][a-z0-9-]*$") == nil then
                invalid("A template of the AI plugin names a section wrongly", id)
            end

            sections[name] = sections[name] or read(name .. ".md"):match("^%s*(.-)%s*$")
            parts[index] = sections[name]
        end

        identifiers[#identifiers + 1] = id
        bodies[id] = table.concat(parts, "\n\n")
    end
end

function templates.list()
    return identifiers
end

function templates.body(id)
    return bodies[id]
end

return templates
