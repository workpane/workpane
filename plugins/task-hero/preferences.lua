-- The preferences of the adventure: the height of its band, its speed, whether its counters show, whether its light follows the clock and the progress of the knight with the kind of warrior he is.
local preferences = {}

local store

local heights = { hidden = 0, small = 64, medium = 88, large = 120 }
local speeds = { slow = 0.6, normal = 1, fast = 1.5, faster = 2 }

function preferences.define()
    store = workpane.preferences.define({
        size = { type = "string", default = "medium", choices = { "hidden", "small", "medium", "large" } },
        speed = { type = "string", default = "normal", choices = { "slow", "normal", "fast", "faster" } },
        counters = { type = "boolean", default = true },
        dayNight = { type = "boolean", default = false },
        progress = { type = "record", fields = {
            gold = { type = "integer", default = 0, minimum = 0 },
            level = { type = "integer", default = 1, minimum = 1 },
            experience = { type = "integer", default = 0, minimum = 0 },
            victories = { type = "integer", default = 0, minimum = 0 },
            class = { type = "string", default = "warrior", choices = { "warrior", "lancer", "archer" } },
        } },
    })
end

function preferences.get(key)
    return store:get(key)
end

function preferences.height()
    return heights[store:get("size")]
end

function preferences.speed()
    return speeds[store:get("speed")]
end

function preferences.keep(progress)
    return store:set("progress", progress)
end

function preferences.forget()
    return store:reset("progress")
end

function preferences.watch(key, handler)
    return store:watch(key, handler)
end

function preferences.control(key, properties)
    return store:control(key, properties)
end

return preferences
