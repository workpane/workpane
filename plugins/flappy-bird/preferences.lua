-- The preferences of the game: the best score, the color of the bird, the time of day of the background and the sound.
local preferences = {}

local store

function preferences.define()
    store = workpane.preferences.define({
        best = { type = "integer", default = 0, minimum = 0 },
        bird = { type = "string", default = "yellow", choices = { "yellow", "blue", "red", "random" } },
        background = { type = "string", default = "clock", choices = { "day", "night", "clock" } },
        sound = { type = "boolean", default = true },
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
