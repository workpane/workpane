local profiles = include("profiles")

local preferences = {}
preferences.defaultTimeoutSeconds = 60

local store

function preferences.define()
    store = workpane.preferences.define({
        profiles = { type = "list", maxItems = profiles.maximumProfiles, check = profiles.validList, items = { type = "record", fields = {
            id = { type = "string", default = "", maxLength = 64 },
            name = { type = "string", default = "", maxLength = 128 },
            host = { type = "string", default = "", maxLength = 255 },
            user = { type = "string", default = "", maxLength = 128 },
            port = { type = "integer", default = 0, minimum = 0, maximum = 65535 },
            identity = { type = "string", default = "", maxLength = 4096 },
            configuration = { type = "string", default = "", maxLength = 4096 },
            root = { type = "string", default = "", maxLength = 4096 },
            timeoutSeconds = { type = "integer", default = preferences.defaultTimeoutSeconds, minimum = 5, maximum = 300 },
            savePassword = { type = "boolean", default = false },
        } } },
    })
end

function preferences.list()
    return store:get("profiles")
end

function preferences.find(id)
    for _, profile in ipairs(preferences.list()) do
        if profile.id == id then
            return profile
        end
    end
end

function preferences.save(profile)
    local accepted, failure = profiles.validate(profile)

    if accepted == nil then
        return nil, { code = failure, message = "The SSH connection configuration is invalid", detail = "" }
    end

    local list = preferences.list()
    local position = #list + 1

    for index, existing in ipairs(list) do
        if existing.id == profile.id then
            position = index
            break
        end
    end

    if position > profiles.maximumProfiles then
        return nil, { code = "ssh_profile_limit", message = "The saved SSH connection limit has been reached", detail = "" }
    end

    list[position] = accepted
    local _, problem = store:set("profiles", list):await()

    if problem ~= nil then
        return nil, problem
    end

    return true
end

function preferences.remove(id)
    local profile = preferences.find(id)

    local function commit()
        local remaining = {}

        for _, profile in ipairs(preferences.list()) do
            if profile.id ~= id then
                remaining[#remaining + 1] = profile
            end
        end

        local _, failure = store:set("profiles", remaining):await()
        return failure == nil, failure
    end

    return include("credentials").remove(profile, commit)
end

function preferences.watch(handler)
    return store:watch("profiles", handler)
end

return preferences
