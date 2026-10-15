#include "support/SshTerminalProbeHelper.h"

#include "app/Product.h"
#include "platform/NativeProcesses.h"
#include "platform/NativePseudoTerminals.h"
#include "platform/PathTextHelper.h"
#include "scripting/ScriptRuntime.h"
#include "support/HeadlessProduct.h"
#include "support/Resources.h"
#include "text/Base64Helper.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace workpane::tests {

std::string SshTerminalProbeHelper::command() {
#if defined(_WIN32)
    const std::string source = "[Console]::WriteLine('ssh-pty-ready')";
    std::string encoded;

    for (const char character : source) {
        encoded.push_back(character);
        encoded.push_back('\0');
    }

    return text::Base64Helper::encode(encoded);
#else
    const std::string source = "trap ':' INT; stty -echo; printf 'ssh-pty-ready\\n'; while IFS= read -r command; do case $command in size) printf 'ssh-size/'; stty size;; block) /bin/sh -c 'printf \"ssh-pty-blocked\\n\"; sleep 120'; printf 'ssh-pty-resumed\\n';; exit) printf 'ssh-final\\n'; exit 7;; esac; done";
    return source;
#endif
}

void SshTerminalProbeHelper::verify(const std::filesystem::path& executable, const std::vector<std::string>& control, const std::vector<std::string>& transfer, std::vector<std::string> arguments, const std::filesystem::path& workspace) {
    const auto mode = std::ranges::find(arguments, "-T");
    ASSERT_NE(mode, arguments.end());
    *mode = "-tt";
    HeadlessProduct product(workspace / "direct-terminal-data");
    product.setTerminalHost(std::make_unique<platform::NativePseudoTerminals>());
    product.setProcessLauncher(std::make_unique<platform::NativeProcesses>());
    ASSERT_TRUE(product.boot().hasValue());
    product.recordCalls("workpane_log");
    const std::string modules = platform::PathTextHelper::generic(Resources::staged() / "plugins" / "remote-ssh");
    const std::string source = "local MODULES = " + nlohmann::json(modules).dump() + "\nlocal PROGRAM = " + nlohmann::json(platform::PathTextHelper::generic(executable)).dump() + "\nlocal DIRECTORY = " + nlohmann::json(platform::PathTextHelper::generic(workspace)).dump() + "\nlocal CONTROL = require('json').decode(" + nlohmann::json(nlohmann::json(control).dump()).dump() + ")\nlocal TRANSFER = require('json').decode(" + nlohmann::json(nlohmann::json(transfer).dump()).dump() + ")\nlocal ARGUMENTS = require('json').decode(" + nlohmann::json(nlohmann::json(arguments).dump()).dump() + ")\nlocal TARGET = " + nlohmann::json(command()).dump() + "\n" + R"lua(
        local bridge = require("workpane.bridge")
        local task = require("workpane.task")
        local async = require("async")
        local crypto = require("crypto")
        local windows = require("platform").os() == "windows"
        local environment = setmetatable({}, { __index = _G })
        local loaded = {}
        environment.workpane = require("workpane.api").create("remote-ssh", MODULES, bridge.call("workpane_app_info", {}))
        local start = environment.workpane.process.start
        local diagnostic = ""
        environment.workpane.process.start = function(options)
            local receive = options.onOutput

            if receive ~= nil then
                options.onOutput = function(chunks)
                    for _, chunk in ipairs(chunks) do
                        if chunk.stream == "error" then
                            diagnostic = (diagnostic .. chunk.text):sub(-16384)
                        end
                    end

                    receive(chunks)
                end
            end

            return start(options)
        end

        function environment.include(name)
            if loaded[name] == nil then
                loaded[name] = assert(loadfile(MODULES .. "/" .. name .. ".lua", "t", environment))()
            end

            return loaded[name]
        end

        task.run("remote-ssh", "test", function()
            local accepted, failure = xpcall(function()
                local source = environment.include("source")
                local platform = require("platform").os()
                local controller

                if windows then
                    controller = source.prepare(platform, source.asset("ssh-jobs-windows.ps1") .. source.asset("ssh-paths-windows.ps1") .. source.asset("ssh-events-windows.ps1") .. source.asset("ssh-scope-windows.ps1"), { crypto.hexEncode(crypto.randomBytes(16)), "60000" })
                else
                    controller = source.prepare(platform, source.asset("ssh-scope-posix.sh"), { "60", source.asset("ssh-session-macos.js") })
                end

                local options = { table.unpack(CONTROL) }
                options[#options + 1] = controller.command
                local connection = environment.include("program").new(PROGRAM, options, {}, nil)

                if controller.input ~= "" then
                    connection:write(controller.input)
                end

                local finished, finish = async.deferred()
                local startupTimeoutMs = environment.include("preferences").defaultTimeoutSeconds * 1000
                local announced = connection:line("SCOPE ", startupTimeoutMs, { closed = false, finished = finished, finish = finish })
                local directory, nativeOwner = announced:match("^SCOPE (.+) ([0-9]+)$")
                assert(directory ~= nil)
                local owner = environment.include("scope").new(connection, platform, directory, tonumber(nativeOwner))
                local identity = owner:open(10000)
                local target

                if windows then
                    local record = directory .. "/program." .. identity
                    local declaration = "$record = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('" .. crypto.base64Encode(record) .. "'))\n$target = '" .. TARGET .. "'\n"
                    local script = source.asset("ssh-jobs-windows.ps1") .. source.asset("ssh-paths-windows.ps1") .. source.asset("ssh-programs-windows.ps1") .. declaration .. [=[
                        [Workpane.Ssh.Jobs]::Join([IO.File]::ReadAllText([IO.Path]::Combine($record, 'job')))
                        [Workpane.Ssh.Paths]::CreatePrivate([IO.Path]::Combine($record, 'claimed'))
                        $executable = [IO.Path]::Combine([Environment]::SystemDirectory, 'WindowsPowerShell/v1.0/powershell.exe')
                        $arguments = [string[]]@('-NoLogo', '-NoProfile', '-NoExit', '-EncodedCommand', $target)
                        $code = [Workpane.Ssh.Programs]::Run($executable, $arguments, $record, @{}, [string[]]@(), $true, $null, $null, $null, $record, [IntPtr]::Zero)
                        exit $code
                    ]=]
                    local transfer = { table.unpack(TRANSFER, 1, #TRANSFER - 1) }
                    transfer[#transfer + 1] = "-s"
                    transfer[#transfer + 1] = TRANSFER[#TRANSFER]
                    transfer[#transfer + 1] = "sftp"
                    local packets = environment.include("sftp-packets")
                    local types = packets.types
                    local client = environment.include("sftp").new({ program = PROGRAM, arguments = transfer, directory = DIRECTORY })
                    assert(client:start(10000))
                    local filename = record .. "/terminal.ps1"
                    local wire = filename:match("^[A-Za-z]:/") ~= nil and "/" .. filename or filename

                    local function request(kind, payload, expected)
                        local value, problem = client:request(kind, payload, expected, 10000)
                        assert(value ~= nil and problem == nil and (value.type ~= types.status or value.status == 0))

                        return value
                    end

                    local handle = request(types.open, packets.string(wire) .. packets.integer(2 | 8 | 32) .. packets.integer(4) .. packets.integer(384), types.handle).handle

                    for offset = 0, #script - 1, packets.maximumData do
                        request(types.write, packets.string(handle) .. packets.offset(offset) .. packets.string(script:sub(offset + 1, offset + packets.maximumData)), types.status)
                    end

                    request(types.close, packets.string(handle), types.status)
                    assert(client:close())
                    local launch = "& ([ScriptBlock]::Create([IO.File]::ReadAllText([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('" .. crypto.base64Encode(filename) .. "')))))"
                    local encoded = launch:gsub(".", function(byte)
                        return byte .. "\0"
                    end)

                    target = { command = "powershell.exe -NoLogo -NoProfile -NonInteractive -EncodedCommand " .. crypto.base64Encode(encoded) }
                else
                    target = source.prepare(platform, source.asset("ssh-program-posix.sh"), { directory, tostring(identity), "terminal", "/bin/sh", "-c", TARGET })
                end

                ARGUMENTS[#ARGUMENTS + 1] = target.command
                local running = environment.include("remote-program").new(owner, identity, { program = PROGRAM, arguments = ARGUMENTS, directory = DIRECTORY, terminal = { columns = 80, rows = 24 } })
                local pending = ""
                local ended = false
                local code

                local function receive(witness, timeoutMs)
                    while pending:find(witness, 1, true) == nil do
                        assert(not ended, pending)
                        local value, problem = async.timeout(async.promise(function() return running:read(65536) end), timeoutMs):await()
                        assert(problem == nil, witness .. "/" .. tostring(problem) .. "/" .. pending)

                        for _, chunk in ipairs(value.chunks) do
                            pending = (pending .. crypto.base64Decode(chunk.content)):sub(-16384)
                        end

                        ended = value.ended
                        code = value.code
                    end

                    pending = ""
                end

                receive("ssh-pty-ready", startupTimeoutMs)
                running:resize(120, 40)
                running:write(windows and "[Console]::WriteLine(('ssh-' + 'size/') + [Console]::WindowHeight + '/' + [Console]::WindowWidth)\r" or "size\r")
                receive(windows and "ssh-size/40/120" or "ssh-size/40 120", 15000)
                running:write(windows and "[Console]::WriteLine('ssh-' + 'pty-blocked'); Start-Sleep -Seconds 120\r" or "block\r")
                receive("ssh-pty-blocked", 15000)
                running:write("\3")

                if windows then
                    receive("> ", 15000)
                    running:write("[Console]::WriteLine('ssh-' + 'pty-resumed')\r")
                end

                receive("ssh-pty-resumed", 15000)
                running:write(windows and "[Console]::WriteLine('ssh-' + 'final'); exit 7\r" or "exit\r")
                receive("ssh-final", 15000)

                while not ended do
                    local value = task.await(async.timeout(async.promise(function() return running:read(65536) end), 15000))
                    ended = value.ended
                    code = value.code
                end

                assert(code == 7, "The native SSH terminal exited with code " .. tostring(code))
                assert(running:close():await().value)
                assert(owner:close():await().value)
                bridge.call("workpane_log", { plugin = "remote-ssh", level = "info", category = "native-ssh-terminal-test", message = "The SSH terminal completed", details = { code = code, interrupted = true, resized = true } })
            end, function(problem)
                return debug.traceback((type(problem) == "table" and problem.code or tostring(problem)) .. "/" .. diagnostic)
            end)

            if not accepted then
                bridge.call("workpane_log", { plugin = "remote-ssh", level = "error", category = "native-ssh-terminal-test", message = "The native SSH terminal verification failed", details = { failure = failure } })
            end
        end)
    )lua";
    ASSERT_TRUE(product.product().runtime().loadString(source, "native-ssh-terminal").hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product]() { const auto value = product.lastCall("workpane_log"); return value.is_object() && value.value("category", "") == "native-ssh-terminal-test"; }, std::chrono::seconds(90)));
    // clang-format on
    EXPECT_FALSE(product.lastCall("workpane_log")["details"].contains("failure")) << product.lastCall("workpane_log")["details"].dump();
    EXPECT_EQ(product.lastCall("workpane_log")["details"], nlohmann::json({{"code", 7}, {"interrupted", true}, {"resized", true}}));
}

} // namespace workpane::tests
