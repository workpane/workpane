local crypto = require("crypto")
local json = require("json")
local profiles = include("profiles")

local credentials = {}

function credentials.name(profile)
    local endpoint = json.encode({ profile.id, profile.host, profile.user, profile.port, profile.identity, profile.configuration })
    return "password:" .. crypto.digest("SHA256", endpoint)
end

function credentials.read(profile)
    return workpane.credentials.read(credentials.name(profile)):await()
end

local function write(name, value)
    if value ~= nil then
        return workpane.credentials.write(name, value):await()
    end

    return workpane.credentials.erase(name):await()
end

local function restore(changes, last)
    local failure

    for index = last, 1, -1 do
        local _, problem = write(changes[index].name, changes[index].before)

        if problem ~= nil then
            failure = { code = "ssh_credential_cleanup_failed", message = "The previous SSH credential could not be restored", detail = "" }
        end
    end

    return failure
end

function credentials.save(profile, password, previous)
    local accepted, invalid = profiles.validate(profile)

    if accepted == nil then
        return nil, { code = invalid, message = "The SSH connection configuration is invalid", detail = "" }
    end

    local name = credentials.name(accepted)
    local previousName = previous ~= nil and previous.savePassword and credentials.name(previous) or nil
    local unchanged = accepted.savePassword and previousName == name and password == ""

    if unchanged or not accepted.savePassword and previousName == nil then
        return include("preferences").save(accepted)
    end

    local old
    local unavailable

    if accepted.savePassword or previousName == name then
        old, unavailable = workpane.credentials.read(name):await()

        if unavailable ~= nil then
            return nil, unavailable
        end
    end

    if accepted.savePassword and password == "" and old == nil then
        return nil, { code = "ssh_password_required", message = "Enter a password to save with this connection", detail = "" }
    end

    local changes = {}
    local replacement = accepted.savePassword and (password ~= "" and password or old) or nil

    if replacement ~= old then
        changes[#changes + 1] = { name = name, before = old, after = replacement }
    end

    if previousName ~= nil and previousName ~= name then
        local preceding, unavailable = workpane.credentials.read(previousName):await()

        if unavailable ~= nil then
            return nil, unavailable
        end

        if preceding ~= nil then
            changes[#changes + 1] = { name = previousName, before = preceding }
        end
    end

    for index, change in ipairs(changes) do
        local _, problem = write(change.name, change.after)

        if problem ~= nil then
            return nil, restore(changes, index - 1) or problem
        end
    end

    local saved, problem = include("preferences").save(accepted)

    if not saved then
        return nil, restore(changes, #changes) or problem
    end

    return true
end

function credentials.remove(profile, commit)
    if profile == nil or not profile.savePassword then
        return commit()
    end

    local name = credentials.name(profile)
    local preceding, unavailable = workpane.credentials.read(name):await()

    if unavailable ~= nil then
        return nil, unavailable
    end

    local changes = {}

    if preceding ~= nil then
        local _, problem = write(name, nil)

        if problem ~= nil then
            return nil, problem
        end

        changes[1] = { name = name, before = preceding }
    end

    local saved, problem = commit()

    if not saved then
        return nil, restore(changes, #changes) or problem
    end

    return true
end

return credentials
