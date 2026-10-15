-- The preferences of the web server, which keep where the reader divides the servers from their requests.
local preferences = {}

local store

function preferences.define()
    store = workpane.preferences.define({ splitRatio = { type = "integer", minimum = 150, maximum = 850, default = 420 } })
end

function preferences.splitRatio()
    return store:get("splitRatio")
end

function preferences.setSplitRatio(ratio)
    return store:set("splitRatio", ratio)
end

function preferences.watchSplitRatio(handler)
    return store:watch("splitRatio", handler)
end

function preferences.control(key, properties)
    return store:control(key, properties)
end

return preferences
