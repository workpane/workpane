local profiles = {}

profiles.maximumProfiles = 64

local fields = { id = true, name = true, host = true, user = true, port = true, identity = true, configuration = true, root = true, timeoutSeconds = true, savePassword = true }

local function text(value, maximum, empty)
    return type(value) == "string" and #value <= maximum and (empty or value ~= "") and value:find("[%z\1-\31\127]") == nil and utf8.len(value) ~= nil
end

function profiles.absolute(platform, path)
    if not text(path, 4096, false) then
        return false
    end

    if platform == "windows" then
        return path:match("^[A-Za-z]:[/\\]") ~= nil or path:match("^[/\\][/\\][^/\\]+[/\\][^/\\]+") ~= nil
    end

    return (platform == "macos" or platform == "linux") and path:sub(1, 1) == "/"
end

function profiles.validate(profile)
    if type(profile) ~= "table" then
        return nil, "ssh_profile_invalid"
    end

    for key in pairs(profile) do
        if not fields[key] then
            return nil, "ssh_profile_invalid"
        end
    end

    if type(profile.savePassword) ~= "boolean" then
        return nil, "ssh_profile_invalid"
    end

    if not text(profile.id, 64, false) or profile.id:match("^[a-z0-9][a-z0-9-]*$") == nil then
        return nil, "ssh_profile_invalid"
    end

    if not text(profile.name, 128, false) or profile.name:match("^%s*$") ~= nil then
        return nil, "ssh_profile_name_invalid"
    end

    if not text(profile.host, 255, false) or profile.host:sub(1, 1) == "-" or profile.host:find("[%s/@\\]") ~= nil then
        return nil, "ssh_profile_host_invalid"
    end

    if not text(profile.user, 128, true) or (profile.user ~= "" and profile.user:match("^%s*$") ~= nil) or profile.user:sub(1, 1) == "-" or profile.user:find("[/:]") ~= nil then
        return nil, "ssh_profile_user_invalid"
    end

    if math.type(profile.port) ~= "integer" or profile.port < 0 or profile.port > 65535 then
        return nil, "ssh_profile_port_invalid"
    end

    if not text(profile.identity, 4096, true) or not text(profile.configuration, 4096, true) then
        return nil, "ssh_profile_path_invalid"
    end

    if (profile.identity ~= "" and not workpane.files.absolute(profile.identity)) or (profile.configuration ~= "" and not workpane.files.absolute(profile.configuration)) then
        return nil, "ssh_profile_path_invalid"
    end

    if not text(profile.root, 4096, true) then
        return nil, "ssh_profile_root_invalid"
    end

    local windowsPath = profile.root:match("^[A-Za-z]:") ~= nil or profile.root:sub(1, 2) == "\\\\"
    local root = workpane.files.paths(windowsPath and "windows" or "linux").normalizeRoot(profile.root)

    if root ~= "" and not profiles.absolute("windows", root) and not profiles.absolute("linux", root) then
        return nil, "ssh_profile_root_invalid"
    end

    if math.type(profile.timeoutSeconds) ~= "integer" or profile.timeoutSeconds < 5 or profile.timeoutSeconds > 300 then
        return nil, "ssh_profile_timeout_invalid"
    end

    local result = {}

    for key in pairs(fields) do
        result[key] = profile[key]
    end

    result.root = root

    return result
end

function profiles.validList(list)
    if #list > profiles.maximumProfiles then
        return false
    end

    local ids = {}

    for _, profile in ipairs(list) do
        if profiles.validate(profile) == nil or ids[profile.id] then
            return false
        end

        ids[profile.id] = true
    end

    return true
end

function profiles.arguments(profile)
    local accepted, failure = profiles.validate(profile)

    if accepted == nil then
        return nil, failure
    end

    local arguments = { "-T", "-o", "StrictHostKeyChecking=ask", "-o", "ExitOnForwardFailure=yes", "-o", "ForkAfterAuthentication=no", "-o", "ControlMaster=no", "-o", "ControlPersist=no", "-o", "ControlPath=none", "-o", "ConnectTimeout=" .. tostring(profile.timeoutSeconds), "-o", "ServerAliveInterval=15", "-o", "ServerAliveCountMax=3" }

    for _, option in ipairs({ { "-F", profile.configuration }, { "-i", profile.identity }, { "-l", profile.user }, { "-p", profile.port ~= 0 and tostring(profile.port) or "" } }) do
        if option[2] ~= "" then
            arguments[#arguments + 1] = option[1]
            arguments[#arguments + 1] = option[2]
        end
    end

    return arguments
end

return profiles
