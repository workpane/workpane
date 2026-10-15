#include "support/SshWorkspaceProbeHelper.h"

#include "app/Product.h"
#include "platform/NativeProcesses.h"
#include "platform/NativePseudoTerminals.h"
#include "platform/PathTextHelper.h"
#include "platform/PrivateDirectoryHelper.h"
#include "scripting/ScriptRuntime.h"
#include "support/HeadlessProduct.h"
#include "support/Resources.h"

#if defined(_WIN32)
#include "support/WindowsFileAccessHelper.h"
#endif

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::tests {

void SshWorkspaceProbeHelper::verify(const std::filesystem::path& executable, const nlohmann::json& profile, const std::vector<int>& ports, const std::filesystem::path& workspace) {
    const std::string payload = std::string("\0\xff\x80\xc3\xa9\r\n", 7) + std::string(66000, 'x');
    const auto files = workspace / "workspace";
    std::filesystem::create_directories(files / "inside");
    std::ofstream(files / "binary.txt", std::ios::binary) << payload;
    std::ofstream(files / "inside" / "first.txt") << "first";
    std::ofstream(files / "existing.txt") << "original";
#if defined(_WIN32)
    ASSERT_TRUE(WindowsFileAccessHelper::protect(files / "existing.txt").hasValue());
    const auto previousAccess = WindowsFileAccessHelper::snapshot(files / "existing.txt");
    ASSERT_TRUE(previousAccess.hasValue());
#endif
    HeadlessProduct product(workspace / "workspace-data");
    product.setProcessLauncher(std::make_unique<platform::NativeProcesses>());
    product.setTerminalHost(std::make_unique<platform::NativePseudoTerminals>());
    ASSERT_TRUE(product.boot().hasValue());
    product.recordCalls("workpane_log");
    const std::string modules = platform::PathTextHelper::generic(Resources::staged() / "plugins" / "remote-ssh");
    const std::string script = "local MODULES = " + nlohmann::json(modules).dump() + "\nlocal PROGRAM = " + nlohmann::json(platform::PathTextHelper::generic(executable)).dump() + "\nlocal PROFILE = require('json').decode(" + nlohmann::json(profile.dump()).dump() + ")\nlocal PORTS = require('json').decode(" + nlohmann::json(nlohmann::json(ports).dump()).dump() + ")\n" + R"lua(
        local bridge = require("workpane.bridge")
        local environment = setmetatable({}, { __index = _G })
        local loaded = {}
        environment.workpane = require("workpane.api").create("remote-ssh", MODULES, bridge.call("workpane_app_info", {}))
        local start = environment.workpane.process.start
        local nextPort = 0
        local diagnostic = ""
        environment.workpane.process.start = function(options)
            nextPort = nextPort + 1
            assert(PORTS[nextPort] ~= nil)
            options.program = PROGRAM

            for index, argument in ipairs(options.arguments) do
                if argument == "-p" then
                    options.arguments[index + 1] = tostring(PORTS[nextPort])
                end
            end

            local receive = options.onOutput

            if receive ~= nil then
                options.onOutput = function(chunks)
                    for _, chunk in ipairs(chunks) do
                        diagnostic = (diagnostic .. chunk.stream .. ": " .. chunk.text):sub(-16384)
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

        require("workpane.task").run("remote-ssh", "test", function()
            local accepted, failure = xpcall(function()
                local crypto = require("crypto")
                PROFILE.timeoutSeconds = environment.include("preferences").defaultTimeoutSeconds
                local connected = assert(environment.include("session").new(PROFILE, function() end))
                local opened, problem = connected:start():await()
                assert(problem == nil and opened.value, (opened ~= nil and opened.failure ~= nil and opened.failure.code or tostring(problem)) .. "/" .. diagnostic)
                local scope = connected.scope.path
                local backend = connected.backend
                local files = backend.files
                local witness = backend.launcher:execute("[IO.File]::WriteAllText($sshArguments[0], 'witness')", { backend.root .. "/inheritance-witness.txt" }, backend.root, backend.account.shell)
                local witnessResult = backend.launcher:capture(witness, 65536)
                assert(witnessResult.code == 0)
                local argumentSource = [=[
                    $fields = @()

                    foreach ($argument in $sshArguments) {
                        $fields += [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($argument))
                    }

                    [Console]::Write([string]$sshArguments.Length + "`n" + ($fields -join "`n"))
                ]=]

                for _, arguments in ipairs({ {}, { "" }, { "Unicode 雪 'quoted'\n$HOME `command`", "tail" } }) do
                    local expected = {}

                    for index, argument in ipairs(arguments) do
                        expected[index] = crypto.base64Encode(argument)
                    end

                    local running = backend.launcher:execute(argumentSource, arguments, backend.root, backend.account.shell)
                    local result, output = backend.launcher:capture(running, 65536)
                    assert(result.code == 0 and output == tostring(#arguments) .. "\n" .. table.concat(expected, "\n"))
                end

                local publish = files.execute
                files.execute = function(script, arguments)
                    if #arguments == 4 then
                        local permissions = files:attributes(arguments[1] .. "/payload", false).permissions
                        assert(permissions & 511 == 384)
                    end

                    return publish(script, arguments)
                end

                local expected = "\0\255\128\195\169\r\n" .. string.rep("x", 66000)
                local document = files:read("binary.txt")
                assert(crypto.base64Decode(document.content) == expected)
                assert(document.revision == environment.workpane.files.hash(expected):await())
                local prefix = files:prefix("binary.txt", 2)
                assert(crypto.base64Decode(prefix.content) == "\0\255" and not prefix.complete)
                local entries = files:list("inside", 1)
                assert(not entries.incomplete and #entries.entries == 1 and entries.entries[1].name == "first.txt")
                local staged, refused = pcall(files.write, files, "missing-parent/file.txt", crypto.base64Encode("unaccepted"), "")
                assert(not staged and refused.code == "remote_file_write_failed" and not connected.scope.closed)
                local existing = files:read("existing.txt")
                files:write("existing.txt", crypto.base64Encode("replacement"), existing.revision)
                assert(crypto.base64Decode(files:read("existing.txt").content) == "replacement")
                local saved = files:write("document 雪.txt", crypto.base64Encode("first\0\255"), "")
                document = files:read("document 雪.txt")
                assert(crypto.base64Decode(document.content) == "first\0\255" and document.revision == saved.revision)
                local changed = files:write("document 雪.txt", crypto.base64Encode("replacement\0\255"), saved.revision)
                local replaced, conflict = pcall(files.write, files, "document 雪.txt", crypto.base64Encode("stale"), saved.revision)
                assert(not replaced and conflict.code == "remote_file_conflict")
                assert(files:read("document 雪.txt").revision == changed.revision)
                assert(files:mkdir("created").kind == "directory")
                files:write("created/text.txt", crypto.base64Encode("first\n Some NEEDLE output \r\n needle again\n"), "")
                local walked = files:walk("created", 1, {})
                assert(walked.complete and #walked.paths == 1 and walked.paths[1] == "text.txt")
                local searched = files:search("created", "needle", 10, 1024, {})
                assert(searched.complete and #searched.matches == 2)
                assert(searched.matches[1].line == 2 and searched.matches[2].line == 3)
                files:rename("created/text.txt", "created/moved.txt")
                local renamed, collision = pcall(files.rename, files, "created/moved.txt", "binary.txt")
                assert(not renamed and collision.code == "remote_file_rename_failed", require("json").encode({ accepted = renamed, result = collision }))
                files:remove("created", true)
                entries = files:list(".", 20)

                for _, entry in ipairs(entries.entries) do
                    assert(entry.name:find(".workpane-stage-", 1, true) == nil)
                end

                local account = backend.context:request("context.info", {})
                assert(account.account ~= "" and account.home ~= "")
                local found = backend:request("process.find", { program = backend.account.shell })
                assert(found.found and found.path ~= "")
                local group = connected:reserveGroup()
                backend:request("process.group.open", { group = group })
                local command = "$inputStream = [Console]::OpenStandardInput(); $outputStream = [Console]::OpenStandardOutput(); $inputStream.CopyTo($outputStream); $outputStream.Flush(); $errorStream = [Console]::OpenStandardError(); $errorStream.Write([byte[]]@(0,255,101,114,114,111,114),0,7); $errorStream.Flush(); exit 7"
                local encoded = command:gsub(".", function(byte) return byte .. "\0" end)
                local running = backend:request("process.start", { group = group, program = backend.account.shell, directory = ".", arguments = { "-NoLogo", "-NoProfile", "-NonInteractive", "-EncodedCommand", crypto.base64Encode(encoded) }, input = crypto.base64Encode(expected) })
                local streams = { stdout = {}, stderr = {} }
                local result

                repeat
                    result = backend:request("process.read", { group = group, process = running.process })

                    for _, chunk in ipairs(result.chunks) do
                        streams[chunk.stream][#streams[chunk.stream] + 1] = crypto.base64Decode(chunk.content)
                    end
                until result.ended

                assert(table.concat(streams.stdout) == expected)
                assert(table.concat(streams.stderr) == "\0\255error")
                assert(result.code == 7 and result.failed)
                backend:request("process.group.close", { group = group })
                assert(next(backend.groups) == nil)
                local history = crypto.hexEncode(crypto.randomBytes(16))
                local terminal = backend:request("terminal.start", { directory = ".", history = history, columns = 80, rows = 24 })
                local historyDirectory = backend.shell.histories[history].directory
                bridge.call("workpane_log", { plugin = "remote-ssh", level = "info", category = "native-ssh-private-state-test", message = "The private SSH paths were created", details = { scope = scope, stage = historyDirectory } })
                local screen = ""
                local update

                repeat
                    update = backend:request("terminal.read", { terminal = terminal.terminal })
                    screen = screen .. crypto.base64Decode(update.content)
                    assert(#screen < 65536 and not update.ended)
                until screen:find("PS ", 1, true) ~= nil

                backend:request("terminal.resize", { terminal = terminal.terminal, columns = 120, rows = 40 })
                backend:request("terminal.write", { terminal = terminal.terminal, content = crypto.base64Encode("Set-Location inside; [Console]::WriteLine(('ssh-{0}' -f 'workspace-ready'))\r") })
                screen = ""

                repeat
                    update = backend:request("terminal.read", { terminal = terminal.terminal })
                    screen = screen .. crypto.base64Decode(update.content)
                    assert(#screen < 65536 and not update.ended)
                until screen:find("ssh-workspace-ready", 1, true) ~= nil and update.directory == connected.identity.root .. "/inside"

                backend:request("terminal.write", { terminal = terminal.terminal, content = crypto.base64Encode("exit 7\r") })

                repeat
                    update = backend:request("terminal.read", { terminal = terminal.terminal })
                until update.ended

                assert(update.code == 7)
                local savedHistory = backend.launcher.files:read(historyDirectory .. "/commands.history")
                assert(crypto.base64Decode(savedHistory.content):find("workspace-ready", 1, true) ~= nil)
                terminal = backend:request("terminal.start", { directory = ".", history = history, columns = 80, rows = 24 })
                assert(backend.shell.histories[history].directory == historyDirectory)
                backend:request("terminal.close", { terminal = terminal.terminal })
                backend:request("terminal.history.close", { history = history })
                assert(next(backend.shell.histories) == nil)
                local closed, closeFailure = connected:close():await()
                assert(closeFailure == nil and closed.failure == nil, closed.failure ~= nil and closed.failure.code or tostring(closeFailure))
                bridge.call("workpane_log", { plugin = "remote-ssh", level = "info", category = "native-ssh-workspace-test", message = "The native SSH workspace completed", details = { scope = scope, bytes = #expected, channels = nextPort } })
            end, function(problem)
                return debug.traceback(type(problem) == "table" and problem.code or tostring(problem))
            end)

            if not accepted then
                bridge.call("workpane_log", { plugin = "remote-ssh", level = "error", category = "native-ssh-workspace-test", message = "The native SSH workspace verification failed", details = { failure = failure } })
            end
        end)
    )lua";
    ASSERT_TRUE(product.product().runtime().loadString(script, "native-ssh-workspace").hasValue());
#if defined(_WIN32)
    const auto inspectPrivate = WindowsFileAccessHelper::privateDirectory;
#else
    const auto inspectPrivate = platform::PrivateDirectoryHelper::restricted;
#endif
    bool privateChecked = false;
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product, &privateChecked, inspectPrivate]() {
        const auto value = product.lastCall("workpane_log");

        if (value.is_object() && value.value("category", "") == "native-ssh-private-state-test" && !privateChecked) {
            for (const std::string_view field : {"scope", "stage"}) {
                const auto path = value["details"][field].get<std::string>();
                EXPECT_TRUE(inspectPrivate(std::filesystem::path(std::u8string(path.begin(), path.end()))));
            }

            privateChecked = true;
        }

        return value.is_object() && value.value("category", "") == "native-ssh-workspace-test";
    }, std::chrono::seconds(180)));
    // clang-format on
    EXPECT_TRUE(privateChecked);
    const auto result = product.lastCall("workpane_log")["details"];
    ASSERT_FALSE(result.contains("failure")) << result.dump();
    EXPECT_EQ(result["bytes"], 66007);
#if defined(_WIN32)
    const auto preservedAccess = WindowsFileAccessHelper::snapshot(files / "existing.txt");
    const auto createdAccess = WindowsFileAccessHelper::snapshot(files / std::filesystem::path(std::u8string(u8"document 雪.txt")));
    const auto inheritedAccess = WindowsFileAccessHelper::snapshot(files / "inheritance-witness.txt");
    ASSERT_TRUE(preservedAccess.hasValue());
    ASSERT_TRUE(createdAccess.hasValue());
    ASSERT_TRUE(inheritedAccess.hasValue());
    EXPECT_EQ(preservedAccess.value(), previousAccess.value());
    EXPECT_EQ(createdAccess.value(), inheritedAccess.value());
#endif
    EXPECT_LE(result["channels"].get<std::size_t>(), ports.size());
    const auto scope = result["scope"].get<std::string>();
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(std::u8string(scope.begin(), scope.end()))));
}

} // namespace workpane::tests
