-- The preferences of the editor, which reach every open document the moment they change and are saved in the background.
local catalog = include("catalog")
local encoding = include("encoding")

local preferences = {}

local store

function preferences.define()
    store = workpane.preferences.define({
        fontSize = { type = "integer", default = 11, minimum = 8, maximum = 36 },
        fontFamily = { type = "string", default = "JetBrains Mono", maxLength = 256, check = function(value)
            return value ~= ""
        end },
        colorScheme = { type = "string", default = "dark-modern", check = function(value)
            return catalog.scheme(value) ~= nil
        end },
        wordWrap = { type = "boolean", default = false },
        defaultCharset = { type = "string", default = "latin1", check = encoding.known },
        languageServers = { type = "boolean", default = true },
    })
end

function preferences.get(key)
    return store:get(key)
end

function preferences.set(key, value)
    return store:set(key, value)
end

function preferences.watch(handler)
    return store:watch(handler)
end

function preferences.control(key, properties)
    return store:control(key, properties)
end

return preferences
