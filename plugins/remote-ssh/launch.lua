local crypto = require("crypto")
local json = require("json")
local protocol = include("protocol")
local remoteProgram = include("remote-program")
local sftpFiles = include("sftp-files")
local source = include("source")

local launch = {}

local Launcher = {}
Launcher.__index = Launcher

local function failed(code)
    error(protocol.failure(code), 0)
end

local function text(value, maximum)
    return type(value) == "string" and #value <= maximum and value:find("%z") == nil and utf8.len(value) ~= nil
end

local function list(values, maximum)
    if type(values) ~= "table" or #values > maximum then
        failed("remote_process_invalid")
    end

    local count = 0

    for key, value in pairs(values) do
        if math.type(key) ~= "integer" or key < 1 or key > #values or not text(value, 65536) then
            failed("remote_process_invalid")
        end

        count = count + 1
    end

    if count ~= #values then
        failed("remote_process_invalid")
    end
end

local function validate(declaration, platform)
    if type(declaration) ~= "table" then
        failed("remote_process_invalid")
    end

    local fields = { program = true, arguments = true, directory = true, variables = true, cleared = true, input = true, terminal = true }

    for key in pairs(declaration) do
        if not fields[key] then
            failed("remote_process_invalid")
        end
    end

    local paths = workpane.files.paths(platform)

    if not text(declaration.program, 4096) or not paths.absolute(declaration.program) or not text(declaration.directory, 4096) or not paths.absolute(declaration.directory) then
        failed("remote_process_invalid")
    end

    local extension = declaration.program:match("%.([^./\\]+)$")
    local batch = platform == "windows" and extension ~= nil and (extension:lower() == "cmd" or extension:lower() == "bat")

    if batch and declaration.program:find('[%%!\r\n]') ~= nil then
        failed("process_argument_unsafe")
    end

    list(declaration.arguments, 256)
    list(declaration.cleared, 256)

    if type(declaration.variables) ~= "table" or declaration.input ~= nil and (type(declaration.input) ~= "string" or #declaration.input > 4194304) then
        failed("remote_process_invalid")
    end

    local count = 0
    local bytes = #declaration.program + #declaration.directory

    for _, argument in ipairs(declaration.arguments) do
        if batch and argument:find('["%%!%^&|<>()\r\n]') ~= nil then
            failed("process_argument_unsafe")
        end

        bytes = bytes + #argument
    end

    for key, value in pairs(declaration.variables) do
        if not text(key, 256) or key == "" or key:find("=", 1, true) ~= nil or not text(value, 65536) then
            failed("remote_process_invalid")
        end

        count = count + 1
        bytes = bytes + #key + #value
    end

    for _, key in ipairs(declaration.cleared) do
        if key == "" or #key > 256 or key:find("=", 1, true) ~= nil then
            failed("remote_process_invalid")
        end

        bytes = bytes + #key
    end

    if count > 256 or bytes > 1048576 then
        failed("remote_process_invalid")
    end

    if declaration.terminal ~= nil then
        local terminal = declaration.terminal

        if type(terminal) ~= "table" or declaration.input ~= nil or math.type(terminal.columns) ~= "integer" or math.type(terminal.rows) ~= "integer" or terminal.columns < 1 or terminal.columns > 10000 or terminal.rows < 1 or terminal.rows > 10000 then
            failed("remote_process_invalid")
        end

        for key in pairs(terminal) do
            if key ~= "columns" and key ~= "rows" then
                failed("remote_process_invalid")
            end
        end
    end
end

function launch.new(owner, client, executable, arguments, host, authentication)
    local resolve

    if owner.platform == "windows" then
        resolve = function(root, path, missing)
            return owner:resolve(root, path, missing, 10000)
        end
    end

    return setmetatable({ owner = owner, executable = executable, arguments = arguments, host = host, authentication = authentication, files = sftpFiles.new(client, owner, owner.path, owner.platform, resolve) }, Launcher)
end

function Launcher:prepare(identity, declaration)
    local platform = self.owner.platform
    local kind = declaration.terminal ~= nil and "terminal" or "pipe"

    if platform ~= "windows" then
        local command = { "cd", source.quote(declaration.directory), "&& exec /usr/bin/env" }

        for _, key in ipairs(declaration.cleared) do
            command[#command + 1] = "-u"
            command[#command + 1] = source.quote(key)
        end

        command[#command + 1] = "--"

        for key, value in pairs(declaration.variables) do
            command[#command + 1] = source.quote(key .. "=" .. value)
        end

        command[#command + 1] = source.quote(declaration.program)

        for _, argument in ipairs(declaration.arguments) do
            command[#command + 1] = source.quote(argument)
        end

        return source.prepare(platform, source.asset("ssh-program-posix.sh"), { self.owner.path, tostring(identity), kind, "/bin/sh", "-c", table.concat(command, " ") })
    end

    local directory = self.owner.path .. "/program." .. tostring(identity)
    local parameters = { program = declaration.program, arguments = json.array(declaration.arguments), directory = declaration.directory, variables = json.object(declaration.variables), cleared = json.array(declaration.cleared), terminal = declaration.terminal ~= nil, record = directory }
    local encoded = crypto.base64Encode(json.encode(parameters))
    local script = source.asset("ssh-jobs-windows.ps1") .. source.asset("ssh-paths-windows.ps1") .. source.asset("ssh-programs-windows.ps1") .. "\n$launch = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('" .. encoded .. "')) | ConvertFrom-Json\n" .. [=[
[Workpane.Ssh.Jobs]::Join([IO.File]::ReadAllText([IO.Path]::Combine($launch.record, 'job')))
[Workpane.Ssh.Paths]::CreatePrivate([IO.Path]::Combine($launch.record, 'claimed'))
$variables = @{}

foreach ($property in $launch.variables.PSObject.Properties) {
    $variables.Add($property.Name, [string]$property.Value)
}

$inputStream = $null
$outputStream = $null
$errorStream = $null
$inputHandle = [IntPtr]::Zero

if (-not $launch.terminal) {
    $inputStream = $sshInput
    $outputStream = $sshOutput
    $errorStream = $sshError
    $inputHandle = $sshInputFile.SafeFileHandle.DangerousGetHandle()
}

try {
    $code = [Workpane.Ssh.Programs]::Run($launch.program, [string[]]$launch.arguments, $launch.directory, $variables, [string[]]$launch.cleared, $launch.terminal, $inputStream, $outputStream, $errorStream, $launch.record, $inputHandle)
    exit $code
}

catch {
    [Workpane.Ssh.Programs]::Fail($launch.record)
    throw
}
]=]
    local filename = directory .. "/launch.ps1"
    self.files:upload(filename, script, 384)

    if declaration.terminal == nil then
        return source.prepare(platform, "& ([ScriptBlock]::Create([IO.File]::ReadAllText($sshArguments[0])))", { filename })
    end

    local command = "& ([ScriptBlock]::Create([IO.File]::ReadAllText([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('" .. crypto.base64Encode(filename) .. "')))))"
    local encodedCommand = command:gsub(".", function(byte)
        return byte .. "\0"
    end)

    return { command = "powershell.exe -NoLogo -NoProfile -NonInteractive -EncodedCommand " .. crypto.base64Encode(encodedCommand), input = "" }
end

function Launcher:start(declaration, reserved, cancelled)
    validate(declaration, self.owner.platform)
    local identity = self.owner:open(10000)
    local channel
    local running
    local accepted, problem = pcall(function()
        if reserved ~= nil then
            reserved(identity)
        end

        validate(declaration, self.owner.platform)
        local target = self:prepare(identity, declaration)

        if cancelled ~= nil and cancelled() then
            failed("remote_process_group_closed")
        end

        local arguments = { table.unpack(self.arguments) }

        if declaration.terminal ~= nil then
            arguments[1] = "-tt"
        end

        arguments[#arguments + 1] = self.host
        arguments[#arguments + 1] = target.command
        local variables

        if self.authentication ~= nil then
            channel = self.authentication:channel()
            variables = channel.variables
        end

        local input = declaration.input ~= nil and target.input .. declaration.input or nil
        running = remoteProgram.new(self.owner, identity, { program = self.executable, arguments = arguments, directory = workpane.app.dataDirectory, variables = variables, input = input, terminal = declaration.terminal })

        if input == nil and target.input ~= "" then
            running:write(target.input)
        end

        running.authentication = channel
    end)

    if not accepted then
        if running ~= nil then
            running:close():await()
        else
            self.owner:release(identity, 10000)
        end

        if channel ~= nil then
            channel:close():await()
        end

        error(problem, 0)
    end

    return running
end

function Launcher:execute(script, arguments, directory, shell)
    if self.owner.platform ~= "windows" then
        return self:start({ program = "/bin/sh", arguments = { "-c", script, "workpane", table.unpack(arguments) }, directory = directory, variables = {}, cleared = {}, input = "" })
    end

    local declaration = { program = shell, arguments = {}, directory = directory, variables = {}, cleared = {}, input = "" }

    return self:start(declaration, function(identity)
        local filename = self.owner.path .. "/program." .. tostring(identity) .. "/operation.ps1"
        local parameters = crypto.base64Encode(json.encode(json.array(arguments)))
        local content = "$ErrorActionPreference = 'Stop'\n[string[]]$sshArguments = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('" .. parameters .. "')) | ConvertFrom-Json\n" .. script
        self.files:upload(filename, content, 384)
        local command = "& ([ScriptBlock]::Create([IO.File]::ReadAllText([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('" .. crypto.base64Encode(filename) .. "')))))"
        local encoded = command:gsub(".", function(byte)
            return byte .. "\0"
        end)

        declaration.arguments = { "-NoLogo", "-NoProfile", "-NonInteractive", "-EncodedCommand", crypto.base64Encode(encoded) }
    end)
end

function Launcher:capture(running, maximum)
    local parts = {}
    local bytes = 0
    local result
    local accepted, problem = pcall(function()
        repeat
            result = running:read(65536)

            for _, chunk in ipairs(result.chunks) do
                if chunk.stream == "stdout" then
                    local value = crypto.base64Decode(chunk.content)

                    if #value > maximum - bytes then
                        failed("ssh_output_invalid")
                    end

                    parts[#parts + 1] = value
                    bytes = bytes + #value
                end
            end
        until result.ended
    end)

    local outcome, cleanup = running:close():await()

    if not accepted then
        error(problem, 0)
    end

    if cleanup ~= nil or outcome.failure ~= nil then
        error(cleanup or outcome.failure, 0)
    end

    return result, table.concat(parts)
end

return launch
