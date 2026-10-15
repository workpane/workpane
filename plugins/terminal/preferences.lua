-- The preferences of the plugin, which reach every terminal the moment they change and are saved in the background.
local preferences = {}

local store

function preferences.define()
    store = workpane.preferences.define({
        fontSize = { type = "integer", default = 11, minimum = 8, maximum = 36 },
        fontFamily = { type = "string", default = "JetBrains Mono", maxLength = 256, check = function(value)
            return value ~= ""
        end },
        palette = { type = "string", default = "balanced", choices = { "vivid", "balanced", "soft" } },
        cursorBlink = { type = "boolean", default = false },
        blinkSpeed = { type = "string", default = "normal", choices = { "slow", "normal", "fast" } },
        clipboardWrites = { type = "boolean", default = false },
        openLinksExternally = { type = "boolean", default = false },
    })
end

function preferences.get(key)
    return store:get(key)
end

function preferences.set(key, value)
    return store:set(key, value)
end

function preferences.watch(key, handler)
    return store:watch(key, handler)
end

function preferences.control(key, properties)
    return store:control(key, properties)
end

return preferences
