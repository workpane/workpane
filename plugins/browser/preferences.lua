-- The preferences of the browser, whose homepage opens in every new tab.
local address = include("address")

local preferences = {}

local store

-- The homepage keeps what the reader wrote as long as the browser can read it as an address, and answers it normalized.
function preferences.define()
    store = workpane.preferences.define({ homepage = { type = "string", default = "about:blank", check = function(value)
        return address.normalize(value) ~= nil
    end } })
end

function preferences.homepage()
    return address.normalize(store:get("homepage"))
end

function preferences.control(key, properties)
    return store:control(key, properties)
end

return preferences
