local crypto = require("crypto")
local fs = require("fs")
local json = require("json")
local protocol = include("protocol")

local source = {}

local function text(value)
    return type(value) == "string" and value:find("%z") == nil and utf8.len(value) ~= nil
end

function source.quote(value)
    if not text(value) then
        error(protocol.failure("ssh_launch_invalid"), 0)
    end

    return "'" .. value:gsub("'", "'\\''") .. "'"
end

function source.asset(name)
    if type(name) ~= "string" or name:match("^ssh%-[a-z-]+%.[a-z0-9]+$") == nil then
        error(protocol.failure("ssh_launch_invalid"), 0)
    end

    local value, problem = fs.readFile(workpane.plugin.directory .. "/assets/" .. name):await()

    if problem ~= nil or not text(value) or #value > 49152 then
        error(protocol.failure("ssh_source_unavailable"), 0)
    end

    return value
end

function source.prepare(platform, script, arguments)
    if not text(script) or #script > 49152 or type(arguments) ~= "table" or #arguments > 256 then
        error(protocol.failure("ssh_launch_invalid"), 0)
    end

    local count = 0
    local bytes = 0

    for key, value in pairs(arguments) do
        if math.type(key) ~= "integer" or key < 1 or key > #arguments or not text(value) then
            error(protocol.failure("ssh_launch_invalid"), 0)
        end

        count = count + 1
        bytes = bytes + #value
    end

    if count ~= #arguments or bytes > 65536 then
        error(protocol.failure("ssh_launch_invalid"), 0)
    end

    if platform == "windows" then
        local declaration = crypto.base64Encode(json.encode(json.array(arguments)))
        local content = "[string[]]$sshArguments = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('" .. declaration .. "')) | ConvertFrom-Json\n" .. script
        local loader = source.asset("ssh-loader-windows.ps1")

        if #content > 49152 or loader:find("[\128-\255]") ~= nil then
            error(protocol.failure("ssh_launch_invalid"), 0)
        end

        local encoded = loader:gsub(".", function(byte)
            return byte .. "\0"
        end)

        local command = "powershell.exe -NoLogo -NoProfile -NonInteractive -EncodedCommand " .. crypto.base64Encode(encoded)

        if #command > 8191 then
            error(protocol.failure("ssh_launch_invalid"), 0)
        end

        return { command = command, input = crypto.base64Encode(content) .. "\n" }
    end

    if platform ~= "macos" and platform ~= "linux" then
        error(protocol.failure("ssh_launch_invalid"), 0)
    end

    local command = { "exec /bin/sh -c", source.quote(script), "workpane" }

    for _, value in ipairs(arguments) do
        command[#command + 1] = source.quote(value)
    end

    local joined = table.concat(command, " ")

    if #joined > 65536 then
        error(protocol.failure("ssh_launch_invalid"), 0)
    end

    return { command = joined, input = "" }
end

return source
