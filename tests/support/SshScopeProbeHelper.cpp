#include "support/SshScopeProbeHelper.h"

#include "app/Product.h"
#include "platform/NativeProcesses.h"
#include "platform/NativePseudoTerminals.h"
#include "platform/PathTextHelper.h"
#include "scripting/ScriptRuntime.h"
#include "support/HeadlessProduct.h"
#include "support/Resources.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace workpane::tests {

void SshScopeProbeHelper::verify(const std::filesystem::path& executable, std::vector<std::string> arguments, const std::filesystem::path& workspace) {
    const auto first = workspace / std::filesystem::path(std::u8string(u8"metadata '雪 first.txt"));
    const auto second = workspace / std::filesystem::path(std::u8string(u8"metadata '雪 second.txt"));
    std::ofstream(first) << "first";
    std::ofstream(second) << "second";
    const auto stamp = std::filesystem::last_write_time(first);
    std::filesystem::last_write_time(second, stamp + std::chrono::nanoseconds(1));
    ASSERT_NE(std::filesystem::last_write_time(first), std::filesystem::last_write_time(second));
    const auto files = workspace / "scope-workspace";
    const auto outside = workspace / "scope-other";
    std::filesystem::create_directories(files / "inside");
    std::filesystem::create_directory(files / "empty");
    std::ofstream(files / ".git") << "gitdir: elsewhere";
    std::filesystem::create_directory(outside);
    const std::string payload = std::string("\0\xff\x80\xc3\xa9\r\n", 7) + std::string(66000, 'x');
    std::ofstream(files / "binary.txt", std::ios::binary) << payload;
    std::ofstream(files / "inside" / "first.txt") << "first";
    std::ofstream(outside / "outside.txt") << "outside";
    std::filesystem::create_directory_symlink(outside, files / "jump");
    std::filesystem::create_directory(files / "remove-tree");
    std::filesystem::create_directory_symlink(outside, files / "remove-tree" / "external");
    HeadlessProduct product(workspace / "direct-scope-data");
    product.setProcessLauncher(std::make_unique<platform::NativeProcesses>());
    product.setTerminalHost(std::make_unique<platform::NativePseudoTerminals>());
    ASSERT_TRUE(product.boot().hasValue());
    product.recordCalls("workpane_log");
    const std::string modules = platform::PathTextHelper::generic(Resources::staged() / "plugins" / "remote-ssh");
    const std::string root = platform::PathTextHelper::generic(workspace);
    const std::string script = "local MODULES = " + nlohmann::json(modules).dump() + "\nlocal PROGRAM = " + nlohmann::json(platform::PathTextHelper::generic(executable)).dump() + "\nlocal ROOT = " + nlohmann::json(root).dump() + "\nlocal ARGUMENTS = require('json').decode(" + nlohmann::json(nlohmann::json(arguments).dump()).dump() + ")\n" + R"lua(
        local bridge = require("workpane.bridge")
        local environment = setmetatable({}, { __index = _G })
        local loaded = {}
        environment.workpane = require("workpane.api").create("remote-ssh", MODULES, bridge.call("workpane_app_info", {}))

        function environment.include(name)
            if loaded[name] == nil then
                loaded[name] = assert(loadfile(MODULES .. "/" .. name .. ".lua", "t", environment))()
            end

            return loaded[name]
        end

        require("workpane.task").run("remote-ssh", "test", function()
            local accepted, failure = xpcall(function()
                local async = require("async")
                local crypto = require("crypto")
                local json = require("json")
                local source = environment.include("source")
                local platform = require("platform").os()
                local declaration = source.prepare(platform, source.asset("ssh-scope-posix.sh"), { "60", source.asset("ssh-session-macos.js") })
                local options = { table.unpack(ARGUMENTS) }
                options[#options + 1] = declaration.command
                local connection = environment.include("program").new(PROGRAM, options, {}, nil)
                local finished, finish = async.deferred()
                local announced = connection:line("SCOPE ", 10000, { closed = false, finished = finished, finish = finish })
                local directory, nativeOwner = announced:match("^SCOPE (.+) ([0-9]+)$")
                assert(directory ~= nil)
                local owner = environment.include("scope").new(connection, platform, directory, tonumber(nativeOwner))
                local account = owner:account(10000)
                assert(account.account ~= "" and account.home:sub(1, 1) == "/" and account.shell:sub(1, 1) == "/")
                local metadata = owner:metadata(ROOT, 10000)
                assert(metadata.kind == "directory" and metadata.size == 0 and metadata.readable and metadata.writable)
                local first = owner:metadata(ROOT .. "/metadata '雪 first.txt", 10000)
                local second = owner:metadata(ROOT .. "/metadata '雪 second.txt", 10000)
                assert(first.kind == "file" and first.size == 5)
                assert(second.kind == "file" and second.size == 6)
                assert(first.modified ~= second.modified)
                local _, touchFailure = require("fs").writeFile(ROOT .. "/metadata '雪 first.txt", "fresh"):await()
                assert(touchFailure == nil)
                assert(owner:metadata(ROOT .. "/metadata '雪 first.txt", 10000).recent)

                for index = 1, 1000 do
                    local identity = owner:open(10000)
                    assert(identity == index)
                    owner:release(identity, 10000)
                end

                local sftpArguments = { table.unpack(ARGUMENTS, 1, #ARGUMENTS - 1) }
                sftpArguments[#sftpArguments + 1] = "-s"
                sftpArguments[#sftpArguments + 1] = ARGUMENTS[#ARGUMENTS]
                sftpArguments[#sftpArguments + 1] = "sftp"
                local client = environment.include("sftp").new({ program = PROGRAM, arguments = sftpArguments, directory = ROOT })
                assert(client:start(10000))
                local transport = environment.include("backend").new(owner, client, PROGRAM, { table.unpack(ARGUMENTS, 1, #ARGUMENTS - 1) }, ARGUMENTS[#ARGUMENTS], nil, ROOT .. "/scope-workspace", account)
                local launcher = transport.launcher
                local found = transport:request("process.find", { program = "/bin/sh" })
                assert(found.found and found.path == "/bin/sh")
                found = transport:request("process.find", { program = "workpane-definitely-absent-executable" })
                assert(not found.found and found.path == "")
                local accountContext = environment.include("context").new(client, owner, ROOT .. "/scope-workspace/inside", account)
                local information = accountContext:request("context.info", {})
                assert(information.account == account.account and information.home == account.home and information.repository == ROOT .. "/scope-workspace" and information.root == ROOT .. "/scope-workspace/inside")
                local inspectedContext = accountContext:request("context.read-prefix", { scope = "repository", path = "binary.txt", maximum = 2 })
                assert(crypto.base64Decode(inspectedContext.content) == "\0\255" and not inspectedContext.complete)
                local contextAccepted, contextProblem = pcall(accountContext.request, accountContext, "context.write", { scope = "home", path = "unaccepted" })
                assert(not contextAccepted and contextProblem.code == "remote_method_unknown")
                local files = transport.files
                local document = files:read("binary.txt")
                local expected = "\0\255\128\195\169\r\n" .. string.rep("x", 66000)
                assert(crypto.base64Decode(document.content) == expected)
                assert(document.revision == environment.workpane.files.hash(expected):await())
                local prefix = files:prefix("binary.txt", 2)
                assert(crypto.base64Decode(prefix.content) == "\0\255" and not prefix.complete)
                prefix = files:prefix("binary.txt", #expected)
                assert(crypto.base64Decode(prefix.content) == expected and prefix.complete)
                assert(files:resolve("new/draft.txt", true) == ROOT .. "/scope-workspace/new/draft.txt")
                local inspected, problem = pcall(files.read, files, "jump/outside.txt")
                assert(not inspected and problem.code == "remote_path_outside_workspace")
                local listing = files:list("inside", 1)
                assert(not listing.incomplete and #listing.entries == 1 and listing.entries[1].name == "first.txt")
                listing = files:list("empty", 1)
                assert(not listing.incomplete and #listing.entries == 0)
                listing = files:list(".", 1)
                assert(listing.incomplete and #listing.entries <= 1)
                local staged, stageFailure = pcall(files.write, files, "missing-parent/file.txt", crypto.base64Encode("unaccepted"), "")
                assert(not staged and stageFailure.code == "remote_file_write_failed" and not owner.closed)
                local saved = files:write("saved.txt", crypto.base64Encode("first\0\255"), "")
                document = files:read("saved.txt")
                assert(crypto.base64Decode(document.content) == "first\0\255" and document.revision == saved.revision)
                inspected, problem = pcall(files.write, files, "saved.txt", crypto.base64Encode("unaccepted"), "")
                assert(not inspected and problem.code == "remote_file_conflict")
                local packets = environment.include("sftp-packets")
                local permission, permissionFailure = client:request(packets.types.setstat, packets.string(ROOT .. "/scope-workspace/saved.txt") .. packets.integer(4) .. packets.integer(416), packets.types.status, 10000)
                assert(permissionFailure == nil and permission.status == 0)
                local changed = files:write("saved.txt", crypto.base64Encode("replacement\0\255"), saved.revision)
                local attributes = files:attributes(ROOT .. "/scope-workspace/saved.txt", false)
                assert(attributes.permissions & 511 == 416)
                inspected, problem = pcall(files.write, files, "saved.txt", crypto.base64Encode("stale"), saved.revision)
                assert(not inspected and problem.code == "remote_file_conflict")
                document = files:read("saved.txt")
                assert(crypto.base64Decode(document.content) == "replacement\0\255" and document.revision == changed.revision)
                listing = files:list(".", 20)

                for _, entry in ipairs(listing.entries) do
                    assert(entry.name:find(".workpane-stage-", 1, true) == nil)
                end

                assert(files:mkdir("created").kind == "directory")
                files:write("created/text.txt", crypto.base64Encode("first\n Some NEEDLE output \r\n needle again\n"), "")
                local walked = files:walk("created", 1, {})
                assert(walked.complete and #walked.paths == 1 and walked.paths[1] == "text.txt")
                local searched = files:search("created", "needle", 10, 1024, {})
                assert(searched.complete and #searched.matches == 2)
                assert(searched.matches[1].line == 2 and searched.matches[1].text == "Some NEEDLE output")
                assert(searched.matches[2].line == 3 and searched.matches[2].text == "needle again")
                local moved = files:rename("created/text.txt", "created/moved.txt")
                assert(moved.path == ROOT .. "/scope-workspace/created/moved.txt")
                inspected, problem = pcall(files.rename, files, "saved.txt", "binary.txt")
                assert(not inspected and problem.code == "remote_file_rename_failed")
                files:remove("created", true)
                files:remove("remove-tree", true)
                inspected, problem = pcall(files.stat, files, "created")
                assert(not inspected and problem.code == "remote_path_unavailable")
                local group = transport:reserveGroup()
                transport:request("process.group.open", { group = group })
                local started = transport:request("process.start", { group = group, program = "/bin/sh", arguments = { "-c", "sleep 120" }, directory = ROOT .. "/scope-workspace" })
                local reading = async.promise(function()
                    return transport:request("process.read", { group = group, process = started.process })
                end)
                transport:request("process.stop", { group = group, process = started.process })
                local stopped, stopFailure = reading:await()
                assert(stopFailure == nil and stopped.ended and stopped.code == -1 and stopped.failed)
                transport:request("process.group.close", { group = group })
                local reopened, refused = pcall(transport.request, transport, "process.group.open", { group = group })
                assert(not reopened and refused.code == "remote_process_group_closed")
                assert(transport.activeGroups == 0)

                for _ = 1, 1000 do
                    local reserved = transport:reserveGroup()
                    transport:request("process.group.open", { group = reserved })
                    transport:request("process.group.close", { group = reserved })
                    assert(next(transport.groups) == nil and transport.activeGroups == 0)
                    local reopened, refusal = pcall(transport.request, transport, "process.group.open", { group = reserved })
                    assert(not reopened and refusal.code == "remote_process_group_closed")
                end

                local cancelled = transport:reserveGroup()
                transport:request("process.group.close", { group = cancelled })
                local reopened, refusal = pcall(transport.request, transport, "process.group.open", { group = cancelled })
                assert(not reopened and refusal.code == "remote_process_group_closed")
                assert(client:close())

                local payload = "\0\255\128\195\169\r\n" .. string.rep("binary", 11000)
                local running = launcher:start({ program = "/bin/sh", arguments = { "-c", "test \"$1\" = 'Unicode 雪 quoted' && test -z \"${HOME+x}\" && test \"$EXACT\" = '雪 value' || exit 99; cat; printf '\\000\\377error' >&2; exit 7", "workpane", "Unicode 雪 quoted" }, directory = ROOT, variables = { EXACT = "雪 value" }, cleared = { "HOME" }, input = payload })
                local streams = { stdout = {}, stderr = {} }
                local result

                repeat
                    result = running:read(65536)

                    for _, chunk in ipairs(result.chunks) do
                        streams[chunk.stream][#streams[chunk.stream] + 1] = crypto.base64Decode(chunk.content)
                    end
                until result.ended

                assert(table.concat(streams.stdout) == payload, json.encode({ expected = #payload, actual = #table.concat(streams.stdout), code = result.code, error = crypto.base64Encode(table.concat(streams.stderr)) }))
                assert(table.concat(streams.stderr) == "\0\255error")
                assert(result.code == 7 and result.failed and owner.count == 0)
                assert(running:close():await().value)
                local outcome, problem = owner:close():await()
                assert(problem == nil and outcome.value)
                local configured = {}

                for index, argument in ipairs(ARGUMENTS) do
                    if argument == "-p" then
                        configured.port = tonumber(ARGUMENTS[index + 1])
                    elseif argument == "-i" then
                        configured.identity = ARGUMENTS[index + 1]
                    elseif argument == "-l" then
                        configured.user = ARGUMENTS[index + 1]
                    elseif argument == "-o" and ARGUMENTS[index + 1]:sub(1, 19) == "UserKnownHostsFile=" then
                        configured.trust = ARGUMENTS[index + 1]:sub(20)
                    end
                end

                local function quoted(value)
                    return '"' .. value:gsub('["\\]', '\\%0') .. '"'
                end

                local configuration = ROOT .. "/direct-client.config"
                local written, writeFailure = require("fs").writeFile(configuration, "Host direct-workspace\n    HostName 127.0.0.1\n    IdentitiesOnly yes\n    UserKnownHostsFile " .. quoted(configured.trust) .. "\n    GlobalKnownHostsFile /dev/null\n"):await()
                assert(writeFailure == nil)
                local session = assert(environment.include("session").new({ id = "direct-profile", name = "Direct workspace", host = "direct-workspace", user = configured.user, port = configured.port, identity = configured.identity, configuration = configuration, root = ROOT .. "/scope-workspace", timeoutSeconds = 30, savePassword = false }, function() end))
                local connected, connectFailure = session:start():await()
                assert(connectFailure == nil and connected.value, connected ~= nil and connected.failure ~= nil and connected.failure.code or tostring(connectFailure))
                local sessionDirectory = session.scope.path

                local dispatch = session.backend.request
                session.backend.request = function(...)
                    local accepted, value = xpcall(dispatch, function(problem)
                        return debug.traceback(type(problem) == "table" and problem.code or tostring(problem))
                    end, ...)

                    if not accepted then
                        bridge.call("workpane_log", { plugin = "remote-ssh", level = "error", category = "native-ssh-scope-test", message = "The direct SSH dispatch verification failed", details = { failure = value } })
                        error(value, 0)
                    end

                    return value
                end

                local function request(method, parameters)
                    local value, problem = session:request(method, parameters, 10000)
                    assert(problem == nil, problem ~= nil and problem.code or "")

                    return value
                end

                local contents = request("files.read", { path = "binary.txt" })
                assert(crypto.base64Decode(contents.content) == expected)
                local saved = request("files.write", { path = "session.txt", content = crypto.base64Encode("The direct session saved this document"), revision = "" })
                assert(request("files.read", { path = "session.txt" }).revision == saved.revision)
                local group = session:reserveGroup()
                request("process.group.open", { group = group })
                local started = request("process.start", { group = group, program = "/bin/sh", directory = ".", arguments = { "-c", "printf 'direct-command'; exit 7" }, input = "" })
                local output = ""
                local completion

                repeat
                    completion = request("process.read", { group = group, process = started.process })

                    for _, chunk in ipairs(completion.chunks) do
                        output = output .. crypto.base64Decode(chunk.content)
                    end
                until completion.ended

                assert(output == "direct-command" and completion.code == 7 and completion.failed)
                request("process.group.close", { group = group })
                local history = string.rep("c", 32)
                local terminal = request("terminal.start", { directory = ".", history = history, columns = 80, rows = 24 })
                request("terminal.resize", { terminal = terminal.terminal, columns = 120, rows = 40 })
                request("terminal.write", { terminal = terminal.terminal, content = crypto.base64Encode("stty size; cd inside; printf 'ssh-%s\\n' shell-ready\r") })
                local screen = ""
                local update

                repeat
                    update = request("terminal.read", { terminal = terminal.terminal })
                    screen = screen .. crypto.base64Decode(update.content)
                    assert(#screen < 65536 and not update.ended)
                until screen:find("ssh-shell-ready", 1, true) ~= nil and update.directory == ROOT .. "/scope-workspace/inside"

                assert(screen:find("40 120", 1, true) ~= nil)
                request("terminal.write", { terminal = terminal.terminal, content = crypto.base64Encode("exit 7\r") })

                repeat
                    update = request("terminal.read", { terminal = terminal.terminal })
                until update.ended

                assert(update.code == 7 and session.backend.terminalCount == 0)
                local historyBytes = session.backend.launcher.files:read(session.backend.shell.histories[history].directory .. "/commands.history")
                assert(crypto.base64Decode(historyBytes.content):find("shell-ready", 1, true) ~= nil)
                request("terminal.history.close", { history = history })
                assert(session.backend.shell.count == 0)
                local closed, closeFailure = session:close():await()
                assert(closeFailure == nil and closed.failure == nil, closed ~= nil and closed.failure ~= nil and closed.failure.code or tostring(closeFailure))
                bridge.call("workpane_log", { plugin = "remote-ssh", level = "info", category = "native-ssh-scope-test", message = "The owned SSH scope completed", details = { directory = directory, bytes = #payload, code = result.code, identities = owner.nextIdentity, sessionDirectory = sessionDirectory } })
            end, function(problem)
                return debug.traceback(type(problem) == "table" and problem.code or tostring(problem))
            end)

            if not accepted then
                bridge.call("workpane_log", { plugin = "remote-ssh", level = "error", category = "native-ssh-scope-test", message = "The owned SSH scope verification failed", details = { failure = failure } })
            end
        end)
    )lua";
    ASSERT_TRUE(product.product().runtime().loadString(script, "native-ssh-scope").hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product]() { const auto value = product.lastCall("workpane_log"); return value.is_object() && value.value("category", "") == "native-ssh-scope-test"; }, std::chrono::seconds(90)));
    // clang-format on
    const auto result = product.lastCall("workpane_log")["details"];
    ASSERT_FALSE(result.contains("failure")) << result.dump();
    EXPECT_EQ(result["bytes"], 66007);
    EXPECT_EQ(result["code"], 7);
    EXPECT_EQ(result["identities"], 1009);
    EXPECT_TRUE(std::filesystem::is_regular_file(outside / "outside.txt"));
    EXPECT_FALSE(std::filesystem::exists(files / "remove-tree"));
    const auto directory = result["directory"].get<std::string>();
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(std::u8string(directory.begin(), directory.end()))));
    const auto sessionDirectory = result["sessionDirectory"].get<std::string>();
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(std::u8string(sessionDirectory.begin(), sessionDirectory.end()))));
}

} // namespace workpane::tests
