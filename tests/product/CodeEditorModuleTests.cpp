#include "platform/PrivateDirectoryHelper.h"
#include "scripting/HostReply.h"
#include "support/Resources.h"
#include "support/ScriptHarness.h"
#include "support/TemporaryDirectory.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <tuple>

namespace workpane::tests {

using nlohmann::json;

// Loads the Lua modules of the Code Editor plugin by themselves and checks the rules they carry, which the product tests reach only through a few cases.
class CodeEditorModuleTest : public ::testing::Test {
  protected:
    // Each module is loaded in an environment that answers include with the other modules of the plugin and names texts by their keys, and a body that waits is run until it reports.
    [[nodiscard]] static json run(std::string_view body) {
        const std::string directory = (Resources::staged() / "plugins" / "code-editor").generic_string();
        const std::string prelude = R"(
            local directory = [==[)" +
                                    directory + R"(]==]
            local loaded = {}
            local environment = setmetatable({}, { __index = _G })
            local paths = require("workpane.paths")
            local system = require("platform").os()
            environment.workpane = { ui = {}, i18n = { text = function(key) return key end, number = function(value) return value end, translate = function(key) return key end } }
            environment.workpane.files = { uri = function(path) return paths.uri(system, path) end, path = function(uri) return paths.path(system, uri) end, absolute = function(path) return paths.absolute(system, path) end }
            environment.workpane.files.stat = require("fs").stat
            environment.workpane.files.paths = function(platform)
                return { absolute = function(path) return paths.absolute(platform, path) end, uri = function(path) return paths.uri(platform, path) end, path = function(uri) return paths.path(platform, uri) end }
            end

            function environment.include(name)
                if loaded[name] == nil then
                    loaded[name] = assert(loadfile(directory .. "/" .. name .. ".lua", "t", environment))()
                end

                return loaded[name]
            end

            local include = environment.include
            environment.workpane.await = require("workpane.task").await
            environment.workpane.task = function(work, ...)
                require("workpane.task").run("test", "task", work, ...)
            end
            environment.workpane.future = function(work, ...)
                return require("workpane.task").future("test", work, ...)
            end
            require("workpane.lifecycle").start("test")
            environment.workpane.workspace = { open = function(options)
                return require("workpane.workspace").open("test", options, environment.workpane)
            end }
            environment.workpane.files.staging = function(path)
                return require("workpane.files").staging("test", path)
            end
            environment.workpane.files.read = function(path, maximum)
                return environment.workpane.future(function()
                    local file = environment.workpane.await(require("fs").open(path, "r"))
                    local bytes = file:read(maximum + 1):await() or ""
                    file:close():await()

                    return { bytes = bytes:sub(1, maximum), complete = #bytes <= maximum }
                end)
            end
        )";
        ScriptHarness harness;
        std::map<std::int64_t, std::filesystem::path> stages;
        std::int64_t nextStage = 0;
        // clang-format off
        harness.function("workpane_files_staging_open", [&stages, &nextStage](const json& value) {
            const auto path = std::filesystem::path(value["path"].get<std::string>());

            if (const auto created = platform::PrivateDirectoryHelper::create(path); created) {
                return scripting::HostReply::failure({"files_private_directory_failed", "The private folder could not be created", ""});
            }

            const auto id = ++nextStage;
            stages.emplace(id, path);

            return scripting::HostReply::success({{"id", id}, {"path", value["path"]}});
        });

        harness.function("workpane_files_staging_ready", [&harness](const json& value) {
            harness.replies().reply(value["request"].get<std::int64_t>(), Result<json>::success(nullptr));

            return scripting::HostReply::success();
        });

        harness.function("workpane_files_staging_close", [&harness, &stages](const json& value) {
            const auto found = stages.find(value["id"].get<std::int64_t>());

            if (found != stages.end()) {
                std::filesystem::remove_all(found->second);
                stages.erase(found);
            }

            harness.replies().reply(value["request"].get<std::int64_t>(), Result<json>::success(nullptr));

            return scripting::HostReply::success();
        });
        // clang-format on

        const auto ran = harness.run(prelude + std::string(body));
        EXPECT_TRUE(ran.hasValue()) << (ran.hasValue() ? "" : ran.error().message);
        // clang-format off
        std::ignore = harness.pollUntil([&harness]() { return !harness.results.empty(); });
        // clang-format on
        EXPECT_EQ(harness.results.size(), 1U) << json(harness.logged).dump();

        return harness.results.size() == 1 ? harness.results.front() : json();
    }

    [[nodiscard]] static json runRemote(std::string_view body) {
        return run(R"(
            local task = require("workpane.task")
            local crypto = require("crypto")
            local requests = {}
            local subscriptions = {}
            local watchers = {}
            local closes = 0
            local releases = 0
            local identity = { workspace = string.rep("a", 64), profile = "profile", session = "session", root = "C:/work", platform = "windows", name = "Remote" }
            local info = { path = "C:/work", name = "work", kind = "directory", symlink = false, size = 0, modified = "-9223372:0", recent = false, readable = true, writable = true }
            local provider = function(method)
                assert(method == "files.stat")
                return info
            end
            environment.workpane.events = { subscribe = function(topic, handler)
                assert(topic == "remote-ssh.workspace.changed")
                subscriptions[#subscriptions + 1] = handler
                return function() releases = releases + 1 end
            end }
            environment.workpane.capabilities = {
                available = function() return true end,
                watch = function(name, handler)
                    assert(name == "ssh.workspace.request")
                    watchers[#watchers + 1] = handler
                    return function() releases = releases + 1 end
                end,
                request = function(name, payload)
                    return environment.workpane.future(function()
                        if name == "ssh.workspace.open" then
                            assert(payload.profile == "profile" and payload.session == "session")
                            return identity
                        elseif name == "ssh.workspace.close" then
                            assert(payload.workspace == identity.workspace)
                            closes = closes + 1
                            return
                        end

                        assert(name == "ssh.workspace.request" and payload.workspace == identity.workspace)
                        requests[#requests + 1] = { method = payload.method, parameters = payload.parameters }
                        return provider(payload.method, payload.parameters)
                    end)
                end,
            }

            task.run("test", "test", function()
                local backend = include("remote-filesystem").open("profile", "session")
        )" + std::string(body) +
                   "\nend)\n");
    }
};

TEST_F(CodeEditorModuleTest, OpensRemoteFilesWhileDiscoveringOnlyTheLanguageTheirDocumentsUse) {
    const json results = runRemote(R"(
        local async = require("async")
        local waiting, finish = async.deferred()
        local entered, enter = async.deferred()
        local discovered, discover = async.deferred()
        local searches = 0
        loaded.catalog.servers = function()
            return {
                { language = "lua", candidates = { { executable = "lua-language-server", arguments = { "--stdio" } } } },
                { language = "python", candidates = { { executable = "pyright" } } },
            }
        end
        backend.serversChanged = discover
        provider = function(method, parameters)
            if method == "files.stat" then
                return info
            end

            assert(method == "process.find" and parameters.program == "lua-language-server")
            searches = searches + 1
            enter()
            waiting:await()
            return { found = true, path = "C:/tools/lua-language-server.exe" }
        end

        assert(searches == 0)
        assert(backend.executable("lua") == nil and backend.executable("lua") == nil)
        entered:await()
        assert(backend.searching("lua") and searches == 1)
        local inspected = environment.workpane.await(backend.stat("C:/work"))
        assert(inspected.isDir)
        finish()
        discovered:await()
        local executable = backend.executable("lua")
        assert(not backend.searching("lua"))
        environment.workpane.await(backend.close())
        host.test_result({ searches = searches, path = executable.path, arguments = executable.arguments, inspected = inspected.isDir, closes = closes })
    )");

    EXPECT_EQ(results, json({{"searches", 1}, {"path", "C:/tools/lua-language-server.exe"}, {"arguments", json::array({"--stdio"})}, {"inspected", true}, {"closes", 1}}));
}

TEST_F(CodeEditorModuleTest, DiscardsLanguageServerDiscoveryAfterItsRemoteWorkspaceCloses) {
    const json results = runRemote(R"(
        local async = require("async")
        local waiting, finish = async.deferred()
        local entered, enter = async.deferred()
        local changes = 0
        loaded.catalog.servers = function()
            return { { language = "lua", candidates = { { executable = "lua-language-server" } } } }
        end
        backend.serversChanged = function() changes = changes + 1 end
        provider = function(method)
            assert(method == "process.find")
            enter()
            waiting:await()
            return { found = true, path = "C:/tools/lua-language-server.exe" }
        end

        backend.executable("lua")
        entered:await()
        local closing = backend.close()
        finish()
        environment.workpane.await(closing)

        while backend.searching("lua") do
            async.sleep(1):await()
        end

        host.test_result({ changes = changes, closed = not backend.available, executable = backend.executable("lua") ~= nil, closes = closes })
    )");

    EXPECT_EQ(results, json({{"changes", 0}, {"closed", true}, {"executable", false}, {"closes", 1}}));
}

TEST_F(CodeEditorModuleTest, KeepsRemoteAccountContextPrivateAndInspectsOnlyDeclaredReadScopes) {
    const json result = runRemote(R"(
        local infos = 0
        provider = function(method, parameters)
            if method == "context.info" then
                infos = infos + 1
                return { account = "DOMAIN\\Reader", home = "C:/Users/Reader", repository = "C:/work", root = "C:/work" }
            end
            assert(parameters.scope == "repository" or parameters.scope == "home")
            if method == "context.read-prefix" then
                return { content = crypto.base64Encode("rules\0"), complete = true }
            elseif method == "context.canonical" then
                return { path = "C:/work/AGENTS.md" }
            elseif method == "context.stat" then
                return info
            elseif method == "context.list" then
                return { entries = {}, incomplete = false }
            end
            error("Unexpected context method")
        end
        local first = environment.workpane.await(backend.context())
        first.home = "C:/modified"
        local second = environment.workpane.await(backend.context())
        local repository = backend.inspect("repository")
        local home = backend.inspect("home")
        local read = environment.workpane.await(repository.readBounded("C:/work/AGENTS.md", 32))
        local canonical = environment.workpane.await(repository.canonical("C:/work/AGENTS.md"))
        local stat = environment.workpane.await(home.stat("C:/Users/Reader"))
        local listed = environment.workpane.await(home.list("C:/Users/Reader", { maximum = 3 }))
        local accepted, invalid = pcall(backend.inspect, "account")
        environment.workpane.await(backend.close())
        local _, closed = backend.context():await()
        local _, readClosed = repository.readBounded("C:/work/AGENTS.md", 32):await()
        host.test_result({ account = second.account, home = second.home, infos = infos, binary = read.bytes == "rules\0", canonical = canonical, directory = stat.isDir, complete = listed.complete, write = repository.save == nil and home.writeFile == nil, invalid = not accepted and invalid.code, closed = closed.code, readClosed = readClosed.code })
    )");

    EXPECT_EQ(result["account"], "DOMAIN\\Reader");
    EXPECT_EQ(result["home"], "C:/Users/Reader");
    EXPECT_EQ(result["infos"], 1);
    EXPECT_EQ(result["binary"], true);
    EXPECT_EQ(result["canonical"], "C:/work/AGENTS.md");
    EXPECT_EQ(result["directory"], true);
    EXPECT_EQ(result["complete"], true);
    EXPECT_EQ(result["write"], true);
    EXPECT_EQ(result["invalid"], "workspace_request_invalid");
    EXPECT_EQ(result["closed"], "workspace_closed");
    EXPECT_EQ(result["readClosed"], "workspace_closed");
}

TEST_F(CodeEditorModuleTest, RefusesMalformedRemoteAccountContextWithoutCachingIt) {
    const json result = runRemote(R"(
        local replies = {
            { account = "Reader", home = "relative", repository = "C:/work", root = "C:/work" },
            { account = "Reader", home = "C:/Users/Reader", repository = "C:/other", root = "C:/work" },
            { account = "Reader", home = "C:/Users/Reader", repository = "C:/work", root = "C:/changed" },
            { account = "Reader", home = "C:/Users/Reader", repository = "C:/work", root = "C:/work", variables = { SECRET = "private" } },
            { account = "Reader\0hidden", home = "C:/Users/Reader", repository = "C:/work", root = "C:/work" },
        }

        local codes = {}
        for _, reply in ipairs(replies) do
            provider = function(method)
                assert(method == "context.info")
                return reply
            end
            local _, problem = backend.context():await()
            codes[#codes + 1] = problem.code
        end
        environment.workpane.await(backend.close())
        host.test_result({ codes = codes })
    )");

    EXPECT_EQ(result["codes"], json::array({"workspace_response_invalid", "workspace_response_invalid", "workspace_response_invalid", "workspace_response_invalid", "workspace_response_invalid"}));
}

TEST_F(CodeEditorModuleTest, ReadsRemoteBinaryFilesAndKeepsRevisionAndPlatformIdentity) {
    const json results = runRemote(R"(
        local binary = "first\0" .. string.char(255) .. "last"
        local revision = string.rep("b", 64)
        provider = function(method, parameters)
            if method == "files.read" then
                return { path = parameters.path, content = crypto.base64Encode(binary), revision = revision }
            elseif method == "files.write" then
                assert(parameters.revision == revision and crypto.base64Decode(parameters.content) == binary)
                return { path = parameters.path, revision = string.rep("c", 64) }
            elseif method == "files.list" then
                assert(parameters.maximum == 2000)
                return { entries = { { path = "C:/work/link", name = "link", kind = "directory", symlink = true, size = 0, modified = "-4:0", recent = false, readable = true, writable = true } }, incomplete = true }
            elseif method == "files.walk" then
                assert(parameters.maximum == 20000 and parameters.skip[1] == ".git")
                return { paths = { "src/main.cpp" }, complete = false }
            elseif method == "files.search" then
                assert(parameters.maximum == 1000 and parameters.maximumFileBytes == 4 * 1024 * 1024)
                return { matches = { { path = "src/main.cpp", line = 3, text = "match" } }, complete = false }
            end
            error("Unexpected remote request")
        end
        local read = environment.workpane.await(backend.read("C:/work/binary"))
        local saved = environment.workpane.await(backend.save("C:/work/binary", read.bytes, read.revision))
        local listing = environment.workpane.await(backend.list("C:/work", { maximum = 20000 }))
        local walked = environment.workpane.await(backend.walk("C:/work", { maximum = 30000, skip = { ".git" } }))
        local searched = environment.workpane.await(backend.search("C:/work", { text = "match", maximumMatches = 5000, maximumFileBytes = 16 * 1024 * 1024, skip = { ".git" } }))
        local _, refused = backend.save("C:/work/binary", binary, nil):await()
        local before = #requests
        local first = backend.close()
        local second = backend.close()
        environment.workpane.await(first)
        local _, closed = backend.read("C:/work/binary"):await()
        host.test_result({ binary = read.bytes == binary, revision = read.revision, saved = saved.revision, uri = backend.uri("C:/work/binary"), path = backend.path("file:///C:/work/binary"), listing = listing, walked = walked, searched = searched, refused = refused.code, closed = closed.code, requests = #requests == before, sameClose = first == second, closes = closes, releases = releases })
    )");

    EXPECT_EQ(results["binary"], true);
    EXPECT_EQ(results["revision"], std::string(64, 'b'));
    EXPECT_EQ(results["saved"], std::string(64, 'c'));
    EXPECT_EQ(results["uri"], "file:///C:/work/binary");
    EXPECT_EQ(results["path"], "C:/work/binary");
    EXPECT_EQ(results["listing"]["entries"][0]["kind"], "symlink");
    EXPECT_EQ(results["listing"]["complete"], false);
    EXPECT_EQ(results["walked"]["complete"], false);
    EXPECT_EQ(results["searched"]["complete"], false);
    EXPECT_EQ(results["refused"], "workspace_write_invalid");
    EXPECT_EQ(results["closed"], "workspace_closed");
    EXPECT_EQ(results["requests"], true);
    EXPECT_EQ(results["sameClose"], true);
    EXPECT_EQ(results["closes"], 1);
    EXPECT_EQ(results["releases"], 2);
}

TEST_F(CodeEditorModuleTest, ReadsBoundedRemotePrefixesAndRefusesMalformedBinaryReplies) {
    const json results = runRemote(R"(
        local binary = "a\0" .. string.char(255)
        local reply = { content = crypto.base64Encode(binary), complete = false }
        provider = function(method, parameters)
            assert(method == "files.read-prefix" and parameters.maximum == 3)
            return reply
        end
        local read = environment.workpane.await(backend.readBounded("C:/work/binary", 3))
        local codes = {}

        for index, malformed in ipairs({ { content = "YQ=", complete = true }, { content = crypto.base64Encode("long"), complete = false }, { content = "YQ==", complete = "yes" }, { content = "YQ==", complete = false } }) do
            reply = malformed
            local _, failure = backend.readBounded("C:/work/binary", 3):await()
            codes[index] = failure.code
        end

        environment.workpane.await(backend.close())
        host.test_result({ binary = read.bytes == binary, complete = read.complete, codes = codes })
    )");

    EXPECT_EQ(results["binary"], true);
    EXPECT_EQ(results["complete"], false);
    EXPECT_EQ(results["codes"], json::array({"workspace_response_invalid", "workspace_response_invalid", "workspace_response_invalid", "workspace_response_invalid"}));
}

TEST_F(CodeEditorModuleTest, RefusesMalformedRemoteFileRepliesAndLateReadsAfterDisconnect) {
    const json results = runRemote(R"(
        local malformed = {
            { path = "C:/work/file", content = "YQ=", revision = string.rep("b", 64) },
            { path = "C:/work/file", content = "YQ==", revision = "bad" },
            { path = "C:/work/file", content = "YQ==", revision = string.rep("b", 64), extra = true },
        }

        local failures = {}

        for index, result in ipairs(malformed) do
            provider = function() return result end
            local _, problem = backend.read("C:/work/file"):await()
            failures[index] = problem.code
        end

        local waiting, finish = require("async").deferred()
        provider = function()
            waiting:await()
            return { path = "C:/work/file", content = "YQ==", revision = string.rep("b", 64) }
        end
        local pending = backend.read("C:/work/file")
        subscriptions[1]({ workspace = string.rep("d", 64), connected = false })
        local unrelated = backend.available
        subscriptions[1]({ workspace = identity.workspace, connected = false })
        watchers[1](false)
        finish()
        local _, problem = pending:await()
        environment.workpane.await(backend.close())
        host.test_result({ malformed = failures, unrelated = unrelated, available = backend.available, late = problem.code, releases = releases, closes = closes })
    )");

    EXPECT_EQ(results["malformed"], json::array({"workspace_response_invalid", "workspace_response_invalid", "workspace_response_invalid"}));
    EXPECT_EQ(results["unrelated"], true);
    EXPECT_EQ(results["available"], false);
    EXPECT_EQ(results["late"], "workspace_closed");
    EXPECT_EQ(results["releases"], 2);
    EXPECT_EQ(results["closes"], 1);
}

TEST_F(CodeEditorModuleTest, OrdersRemoteProgramInputAndDeliversItsOutputBeforeExit) {
    const json results = runRemote(R"(
        local async = require("async")
        local waiting, deliver = async.deferred()
        local entered = false
        local input = {}
        local events = {}
        local ended = false
        provider = function(method, parameters)
            if method == "process.start" then
                return { process = 4 }
            elseif method == "process.write" then
                assert(parameters.process == 4)
                local bytes = crypto.base64Decode(parameters.content)
                input[#input + 1] = bytes

                if #input == 1 then
                    entered = true
                    waiting:await()
                end

                return {}
            elseif method == "process.read" then
                if #input < 3 then
                    return { chunks = {}, ended = false, failed = false, code = 0 }
                end

                return { chunks = { { stream = "stdout", content = crypto.base64Encode("first\0last") }, { stream = "stderr", content = crypto.base64Encode("warning") } }, ended = true, failed = true, code = 7 }
            end

            error("Unexpected program request")
        end
        local operation = environment.workpane.await(backend.process.start({ program = "C:/server", arguments = {}, directory = "C:/work", onOutput = function(chunks)
            for _, chunk in ipairs(chunks) do
                events[#events + 1] = chunk.stream .. ":" .. chunk.text
            end
        end, onExit = function(code)
            events[#events + 1] = "exit:" .. code
            ended = true
        end }))
        backend.process.write(operation, string.rep("a", 100000))

        while not entered do
            async.sleep(5):await()
        end

        backend.process.write(operation, "tail")
        local oversized, limit = pcall(backend.process.write, operation, string.rep("b", 16 * 1024 * 1024))
        deliver()

        while not ended do
            async.sleep(5):await()
        end

        local written, retired = pcall(backend.process.write, operation, "late")
        environment.workpane.await(backend.close())
        host.test_result({ events = events, sizes = { #input[1], #input[2], #input[3] }, input = table.concat(input) == string.rep("a", 100000) .. "tail", oversized = oversized, limit = limit.code, written = written, retired = retired.code })
    )");

    EXPECT_EQ(results["events"], json::array({std::string("output:first\0last", 17), "error:warning", "exit:7"}));
    EXPECT_EQ(results["sizes"], json::array({65536, 34464, 4}));
    EXPECT_EQ(results["input"], true);
    EXPECT_EQ(results["oversized"], false);
    EXPECT_EQ(results["limit"], "remote_input_refused");
    EXPECT_EQ(results["written"], false);
    EXPECT_EQ(results["retired"], "remote_process_missing");
}

TEST_F(CodeEditorModuleTest, RejectsAnAcceptedRemoteProgramStartAfterItsConnectionCloses) {
    const json results = runRemote(R"(
        local async = require("async")
        local waiting, finish = async.deferred()
        local accepted = false
        local exits = 0
        provider = function(method)
            assert(method == "process.start")
            accepted = true
            waiting:await()

            return { process = 4 }
        end
        local pending = backend.process.start({ program = "C:/server", arguments = {}, directory = "C:/work", onOutput = function() error("A closed connection delivers no output") end, onExit = function() exits = exits + 1 end })

        while not accepted do
            async.sleep(5):await()
        end

        subscriptions[1]({ workspace = identity.workspace, connected = false })
        finish()
        local _, problem = pending:await()
        environment.workpane.await(backend.close())
        async.sleep(5):await()
        host.test_result({ code = problem.code, exits = exits, requests = #requests, available = backend.available })
    )");

    EXPECT_EQ(results["code"], "workspace_closed");
    EXPECT_EQ(results["exits"], 0);
    EXPECT_EQ(results["requests"], 2);
    EXPECT_EQ(results["available"], false);
}

TEST_F(CodeEditorModuleTest, RetainsTheTransportFailureWhenAProgramStreamEndsWithoutAnExitReply) {
    const json results = runRemote(R"(
        local async = require("async")
        local exit
        provider = function(method)
            if method == "process.start" then
                return { process = 7 }
            end

            if method == "process.read" then
                error({ code = "ssh_transport_failed", message = "Private transport detail", detail = "Private prompt" }, 0)
            end

            assert(method == "process.stop")
            return {}
        end
        environment.workpane.await(backend.process.start({ program = "C:/server", arguments = {}, directory = "C:/work", onOutput = function() error("A failed stream has no output") end, onExit = function(code, unsuccessful, problem)
            exit = { code = code, failed = unsuccessful, cause = problem.code }
        end }))

        while exit == nil do
            async.sleep(1):await()
        end

        host.test_result({ exit = exit, stopped = requests[#requests].method })
    )");

    EXPECT_EQ(results["exit"], json({{"code", -1}, {"failed", true}, {"cause", "ssh_transport_failed"}}));
    EXPECT_EQ(results["stopped"], "process.stop");
}

TEST_F(CodeEditorModuleTest, FreezesRemoteProgramArgumentsAndEnvironmentAndDeliversBinaryInputAndCrash) {
    const json results = runRemote(R"(
        local async = require("async")
        local binary = "input\0" .. string.char(255, 128)
        local launched
        local ended = false
        local events = {}
        provider = function(method, parameters)
            if method == "process.start" then
                launched = parameters
                return { process = 9 }
            elseif method == "process.read" then
                return { chunks = { { stream = "stdout", content = crypto.base64Encode(binary) } }, ended = true, failed = true, code = -1 }
            end
            error("Unexpected remote request")
        end
        local options = { program = "C:/agent.exe", directory = "C:/work", arguments = { "original", "" }, variables = { SECRET = "original", EMPTY = "" }, cleared = { "OTHER" }, input = binary, onOutput = function(chunks)
            events[#events + 1] = chunks[1].text == binary and "binary" or "invalid"
        end, onExit = function(code, unsuccessful)
            events[#events + 1] = "exit"
            ended = code == -1 and unsuccessful
        end }
        local pending = backend.process.start(options)
        options.arguments[1] = "changed"
        options.variables.SECRET = "changed"
        options.cleared[1] = "changed"
        options.input = "changed"
        environment.workpane.await(pending)

        while not ended do
            async.sleep(5):await()
        end

        environment.workpane.await(backend.close())
        host.test_result({ arguments = launched.arguments, variables = launched.variables, cleared = launched.cleared, binary = crypto.base64Decode(launched.input) == binary, events = events })
    )");

    EXPECT_EQ(results["arguments"], json::array({"original", ""}));
    EXPECT_EQ(results["variables"]["SECRET"], "original");
    EXPECT_EQ(results["variables"]["EMPTY"], "");
    EXPECT_EQ(results["cleared"], json::array({"OTHER"}));
    EXPECT_EQ(results["binary"], true);
    EXPECT_EQ(results["events"], json::array({"binary", "exit"}));
}

TEST_F(CodeEditorModuleTest, RefusesInvalidRemoteTextBeforeItCrossesTheWorkspaceTransport) {
    const json results = runRemote(R"(
        local failures = {}
        local before = #requests
        local function refused(work)
            local accepted, problem = pcall(work)
            assert(not accepted)
            failures[#failures + 1] = problem.code
        end
        local function options()
            return { program = "C:/agent.exe", directory = "C:/work", onOutput = function() end, onExit = function() end }
        end

        for _, invalid in ipairs({ "name\0suffix", string.char(255) }) do
            refused(function() backend.read(invalid) end)
            refused(function() backend.process.find(invalid, {}) end)
            refused(function()
                local launch = options()
                launch.arguments = { invalid }
                backend.process.start(launch)
            end)
            refused(function()
                local launch = options()
                launch.variables = { NAME = invalid }
                backend.process.start(launch)
            end)
        end

        refused(function() backend.process.find("agent", { "relative" }) end)
        refused(function()
            local launch = options()
            launch.directory = "relative"
            backend.process.start(launch)
        end)
        refused(function() backend.list("C:/work", { maximum = 1.5 }) end)
        refused(function() backend.walk("C:/work", { maximum = 10, skip = { [2] = ".git" } }) end)
        refused(function() backend.search("C:/work", { maximumMatches = 10, maximumFileBytes = 100, text = "a\0b", skip = {} }) end)
        refused(function() backend.request("files.stat", { path = "C:/work", extra = { [true] = "invalid" } }) end)
        environment.workpane.await(backend.close())
        host.test_result({ failures = failures, unchanged = before == #requests })
    )");

    ASSERT_EQ(results["failures"].size(), 14U);
    EXPECT_EQ(results["unchanged"], true);

    for (const auto& code : results["failures"]) {
        EXPECT_TRUE(code == "workspace_request_invalid" || code == "workspace_process_invalid");
    }
}

TEST_F(CodeEditorModuleTest, UsesRemoteProgramSearchDirectoriesAndFreezesTraversalOptions) {
    const json results = runRemote(R"(
        provider = function(method, parameters)
            if method == "process.find" then
                assert(parameters.program == "agent" and parameters.directories[1] == "C:/tools")
                return { found = true, path = "C:/tools/agent.exe" }
            elseif method == "files.walk" then
                assert(parameters.maximum == 5 and parameters.skip[1] == ".git")
                return { complete = true, paths = {} }
            elseif method == "files.search" then
                assert(parameters.text == "original" and parameters.maximum == 7 and parameters.maximumFileBytes == 100 and parameters.skip[1] == ".git")
                return { complete = true, matches = {} }
            end
            error("Unexpected remote request")
        end
        local directories = { "C:/tools" }
        local finding = backend.process.find("agent", directories)
        directories[1] = "C:/changed"
        local walked = { maximum = 5, skip = { ".git" } }
        local walking = backend.walk("C:/work", walked)
        walked.maximum = 100
        walked.skip[1] = "changed"
        local searched = { text = "original", maximumMatches = 7, maximumFileBytes = 100, skip = { ".git" } }
        local searching = backend.search("C:/work", searched)
        searched.text = "changed"
        searched.maximumMatches = 100
        searched.maximumFileBytes = 1000
        searched.skip[1] = "changed"
        local found = environment.workpane.await(finding)
        environment.workpane.await(walking)
        environment.workpane.await(searching)
        environment.workpane.await(backend.close())
        host.test_result({ found = found })
    )");

    EXPECT_EQ(results["found"], "C:/tools/agent.exe");
}

TEST_F(CodeEditorModuleTest, RefusesMalformedRemoteExecutableDiscoveryReplies) {
    const json results = runRemote(R"(
        local codes = {}
        local answers = {
            { found = true, path = "relative" },
            { found = false, path = "C:/unexpected" },
            { found = true, path = "C:/" .. string.rep("a", 4096) },
            { found = true, path = "C:/" .. string.char(255) },
            { found = true, path = "C:/agent.exe", extra = true },
        }

        for index, answer in ipairs(answers) do
            provider = function(method)
                assert(method == "process.find")
                return answer
            end
            local _, problem = backend.process.find("agent"):await()
            codes[index] = problem.code
        end

        environment.workpane.await(backend.close())
        host.test_result({ codes = codes })
    )");

    EXPECT_EQ(results["codes"], json::array({"workspace_response_invalid", "workspace_response_invalid", "workspace_response_invalid", "workspace_response_invalid", "workspace_response_invalid"}));
}

TEST_F(CodeEditorModuleTest, RefusesInvalidWorkspaceEndpointsBeforeOpeningAProviderScope) {
    const json results = runRemote(R"(
        local before = #requests
        local failures = {}
        local function endpoint()
            return { open = "ssh.workspace.open", request = "ssh.workspace.request", close = "ssh.workspace.close", changed = "remote-ssh.workspace.changed", profile = "profile", session = "session", closedCodes = { "ssh_connection_closed" } }
        end
        local mutations = {
            function(value) value.extra = true end,
            function(value) value.profile = "profile\0suffix" end,
            function(value) value.session = string.char(255) end,
            function(value) value.open = "invalid" end,
            function(value) value.closedCodes = { [2] = "ssh_connection_closed" } end,
            function(value) value.closedCodes = { string.char(255) } end,
        }

        for index, mutate in ipairs(mutations) do
            local value = endpoint()
            mutate(value)
            local accepted, problem = pcall(environment.workpane.workspace.open, value)
            assert(not accepted)
            failures[index] = problem.code
        end

        environment.workpane.await(backend.close())
        host.test_result({ failures = failures, unchanged = #requests == before, closes = closes })
    )");

    EXPECT_EQ(results["failures"], json::array({"workspace_endpoint_invalid", "workspace_endpoint_invalid", "workspace_endpoint_invalid", "workspace_endpoint_invalid", "workspace_endpoint_invalid", "workspace_endpoint_invalid"}));
    EXPECT_EQ(results["unchanged"], true);
    EXPECT_EQ(results["closes"], 1);
}

TEST_F(CodeEditorModuleTest, DistinguishesAbsentAndEmptyRemoteProgramInput) {
    const json results = runRemote(R"(
        local async = require("async")
        local inputs = {}
        local ended = 0
        provider = function(method, parameters)
            if method == "process.start" then
                inputs[#inputs + 1] = parameters.input == nil and "absent" or (parameters.input == "" and "empty" or "invalid")
                return { process = #inputs }
            elseif method == "process.read" then
                return { chunks = {}, ended = true, failed = false, code = 0 }
            end
            error("Unexpected remote request")
        end

        for _, input in ipairs({ false, "" }) do
            environment.workpane.await(backend.process.start({ program = "C:/agent.exe", directory = "C:/work", input = input ~= false and input or nil, onOutput = function() end, onExit = function()
                ended = ended + 1
            end }))
        end

        while ended ~= 2 do
            async.sleep(5):await()
        end

        environment.workpane.await(backend.close())
        host.test_result({ inputs = inputs })
    )");

    EXPECT_EQ(results["inputs"], json::array({"absent", "empty"}));
}

TEST_F(CodeEditorModuleTest, ReleasesReservedRemoteScopesWhenTheirIdentityOrRootIsInvalid) {
    const json results = runRemote(R"(
        local codes = {}
        identity.root = "relative"
        local _, malformed = environment.workpane.workspace.open({ open = "ssh.workspace.open", request = "ssh.workspace.request", close = "ssh.workspace.close", changed = "remote-ssh.workspace.changed", profile = "profile", session = "session", closedCodes = {} }):await()
        codes[1] = malformed.code
        identity.root = "C:/work"
        provider = function()
            local value = {}
            for key, entry in pairs(info) do value[key] = entry end
            value.kind = "file"
            return value
        end
        local _, root = environment.workpane.workspace.open({ open = "ssh.workspace.open", request = "ssh.workspace.request", close = "ssh.workspace.close", changed = "remote-ssh.workspace.changed", profile = "profile", session = "session", closedCodes = {} }):await()
        codes[2] = root.code
        environment.workpane.await(backend.close())
        host.test_result({ codes = codes, closes = closes, releases = releases })
    )");

    EXPECT_EQ(results["codes"], json::array({"workspace_response_invalid", "workspace_root_invalid"}));
    EXPECT_EQ(results["closes"], 3);
    EXPECT_EQ(results["releases"], 4);
}

TEST_F(CodeEditorModuleTest, DetectsAnExternalEditThatCrossesSaveCompletionWithoutLosingLaterText) {
    const json result = runRemote(R"lua(
        environment.workpane.plugin = { directory = directory }
        local notifications = 0
        local reads = 0
        environment.workpane.notify = { error = function() notifications = notifications + 1 end }
        loaded.preferences = { get = function(key) return key == "defaultCharset" and "utf-8" or false end }
        include("catalog").load()
        local handlers = { changed = function() end, edited = function() end, saved = function() end }
        local document = include("documents").new({ root = "C:/work", files = backend }, "C:/work/file.txt", nil, handlers)
        document.loaded = true
        document.text = "Saved text\n"
        document.savedText = "Original text\n"
        document.digest = include("encoding").digest(document.savedText)
        document.revision = string.rep("a", 64)
        document.stamp = { size = #document.savedText, mtime = "original" }
        document.dirty = true
        provider = function(method, parameters)
            if method == "files.write" then
                assert(crypto.base64Decode(parameters.content) == "Saved text\n")
                document.text = "Later unsaved text\n"
                return { path = parameters.path, revision = string.rep("b", 64) }
            elseif method == "files.stat" then
                return { path = parameters.path, name = "file.txt", kind = "file", symlink = false, size = 9, modified = "external", recent = false, readable = true, writable = true }
            elseif method == "files.read" then
                reads = reads + 1
                return { path = parameters.path, content = crypto.base64Encode("External\n"), revision = string.rep("c", 64) }
            end

            error("Unexpected remote request")
        end

        document:save()
        document:poll()
        environment.workpane.await(backend.close())
        host.test_result({ text = document.text, saved = document.savedText, dirty = document.dirty, notifications = notifications, reads = reads, revision = document.revision })
    )lua");

    EXPECT_EQ(result["text"], "Later unsaved text\n");
    EXPECT_EQ(result["saved"], "Saved text\n");
    EXPECT_EQ(result["dirty"], true);
    EXPECT_EQ(result["notifications"], 1);
    EXPECT_EQ(result["reads"], 1);
    EXPECT_EQ(result["revision"], std::string(64, 'b'));
}

TEST_F(CodeEditorModuleTest, KeepsACleanRemoteDocumentWhenDisconnectionCrossesItsPoll) {
    const json results = runRemote(R"(
        local async = require("async")
        local waiting, finish = async.deferred()
        local accepted = false
        local removed = 0
        environment.workpane.plugin = { directory = directory }
        include("catalog").load()
        local document = include("documents").new({ root = "C:/work", files = backend }, "C:/work/file.txt", nil, { removed = function() removed = removed + 1 end })
        document.loaded = true
        document.text = "preserved"
        document.savedText = "preserved"
        provider = function(method)
            assert(method == "files.stat")
            accepted = true
            waiting:await()
            error({ code = "ssh_connection_closed", message = "Closed", detail = "" }, 0)
        end
        local polling = environment.workpane.future(function() document:poll() end)

        while not accepted do
            async.sleep(5):await()
        end

        subscriptions[1]({ workspace = identity.workspace, connected = false })
        finish()
        environment.workpane.await(polling)
        document:poll()
        environment.workpane.await(backend.close())
        host.test_result({ removed = removed, text = document.text, closed = document.closed, requests = #requests })
    )");

    EXPECT_EQ(results["removed"], 0);
    EXPECT_EQ(results["text"], "preserved");
    EXPECT_EQ(results["closed"], false);
    EXPECT_EQ(results["requests"], 2);
}

TEST_F(CodeEditorModuleTest, KeepsARemoteDocumentAfterAMetadataTimeoutAndClosesOnlyAMissingFile) {
    const json results = runRemote(R"(
        environment.workpane.plugin = { directory = directory }
        include("catalog").load()
        local removed = 0
        local document = include("documents").new({ root = "C:/work", files = backend }, "C:/work/file.txt", nil, { removed = function() removed = removed + 1 end })
        document.loaded = true
        document.text = "preserved"
        document.savedText = "preserved"
        document.stamp = { size = 9, mtime = "unchanged" }
        provider = function()
            error({ code = "ssh_request_timeout", message = "The metadata request timed out", detail = "" }, 0)
        end
        document:poll()
        local afterTimeout = removed
        provider = function()
            error({ code = "remote_path_inspection_failed", message = "The file cannot be inspected", detail = "" }, 0)
        end
        document:poll()
        local afterInspectionFailure = removed
        provider = function()
            return { path = "C:/work/file.txt", name = "file.txt", kind = "file", symlink = false, size = 9, modified = "unchanged", recent = false, readable = true, writable = true }
        end
        document:poll()
        local afterRecovery = removed
        provider = function()
            error({ code = "remote_path_unavailable", message = "The file no longer exists", detail = "" }, 0)
        end
        document:poll()
        environment.workpane.await(backend.close())
        host.test_result({ afterTimeout = afterTimeout, afterInspectionFailure = afterInspectionFailure, afterRecovery = afterRecovery, afterRemoval = removed, text = document.text, requests = #requests })
    )");

    EXPECT_EQ(results["afterTimeout"], 0);
    EXPECT_EQ(results["afterInspectionFailure"], 0);
    EXPECT_EQ(results["afterRecovery"], 0);
    EXPECT_EQ(results["afterRemoval"], 1);
    EXPECT_EQ(results["text"], "preserved");
    EXPECT_EQ(results["requests"], 5);
}

TEST_F(CodeEditorModuleTest, UsesSdkMetadataForNativeDocumentsAndRefusesSavingAfterInspectionFailure) {
    const json results = run(R"(
        environment.workpane.plugin = { directory = directory }

        require("workpane.task").run("test", "task", function()
            include("catalog").load()
            local calls = 0
            local staging = 0
            local metadataFailure = { code = "files_metadata_unavailable", message = "The path cannot be inspected", detail = "" }
            environment.workpane.files.stat = function()
                calls = calls + 1
                return environment.workpane.future(function()
                    error(metadataFailure, 0)
                end)
            end
            environment.workpane.files.staging = function()
                staging = staging + 1
            end

            local files = include("filesystem").native()
            local _, failure = files.stat("/folder/file.txt"):await()
            local _, refused = files.save("/folder/file.txt", "changed"):await()
            local document = include("documents").new({ files = files }, "/folder/file.txt", nil, {})
            local read, readFailure = document:read(true)
            environment.workpane.files.stat = function()
                calls = calls + 1
                return environment.workpane.future(function() return nil end)
            end

            local missing, missingFailure = files.stat("/folder/missing.txt"):await()
            local absent, absentFailure = document:read(true)
            host.test_result({ code = failure ~= nil and failure.code, refused = refused ~= nil and refused.code, calls = calls, staging = staging, missing = missing == nil, missingFailure = missingFailure ~= nil, read = read, readFailure = readFailure, absent = absent, absentFailure = absentFailure })
        end)
    )");

    EXPECT_EQ(results["code"], "files_metadata_unavailable");
    EXPECT_EQ(results["refused"], "files_metadata_unavailable");
    EXPECT_EQ(results["calls"], 5);
    EXPECT_EQ(results["staging"], 0);
    EXPECT_EQ(results["missing"], true);
    EXPECT_EQ(results["missingFailure"], false);
    EXPECT_EQ(results["read"], false);
    EXPECT_EQ(results["readFailure"], "code-editor.error.read-failed");
    EXPECT_EQ(results["absent"], false);
    EXPECT_EQ(results["absentFailure"], "code-editor.error.file-missing");
}

TEST_F(CodeEditorModuleTest, ReceivesRemoteLanguageDiscoveryBeforeItsWorkspaceIsFirstPresented) {
    const json results = run(R"(
        local refreshed = 0
        local capabilities = 0
        environment.workpane.ui = setmetatable({}, { __index = function()
            return function(properties)
                return { properties = properties, set = function() end }
            end
        end })
        loaded.preferences = { get = function() return false end }
        loaded["views/tree"] = { new = function() return { node = {} } end }
        loaded["views/panels"] = { new = function() return { nodes = { symbolPanel = {}, bottom = {} } } end }
        loaded.analysis = { new = function()
            return { refresh = function(_, documents)
                assert(#documents == 0)
                refreshed = refreshed + 1
            end }
        end }
        local backend = { remote = true, identity = { root = "/remote", name = "Remote" }, serversChanged = function() end }
        local opened = include("views/workspace").new({ id = "workspace", root = "/remote" }, {}, {}, backend)
        opened.refreshCapabilities = function() capabilities = capabilities + 1 end
        backend.serversChanged()
        opened.closed = true
        backend.serversChanged()
        host.test_result({ refreshed = refreshed, capabilities = capabilities })
    )");

    EXPECT_EQ(results, json({{"refreshed", 1}, {"capabilities", 1}}));
}

TEST_F(CodeEditorModuleTest, ShowsAndCancelsRemoteOpeningBeforeItsAcceptedBackendArrives) {
    const json results = run(R"(
        local async = require("async")
        local waiting, finish = async.deferred()
        local entered, enter = async.deferred()
        local nodes = {}
        local closed = 0
        local created = 0
        local navigated = 0
        loaded.store = { save = function() end }
        loaded["views/workspace"] = { new = function() created = created + 1 end }
        environment.workpane.shell = { navigate = function() navigated = navigated + 1 end }
        environment.workpane.ui = setmetatable({}, { __index = function(_, kind)
            return function(properties, children)
                local node = { kind = kind, properties = properties, children = children or {} }

                function node:set(patch)
                    for key, value in pairs(patch) do self.properties[key] = value end
                end

                function node:setChildren(children, patch)
                    self.children = children
                    self:set(patch)
                end

                nodes[#nodes + 1] = node

                return node
            end
        end })
        loaded["remote-filesystem"] = { open = function()
            enter()
            waiting:await()

            return { identity = { root = "/remote" }, close = function()
                return environment.workpane.future(function() closed = closed + 1 end)
            end }
        end }
        require("workpane.task").run("test", "test", function()
            local view = include("view")
            view.build()
            local opening = environment.workpane.future(function()
                return view.remoteRequested({ profile = "profile", session = "session" })
            end)
            entered:await()
            local shown = false
            local cancel

            for _, node in ipairs(nodes) do
                shown = shown or node.kind == "label" and node.properties.text == "code-editor.remote.opening"

                if node.kind == "button" and node.properties.text == "code-editor.actions.cancel" then
                    cancel = node.properties.onClick
                end
            end

            assert(shown and cancel ~= nil and navigated == 1)
            cancel()
            finish()
            local _, failure = opening:await()
            view.stop()
            host.test_result({ shown = shown, closed = closed, created = created, code = failure.code })
        end)
    )");

    EXPECT_EQ(results, json({{"shown", true}, {"closed", 1}, {"created", 0}, {"code", "code_editor_open_cancelled"}}));
}

TEST_F(CodeEditorModuleTest, ClosesRemotePreparationThatFinishesAfterTheEditorStartsStopping) {
    const json results = run(R"(
        local async = require("async")
        local waiting, finish = async.deferred()
        local preparing = false
        local closed = 0
        local created = 0
        loaded.store = { save = function(records) assert(#records == 0) end }
        loaded.workspace = { new = function() created = created + 1 end }
        environment.workpane.shell = { navigate = function() end }
        loaded["remote-filesystem"] = { open = function()
            preparing = true
            waiting:await()

            return { close = function()
                return environment.workpane.future(function() closed = closed + 1 end)
            end, identity = { root = "/remote" } }
        end }
        require("workpane.task").run("test", "test", function()
            local view = include("view")
            local pending = environment.workpane.future(function()
                return view.remoteRequested({ profile = "profile", session = "session" })
            end)

            while not preparing do
                async.sleep(1):await()
            end

            view.stop()
            finish()
            local _, problem = pending:await()
            local _, stopped = pcall(view.remoteRequested, { profile = "profile", session = "session" })
            host.test_result({ closed = closed, created = created, late = problem.code, stopped = stopped.code })
        end)
    )");

    EXPECT_EQ(results["closed"], 1);
    EXPECT_EQ(results["created"], 0);
    EXPECT_EQ(results["late"], "code_editor_stopping");
    EXPECT_EQ(results["stopped"], "code_editor_stopping");
}

// Sections follow the EditorConfig globs: stars stop at separators, double stars cross them or stand for no folder, classes, alternatives, ranges and escapes read as the specification says, and braces without a comma are literal.
TEST_F(CodeEditorModuleTest, MatchesEditorConfigSections) {
    const json results = run(R"(
        local editorconfig = include("editorconfig")
        local cases = {
            { "*", "a.txt" }, { "*", "dir/a.txt" }, { "*.txt", "a.lua" }, { "*.{js,py}", "src/x.py" }, { "*.{js,py}", "x.rb" },
            { "/root.txt", "root.txt" }, { "/root.txt", "dir/root.txt" }, { "lib/**.js", "lib/a/b/c.js" }, { "lib/*.js", "lib/a/b.js" },
            { "a/**/b.txt", "a/x/y/b.txt" }, { "[abc].txt", "b.txt" }, { "[!abc].txt", "b.txt" }, { "[!abc].txt", "d.txt" },
            { "file{1..3}.txt", "file2.txt" }, { "file{1..3}.txt", "file4.txt" }, { "\\*.txt", "*.txt" }, { "\\*.txt", "a.txt" },
            { "?.txt", "a.txt" }, { "?.txt", "ab.txt" }, { "{a,{b,c}}.txt", "c.txt" }, { "{a,b.txt", "{a,b.txt" },
            { "a/**/b.txt", "a/b.txt" }, { "{single}.txt", "{single}.txt" }, { "{single}.txt", "single.txt" }, { "{}.txt", "{}.txt" },
        }

        local answers = {}

        for index, case in ipairs(cases) do
            answers[index] = editorconfig.matches(case[1], case[2])
        end

        host.test_result({ answers = answers })
    )");

    EXPECT_EQ(results["answers"], json::array({true, true, false, true, false, true, false, true, false, true, true, false, true, true, false, true, false, true, false, true, true, true, true, false, true}));
}

// An indent size of tab takes the width of a tab, four when none is set, and every width stays within the one to sixteen columns the editor draws.
TEST_F(CodeEditorModuleTest, ResolvesIndentWidthsTheEditorDraws) {
    TemporaryDirectory folder;
    const std::map<std::string, std::string> files{{"tab", "root = true\n[*]\nindent_size = tab\n"}, {"tabbed", "root = true\n[*]\nindent_style = tab\nindent_size = tab\ntab_width = 3\n"}, {"wide", "root = true\n[*]\nindent_style = space\nindent_size = 40\n"}, {"narrow", "root = true\n[*]\nindent_style = tab\ntab_width = 99\n"}};

    for (const auto& [name, text] : files) {
        std::filesystem::create_directories(folder.path() / name);
        std::ofstream(folder.path() / name / ".editorconfig") << text;
    }

    const json results = run(R"(
        local root = [==[)" + folder.path().generic_string() +
                             R"(]==]
        require("workpane.task").run("test", "task", function()
            environment.workpane.plugin = { directory = directory }
            include("catalog").load()
            local editorconfig = include("editorconfig")
            local widths = {}
            for _, name in ipairs({ "tab", "tabbed", "wide", "narrow" }) do
                widths[name] = editorconfig.resolve(root .. "/" .. name .. "/a.txt", root .. "/" .. name, include("filesystem").native()).indentWidth
            end
            host.test_result(widths)
        end)
    )");

    EXPECT_EQ(results["tab"], 4);
    EXPECT_EQ(results["tabbed"], 3);
    EXPECT_EQ(results["wide"], 16);
    EXPECT_EQ(results["narrow"], 16);
}

// A document waiting for the text of its editor goes on once it closes, and a closed document never asks its editor again, since a closed editor never answers.
TEST_F(CodeEditorModuleTest, LetsGoOfTheWaitsOfAClosedDocument) {
    const json results = run(R"(
        local task = require("workpane.task")
        environment.workpane.plugin = { directory = directory }
        task.run("test", "task", function()
            include("catalog").load()
            local documents = include("documents")
            local document = documents.new(nil, "/folder/notes.txt", nil, {})
            local flushes = 0
            local waited = false
            document.node = { command = function() flushes = flushes + 1 end, set = function() end }
            task.run("test", "waiter", function()
                document:settle()
                waited = true
            end)
            require("async").sleep(10):await()
            local before = waited
            document:close()
            require("async").sleep(10):await()
            document:settle()
            document:poll()
            host.test_result({ before = before, after = waited, flushes = flushes, closed = document.closed })
        end)
    )");

    EXPECT_EQ(results["before"], false);
    EXPECT_EQ(results["after"], true);
    EXPECT_EQ(results["flushes"], 1);
    EXPECT_EQ(results["closed"], true);
}

TEST_F(CodeEditorModuleTest, DoesNotSaveAnEncodingAfterItsWaitingEditorCloses) {
    const json results = run(R"(
        local async = require("async")
        environment.workpane.plugin = { directory = directory }

        require("workpane.task").run("test", "task", function()
            include("catalog").load()
            local document = include("documents").new(nil, "/folder/notes.txt", nil, {})
            local flushing = false
            local finished = false
            document.node = { command = function() flushing = true end }
            require("workpane.task").run("test", "save", function()
                document:saveWith("utf-16le")
                finished = true
            end)

            while not flushing do
                async.sleep(1):await()
            end

            document:close()

            while not finished do
                async.sleep(1):await()
            end

            host.test_result({ closed = document.closed, saving = document.saving, saves = document.saves, charset = document:effectiveCharset() })
        end)
    )");

    EXPECT_EQ(results["closed"], true);
    EXPECT_EQ(results["saving"], false);
    EXPECT_EQ(results["saves"], 0);
    EXPECT_EQ(results["charset"], "utf-8");
}

#if !defined(_WIN32)
TEST_F(CodeEditorModuleTest, SavesAPosixFilenameWithABackslashBesideItsActualParent) {
    TemporaryDirectory folder;
    const auto root = std::filesystem::canonical(folder.path());
    const auto file = root / "notes\\draft.txt";
    std::ofstream(file, std::ios::binary) << "Original text\n";
    const auto source = R"lua(
        environment.workpane.files.replace = function(source, destination)
            return require("fs").rename(source, destination)
        end

        require("workpane.task").run("test", "save", function()
            local files = include("filesystem").native()
            local _, problem = files.save(FILE, "Saved text\n"):await()
            local document = include("documents").new({ files = files }, FILE, nil, {})
            host.test_result({ saved = problem == nil, name = document:name(), uri = files.uri(FILE), bytes = require("fs").readFile(FILE):await(), entries = #require("fs").readdir(ROOT):await() })
        end)
    )lua";
    const json result = run("local FILE = " + json(file.generic_string()).dump() + "\nlocal ROOT = " + json(root.generic_string()).dump() + "\n" + source);

    EXPECT_EQ(result["saved"], true);
    EXPECT_EQ(result["name"], "notes\\draft.txt");
    EXPECT_EQ(result["bytes"], "Saved text\n");
    EXPECT_EQ(result["entries"], 1);
    EXPECT_NE(result["uri"].get<std::string>().find("notes%5Cdraft.txt"), std::string::npos);
}
#endif

TEST_F(CodeEditorModuleTest, WaitsForAnAcceptedSaveBeforeWorkspaceShutdownSettles) {
    TemporaryDirectory folder;
    std::ofstream(folder.path() / "notes.txt", std::ios::binary) << "Original text\n";

    const json results = run(R"(
        local root = [==[)" + std::filesystem::canonical(folder.path()).generic_string() +
                             R"(]==]
        local async = require("async")
        local gate, release = async.deferred()
        local replacing = false
        environment.workpane.plugin = { directory = directory }
        environment.workpane.notify = { error = function() end }
        environment.workpane.log = { error = function() end }
        environment.workpane.ui = setmetatable({}, { __index = function() return function() return {} end end })
        environment.workpane.files.replace = function(source, destination)
            return { await = function()
                replacing = true
                gate:await()
                return require("fs").rename(source, destination):await()
            end }
        end
        loaded.preferences = { get = function() return false end }
        loaded.analysis = { new = function() return { stop = function() end, settled = function() return true end } end }
        loaded["views/tree"] = { new = function() return { node = {} } end }
        loaded["views/panels"] = { new = function() return { nodes = { symbolPanel = {}, bottom = {} } } end }

        require("workpane.task").run("test", "task", function()
            include("catalog").load()
            local opened = include("views/workspace").new({ id = "folder", root = root }, {}, {}, include("filesystem").native())
            local document = include("documents").new(opened, root .. "/notes.txt", nil, { changed = function() end, edited = function() end, saved = function() end })
            opened.documents = { document }
            document.text = "Saved text\n"
            document.savedText = "Original text\n"
            document.dirty = true
            require("workpane.task").run("test", "save", function() document:save() end)

            while not replacing do
                async.sleep(1):await()
            end

            opened:shutdown()
            local before = opened:settled()
            release()

            while document.saving do
                async.sleep(1):await()
            end

            host.test_result({ before = before, after = opened:settled(), closed = document.closed, bytes = require("fs").readFile(document.path):await() })
        end)
    )");

    EXPECT_EQ(results["before"], false);
    EXPECT_EQ(results["after"], true);
    EXPECT_EQ(results["closed"], true);
    EXPECT_EQ(results["bytes"], "Saved text\n");
}

TEST_F(CodeEditorModuleTest, KeepsThePreviousEncodingWhenAnAtomicSaveFails) {
    TemporaryDirectory folder;
    std::ofstream(folder.path() / "notes.txt", std::ios::binary) << "Original text\n";

    const json results = run(R"(
        local root = [==[)" + std::filesystem::canonical(folder.path()).generic_string() +
                             R"(]==]
        environment.workpane.plugin = { directory = directory }
        environment.workpane.notify = { error = function() end }
        environment.workpane.log = { error = function() end }
        environment.workpane.files.replace = function()
            return { await = function() return nil, { code = "files_replace_failed" } end }
        end

        require("workpane.task").run("test", "task", function()
            include("catalog").load()
            local document = include("documents").new({ root = root, files = include("filesystem").native() }, root .. "/notes.txt", nil, { changed = function() end, edited = function() end, saved = function() error("A failed save cannot announce success") end })
            document.text = "Changed text\n"
            document.savedText = "Original text\n"
            document.dirty = true
            document:saveWith("utf-16le")
            local first = document.manualCharset
            local saved = document:save()
            local entries = require("fs").readdir(root):await()
            host.test_result({ charset = first or document.charset, saved = saved, dirty = document.dirty, saving = document.saving, bytes = require("fs").readFile(document.path):await(), entries = #entries })
        end)
    )");

    EXPECT_EQ(results["charset"], "utf-8");
    EXPECT_EQ(results["saved"], false);
    EXPECT_EQ(results["dirty"], true);
    EXPECT_EQ(results["saving"], false);
    EXPECT_EQ(results["bytes"], "Original text\n");
    EXPECT_EQ(results["entries"], 1);
}

TEST_F(CodeEditorModuleTest, CleansAnAcceptedAtomicSaveAfterItsPluginIsWithdrawn) {
    TemporaryDirectory folder;
    std::ofstream(folder.path() / "notes.txt", std::ios::binary) << "Original text\n";
    const json results = run(R"lua(
        local root = [==[)lua" +
                             std::filesystem::canonical(folder.path()).generic_string() + R"lua(]==]
        local async = require("async")
        local gate, release = async.deferred()
        local replacing = false
        environment.workpane.files.replace = function(source, destination)
            return { await = function()
                replacing = true
                gate:await()
                return require("fs").rename(source, destination):await()
            end }
        end

        require("workpane.task").run("test", "suite", function()
            local saving = include("filesystem").native().save(root .. "/notes.txt", "Saved text\n")
            async.run(function()
                while not replacing do
                    async.sleep(1):await()
                end

                local before = require("fs").readdir(root):await()
                require("workpane.files").forget("test")
                require("workpane.task").forget("test")
                local _, problem = saving:await()
                release()
                local entries

                repeat
                    async.sleep(1):await()
                    entries = require("fs").readdir(root):await()
                until #entries == 1

                host.test_result({ before = #before, after = #entries, code = problem.code, bytes = require("fs").readFile(root .. "/notes.txt"):await() })
            end)
        end)
    )lua");

    EXPECT_EQ(results["before"], 2);
    EXPECT_EQ(results["after"], 1);
    EXPECT_EQ(results["code"], "plugin_not_running");
    EXPECT_EQ(results["bytes"], "Saved text\n");
}

// Every mark, the fallback encoding and the refused files decode as each charset defines them, and text is written back in each charset with the EditorConfig rules.
TEST_F(CodeEditorModuleTest, DecodesAndEncodesTheCharsetsItKnows) {
    const json results = run(R"(
        local encoding = include("encoding")
        local function decoded(bytes, manual)
            local answer, failure = encoding.decode(bytes, manual, "latin1")
            return answer ~= nil and { text = answer.text, charset = answer.charset, lineEnding = answer.lineEnding } or { failure = failure.code, detail = failure.detail }
        end

        host.test_result({
            latin = decoded("caf\xE9\n"),
            crlf = decoded("a\r\nb"),
            cr = decoded("a\rb"),
            bom = decoded("\xEF\xBB\xBFhi"),
            wide = decoded("\xFE\xFF\0h\0i"),
            pair = decoded("\xFF\xFE\x3D\xD8\x00\xDE"),
            lone = decoded("\xFF\xFE\x3D\xD8"),
            binary = decoded("\0\1"),
            utf32 = decoded("\xFF\xFE\0\0h\0\0\0"),
            utf7 = decoded("+/v8"),
            manual = decoded("caf\xE9", "utf-8"),
            trimmed = encoding.encode("x  \ny", "utf-8", "crlf", { trimTrailingWhitespace = true, insertFinalNewline = true }),
            kept = encoding.encode("x  \n", "utf-8", "lf", {}),
            narrow = encoding.encode("\xC3\xA9", "latin1", "lf", {}) == "\xE9",
            refused = encoding.encode("\xF0\x9F\x98\x80", "latin1", "lf", {}) == nil,
            surrogates = encoding.encode("\xF0\x9F\x98\x80", "utf-16le", "lf", {}) == "\xFF\xFE\x3D\xD8\x00\xDE",
            marked = encoding.encode("hi", "utf-8-bom", "lf", {}) == "\xEF\xBB\xBFhi",
        })
    )");

    EXPECT_EQ(results["latin"], (json{{"text", "caf\xC3\xA9\n"}, {"charset", "latin1"}, {"lineEnding", "lf"}}));
    EXPECT_EQ(results["crlf"], (json{{"text", "a\nb"}, {"charset", "utf-8"}, {"lineEnding", "crlf"}}));
    EXPECT_EQ(results["cr"], (json{{"text", "a\nb"}, {"charset", "utf-8"}, {"lineEnding", "cr"}}));
    EXPECT_EQ(results["bom"], (json{{"text", "hi"}, {"charset", "utf-8-bom"}, {"lineEnding", "lf"}}));
    EXPECT_EQ(results["wide"], (json{{"text", "hi"}, {"charset", "utf-16be"}, {"lineEnding", "lf"}}));
    EXPECT_EQ(results["pair"], (json{{"text", "\xF0\x9F\x98\x80"}, {"charset", "utf-16le"}, {"lineEnding", "lf"}}));
    EXPECT_EQ(results["lone"]["failure"], "code-editor.error.encoding");
    EXPECT_EQ(results["binary"]["failure"], "code-editor.error.binary-file");
    EXPECT_EQ(results["utf32"], (json{{"failure", "code-editor.error.encoding-unsupported"}, {"detail", " UTF-32"}}));
    EXPECT_EQ(results["utf7"], (json{{"failure", "code-editor.error.encoding-unsupported"}, {"detail", " UTF-7"}}));
    EXPECT_EQ(results["manual"]["failure"], "code-editor.error.encoding");
    EXPECT_EQ(results["trimmed"], "x\r\ny\r\n");
    EXPECT_EQ(results["kept"], "x  \n");
    EXPECT_EQ(results["narrow"], true);
    EXPECT_EQ(results["refused"], true);
    EXPECT_EQ(results["surrogates"], true);
    EXPECT_EQ(results["marked"], true);
}

// A match counts one more for every character that follows the one before it, a match ending in the file name counts twice, and equal scores keep the order of the walk.
TEST_F(CodeEditorModuleTest, RanksFilesByTheCharactersOfTheQuery) {
    const json results = run(R"(
        local finder = include("views/finder")
        host.test_result({
            empty = finder.score("src/main.lua", ""),
            missing = finder.score("src/main.lua", "xyz") == nil,
            name = finder.score("src/main.lua", "main"),
            folder = finder.score("main/src.lua", "main"),
            gap = finder.score("src/main.lua", "sml"),
            ranked = finder.rank({ "main/src.lua", "src/main.lua", "docs/readme.md" }, "main", 10),
            walked = finder.rank({ "b.txt", "a.txt", "c.txt" }, "", 2),
        })
    )");

    EXPECT_EQ(results["empty"], 0);
    EXPECT_EQ(results["missing"], true);
    EXPECT_EQ(results["name"], 20);
    EXPECT_EQ(results["folder"], 10);
    EXPECT_EQ(results["gap"], 6);
    EXPECT_EQ(results["ranked"], json::array({"src/main.lua", "main/src.lua"}));
    EXPECT_EQ(results["walked"], json::array({"b.txt", "a.txt"}));
}

// A server that never answers initialization is ended and started again within the restart budget, and one that cannot be started again is given up with its reason.
TEST_F(CodeEditorModuleTest, RestartsAServerThatDoesNotAnswerInitialization) {
    const json results = run(R"(
        loaded.catalog = { limit = function(name) return ({ initializeTimeoutMs = 20, maximumRestarts = 2, restartWindowMs = 60000 })[name] end }
        local run = function(work, ...) require("workpane.task").run("test", "task", work, ...) end
        local starts = 0
        local failures = {}
        local running = {}
        environment.workpane.app = { processId = 7 }
        environment.workpane.task = run
        environment.workpane.process = {
            start = function(options)
                starts = starts + 1
                running[starts] = options
                return starts
            end,
            write = function() end,
            stop = function(id)
                run(function() running[id].onExit(143) end)
            end,
        }

        run(function()
            local lsp = include("lsp")
            local handlers = { failed = function(message) failures[#failures + 1] = message end, log = function() end, ready = function() end, stopped = function() end, notification = function() end, capabilities = function() end }
            lsp.new("/work", "lua", "lua-language-server", {}, handlers, include("filesystem").native()):start()

            for _ = 1, 100 do
                if #failures > 0 then
                    break
                end

                require("async").sleep(10):await()
            end

            host.test_result({ starts = starts, failures = failures })
        end)
    )");

    EXPECT_EQ(results["starts"], 3);
    ASSERT_EQ(results["failures"].size(), 1U);
    EXPECT_EQ(results["failures"][0]["key"], "code-editor.lsp.restart-limit");
    EXPECT_NE(results["failures"][0]["detail"].get<std::string>().find("restart limit"), std::string::npos);
}

// Frames split across batches and several frames in one batch are read in the order they came, a handler that waits holds no frame after it and one that fails holds none either.
TEST_F(CodeEditorModuleTest, ReadsTheFramesOfALanguageServerInTheOrderTheyCame) {
    const json results = run(R"(
        loaded.catalog = { limit = function(name) return ({ initializeTimeoutMs = 60000, maximumRestarts = 2, restartWindowMs = 60000 })[name] end }
        local run = function(work, ...) require("workpane.task").run("test", "task", work, ...) end
        local process
        local logged = {}
        environment.workpane.app = { processId = 7 }
        environment.workpane.task = run
        environment.workpane.log = { error = function() end }
        environment.workpane.process = { start = function(options) process = options return 1 end, write = function() end, stop = function() end }

        run(function()
            local seen = {}
            local handlers = { failed = function() end, ready = function() end, stopped = function() end, log = function(line) logged[#logged + 1] = line end, notification = function(_, params)
                if params.wait then
                    require("async").sleep(20):await()
                end

                if params.fail then
                    error("broken")
                end

                seen[#seen + 1] = params.index
            end }

            include("lsp").new("/work", "lua", "server", {}, handlers, include("filesystem").native()):start()

            local function frame(index, extra)
                local body = '{"jsonrpc":"2.0","method":"note","params":{"index":' .. index .. (extra or "") .. '}}'
                return "Content-Length: " .. #body .. "\r\n\r\n" .. body
            end

            local first = frame(1, ',"wait":true') .. frame(2)
            local second = frame(3, ',"fail":true') .. frame(4)
            process.onOutput({ { stream = "output", text = first:sub(1, 10) }, { stream = "output", text = first:sub(11) .. second:sub(1, 7) } })
            process.onOutput({ { stream = "output", text = second:sub(8) }, { stream = "error", text = "line one\nline " }, { stream = "output", text = frame(5) }, { stream = "error", text = "two\n" } })
            require("async").sleep(60):await()
            host.test_result({ seen = seen, logged = logged })
        end)
    )");

    EXPECT_EQ(results["seen"], json::array({2, 4, 5, 1}));
    EXPECT_EQ(results["logged"], json::array({"line one", "line two"}));
}

TEST_F(CodeEditorModuleTest, ReplacesTheCapabilitiesWhenALanguageServerRestarts) {
    const json results = run(R"(
        loaded.catalog = { limit = function(name) return ({ initializeTimeoutMs = 60000, maximumRestarts = 2, restartWindowMs = 60000 })[name] end }
        local async = require("async")
        local json = require("json")
        local running = {}
        local starts = 0
        local ready = 0
        local writes = {}
        environment.workpane.app = { processId = 7 }
        environment.workpane.process = {
            start = function(options)
                starts = starts + 1
                running[starts] = options
                return starts
            end,
            write = function(id, bytes) writes[#writes + 1] = { id = id, bytes = bytes } end,
            stop = function() end,
        }

        environment.workpane.task(function()
            local quiet = function() end
            local handlers = { failed = quiet, ready = function() ready = ready + 1 end, stopped = quiet, log = quiet, notification = quiet }
            local client = include("lsp").new("/work", "lua", "server", {}, handlers, include("filesystem").native())
            client:open("file:///work/a.lua", "lua", "local value = 1")
            client:start()

            local function initialize(capabilities)
                local body = json.encode({ jsonrpc = "2.0", id = client.nextId, result = { capabilities = capabilities } })
                running[starts].onOutput({ { stream = "output", text = "Content-Length: " .. #body .. "\r\n\r\n" .. body } })
            end

            initialize({ hoverProvider = true, textDocumentSync = { change = 2, openClose = false, save = { includeText = true } }, semanticTokensProvider = { legend = { tokenTypes = { "variable" } }, full = true } })
            async.sleep(10):await()
            local first = { hover = client:provides("textDocument/hover"), tokens = client:provides("textDocument/semanticTokens/full"), save = client.save.includeText, openClose = client.openClose }
            running[1].onExit(1)
            async.sleep(10):await()
            initialize({ textDocumentSync = 1, completionProvider = {} })
            async.sleep(10):await()
            local opened = 0

            for _, write in ipairs(writes) do
                if write.id == 2 and write.bytes:find("textDocument/didOpen", 1, true) ~= nil then
                    opened = opened + 1
                end
            end

            host.test_result({ starts = starts, ready = ready, first = first, hover = client:provides("textDocument/hover"), tokens = client:provides("textDocument/semanticTokens/full"), completion = client:provides("textDocument/completion"), save = client.save ~= nil, openClose = client.openClose, opened = opened })
        end)
    )");

    EXPECT_EQ(results["starts"], 2);
    EXPECT_EQ(results["ready"], 2);
    EXPECT_EQ(results["first"], (json{{"hover", true}, {"tokens", true}, {"save", true}, {"openClose", false}}));
    EXPECT_EQ(results["hover"], false);
    EXPECT_EQ(results["tokens"], false);
    EXPECT_EQ(results["completion"], true);
    EXPECT_EQ(results["save"], false);
    EXPECT_EQ(results["openClose"], true);
    EXPECT_EQ(results["opened"], 1);
}

TEST_F(CodeEditorModuleTest, DiscardsInitializationThatArrivesWhileTheClientStops) {
    const json results = run(R"(
        loaded.catalog = { limit = function(name) return ({ initializeTimeoutMs = 60000, maximumRestarts = 2, restartWindowMs = 60000 })[name] end }
        local async = require("async")
        local json = require("json")
        local running
        local ready = 0
        local stopped = 0
        local writes = {}
        environment.workpane.app = { processId = 7 }
        environment.workpane.process = {
            start = function(options) running = options return 1 end,
            write = function(_, bytes) writes[#writes + 1] = bytes end,
            stop = function() stopped = stopped + 1 end,
        }

        environment.workpane.task(function()
            local quiet = function() end
            local handlers = { failed = quiet, ready = function() ready = ready + 1 end, stopped = quiet, log = quiet, notification = quiet }
            local client = include("lsp").new("/work", "lua", "server", {}, handlers, include("filesystem").native())
            client:open("file:///work/a.lua", "lua", "local value = 1")
            client:start()
            local body = json.encode({ jsonrpc = "2.0", id = client.nextId, result = { capabilities = { hoverProvider = true } } })
            client:stop()
            running.onOutput({ { stream = "output", text = "Content-Length: " .. #body .. "\r\n\r\n" .. body } })
            async.sleep(20):await()
            host.test_result({ ready = ready, stopped = stopped, initialized = client.ready, writes = #writes })
        end)
    )");

    EXPECT_EQ(results["ready"], 0);
    EXPECT_EQ(results["stopped"], 1);
    EXPECT_EQ(results["initialized"], false);
    EXPECT_EQ(results["writes"], 1);
}

// A file reaches its server under the identifier the protocol gives its extension, such as C among C++ files and TSX among TypeScript files.
TEST_F(CodeEditorModuleTest, NamesEachFileTheWayItsServerKnowsIt) {
    const json results = run(R"(
        environment.workpane.plugin = { directory = directory }
        require("workpane.task").run("test", "task", function()
            local catalog = include("catalog")
            catalog.load()
            local ids = {}

            for _, path in ipairs({ "/src/app.tsx", "/src/app.ts", "/src/view.jsx", "/src/view.js", "/src/main.c", "/src/MAIN.C", "/src/main.cpp", "/src/notes.txt" }) do
                ids[#ids + 1] = catalog.protocolId(catalog.detect(path), path)
            end

            host.test_result({ ids = ids })
        end)
    )");

    EXPECT_EQ(results["ids"], json::array({"typescriptreact", "typescript", "javascriptreact", "javascript", "c", "c", "cpp", "plaintext"}));
}

// A closed document leaves nothing behind in the analysis, and a stopped analysis starts no server for a document opened after it stopped.
TEST_F(CodeEditorModuleTest, StopsAnAnalysisForGoodAndForgetsWhatClosed) {
    const json results = run(R"(
        local run = function(work, ...) require("workpane.task").run("test", "task", work, ...) end
        local starts = 0
        loaded.catalog = { limit = function() return 60000 end, protocolId = function(language) return language.id end }
        loaded.preferences = { get = function() return true end }
        loaded.servers = { executable = function() return { path = "/bin/server", arguments = {} } end, any = function() return true end }
        environment.workpane.app = { processId = 7 }
        environment.workpane.task = run
        environment.workpane.process = { start = function() starts = starts + 1 return starts end, write = function() end, stop = function() end }

        run(function()
            local quiet = function() end
            local analysis = include("analysis").new("/work", { changed = quiet, progress = quiet, dropped = quiet, diagnostics = quiet, outline = quiet, highlights = quiet }, include("filesystem").native())
            local document = { path = "/work/a.lua", language = { id = "lua" }, text = "x", cursor = { line = 1, column = 1 }, line = function() return "x" end }
            analysis:open(document)
            analysis:changed(document)
            local counted = analysis.revisions[document]
            analysis:closed(document)
            local pruned = analysis.revisions[document] == nil and analysis.analyzed[document] == nil
            analysis:stop()
            analysis:open(document)
            host.test_result({ starts = starts, counted = counted, pruned = pruned, tracked = next(analysis.tracked) == nil })
        end)
    )");

    EXPECT_EQ(results["starts"], 1);
    EXPECT_EQ(results["counted"], 1);
    EXPECT_EQ(results["pruned"], true);
    EXPECT_EQ(results["tracked"], true);
}

// A read of the file that a save crossed is dropped, so the bytes it found before the save never replace the text that was saved.
TEST_F(CodeEditorModuleTest, DropsAReadThatASaveCrossed) {
    TemporaryDirectory folder;
    std::ofstream(folder.path() / "notes.txt", std::ios::binary) << "older bytes\n";

    const json results = run(R"(
        local root = [==[)" + std::filesystem::canonical(folder.path()).generic_string() +
                             R"(]==]
        local run = function(work, ...) require("workpane.task").run("test", "task", work, ...) end
        environment.workpane.plugin = { directory = directory }
        environment.workpane.notify = { error = function() end }
        loaded.preferences = { get = function() return "utf-8" end }

        local async = require("async")
        local files = require("fs")
        local gate, open = async.deferred()
        local entered, enter = async.deferred()
        local reading = false
        environment.workpane.files.stat = function(path)
            return async.promise(function()
                if not reading then
                    reading = true
                    enter()
                end

                gate:await()

                return files.stat(path):await()
            end)
        end

        run(function()
            include("catalog").load()
            local changes = 0
            local document = include("documents").new({ root = root, files = include("filesystem").native() }, root .. "/notes.txt", nil, { changed = function() changes = changes + 1 end, loaded = function() end })
            document.node = { command = function() end, set = function() end }
            document.loaded = true
            document.text = "saved\n"
            document.savedText = "saved\n"
            document.digest = "saved"
            local finished = false

            run(function()
                document:read(false)
                finished = true
            end)

            entered:await()
            document.saves = document.saves + 1
            open()

            while not finished do
                require("async").sleep(5):await()
            end

            local crossed = document.text
            local changed = changes
            document:read(false)
            host.test_result({ crossed = crossed, changed = changed, read = document.text })
        end)
    )");

    EXPECT_EQ(results["crossed"], "saved\n");
    EXPECT_EQ(results["changed"], 0);
    EXPECT_EQ(results["read"], "older bytes\n");
}

// The tree lists again only the folders on screen and forgets a folder that went away, with everything that was open inside it.
TEST_F(CodeEditorModuleTest, ListsOnlyTheFoldersOnScreenAndForgetsTheOnesThatWent) {
    const json results = run(R"(
        local run = function(work, ...) require("workpane.task").run("test", "task", work, ...) end
        local listings = { ["/work"] = { { name = "a", kind = "directory" } }, ["/work/a"] = { { name = "b", kind = "directory" } }, ["/work/a/b"] = { { name = "c.txt", kind = "file" } } }
        local listed = {}
        environment.workpane.ui = { tree = function() return { set = function() end, command = function() end } end }
        environment.workpane.files.list = function(path)
            listed[path] = (listed[path] or 0) + 1
            return { await = function() return listings[path] ~= nil and { entries = listings[path], complete = true } or nil, listings[path] == nil and { code = "files_missing" } or nil end }
        end

        run(function()
            environment.workpane.plugin = { directory = directory }
            include("catalog").load()
            local tree = include("views/tree").new("/work", { watched = function() end }, include("filesystem").native())
            tree:start()
            tree:toggled("/work/a", true)
            tree:toggled("/work/a/b", true)
            tree:toggled("/work/a", false)
            listed = {}
            tree:poll()
            local collapsed = { root = listed["/work"] or 0, inner = listed["/work/a/b"] or 0 }
            tree:toggled("/work/a", true)
            listings["/work/a"] = {}
            listings["/work/a/b"] = nil
            tree:poll()
            host.test_result({ collapsed = collapsed, forgotten = tree.folders["/work/a/b"] == nil and tree.expanded["/work/a/b"] == nil, kept = tree.folders["/work/a"] ~= nil })
        end)
    )");

    EXPECT_EQ(results["collapsed"]["root"], 1);
    EXPECT_EQ(results["collapsed"]["inner"], 0);
    EXPECT_EQ(results["forgotten"], true);
    EXPECT_EQ(results["kept"], true);
}

TEST_F(CodeEditorModuleTest, KeepsObservedFoldersWhenAListingIsIncompleteAndReportsItOnce) {
    const json results = run(R"lua(
        local partial = false
        local removed = false
        local warnings = 0
        local events = {}
        local frames = 0
        environment.workpane.plugin = { directory = directory }
        environment.workpane.notify = { warning = function() warnings = warnings + 1 end }
        environment.workpane.ui = { tree = function() return { set = function() frames = frames + 1 end, command = function() end } end }
        environment.workpane.files.list = function(path, options)
            assert(options.maximum == 20000)
            local entries = path == "/work" and not partial and not removed and { { name = "a", kind = "directory" } } or {}
            return { await = function() return { entries = entries, complete = not partial } end }
        end

        require("workpane.task").run("test", "task", function()
            include("catalog").load()
            local tree = include("views/tree").new("/work", { watched = function(changes)
                for _, change in ipairs(changes) do
                    events[#events + 1] = change
                end
            end }, include("filesystem").native())
            tree:start()
            tree:toggled("/work/a", true)
            partial = true
            tree:poll()
            tree:poll()
            local kept = tree.expanded["/work/a"] == true and tree.folders["/work/a"] ~= nil
            local during = #events
            partial = false
            removed = true
            tree:poll()
            host.test_result({ kept = kept, during = during, events = events, warnings = warnings, frames = frames, forgotten = tree.folders["/work/a"] == nil and tree.expanded["/work/a"] == nil })
        end)
    )lua");

    EXPECT_EQ(results["kept"], true);
    EXPECT_EQ(results["during"], 0);
    ASSERT_EQ(results["events"].size(), 1U);
    EXPECT_EQ(results["events"][0], (json{{"path", "/work/a"}, {"type", 3}}));
    EXPECT_EQ(results["warnings"], 2);
    EXPECT_GE(results["frames"].get<int>(), 5);
    EXPECT_EQ(results["forgotten"], true);
}

TEST_F(CodeEditorModuleTest, RefusesACaseOnlyRenameWhenTheDestinationListingIsIncomplete) {
    const json results = run(R"lua(
        local fs = require("fs")
        local renames = 0
        local failures = 0
        environment.workpane.plugin = { directory = directory }
        environment.workpane.notify = { error = function() failures = failures + 1 end }
        environment.workpane.ui = { tree = function() return { set = function() end, command = function() end } end }
        environment.workpane.files.list = function()
            return { await = function() return { entries = {}, complete = false } end }
        end
        environment.workpane.files.stat = function() return { await = function() return { isFile = true } end } end
        fs.rename = function() renames = renames + 1 return { await = function() return true end } end

        require("workpane.task").run("test", "task", function()
            include("catalog").load()
            local tree = include("views/tree").new("/work", {}, include("filesystem").native())
            local moved = tree:relocate("/work/alpha.lua", "/work/ALPHA.lua")
            host.test_result({ moved = moved, renames = renames, failures = failures })
        end)
    )lua");

    EXPECT_EQ(results["moved"], false);
    EXPECT_EQ(results["renames"], 0);
    EXPECT_EQ(results["failures"], 1);
}

TEST_F(CodeEditorModuleTest, RefusesFileCreationAndRelocationWhenDestinationInspectionFails) {
    const json results = run(R"(
        environment.workpane.plugin = { directory = directory }
        local failures = 0
        local writes = 0
        local directories = 0
        local renames = 0
        local prompts = 0
        local targetUnavailable = false
        environment.workpane.notify = { error = function() failures = failures + 1 end }
        environment.workpane.ui.tree = function() return { set = function() end, command = function() end } end
        environment.workpane.dialogs = { prompt = function()
            prompts = prompts + 1
            return { await = function() return "existing.txt" end }
        end }

        require("workpane.task").run("test", "task", function()
            include("catalog").load()
            local files = {
                stat = function(path)
                    return { await = function()
                        if path == "/work" and not targetUnavailable then
                            return { isDir = true }
                        end

                        return nil, { code = "files_metadata_unavailable", message = "The destination cannot be inspected", detail = "" }
                    end }
                end,

                writeFile = function() writes = writes + 1 return { await = function() return true end } end,
                mkdir = function() directories = directories + 1 return { await = function() return true end } end,
                rename = function() renames = renames + 1 return { await = function() return true end } end,
            }

            local handlers = { watched = function() end, moved = function() end, open = function() end }
            local tree = include("views/tree").new("/work", handlers, files)
            tree:create("/work", false)
            tree:create("/work", true)
            local relocated = tree:relocate("/work/source.txt", "/work/existing.txt")
            targetUnavailable = true
            tree:create("/work", false)
            host.test_result({ writes = writes, directories = directories, renames = renames, relocated = relocated, failures = failures, prompts = prompts })
        end)
    )");

    EXPECT_EQ(results["writes"], 0);
    EXPECT_EQ(results["directories"], 0);
    EXPECT_EQ(results["renames"], 0);
    EXPECT_EQ(results["relocated"], false);
    EXPECT_EQ(results["failures"], 4);
    EXPECT_EQ(results["prompts"], 2);
}

TEST_F(CodeEditorModuleTest, RefusesCreationWhenItsTargetFolderDisappears) {
    TemporaryDirectory folder;
    const auto root = std::filesystem::canonical(folder.path());
    const auto target = root / "workspace";
    ASSERT_TRUE(std::filesystem::create_directory(target));
    ASSERT_TRUE(std::filesystem::remove(target));
    const auto script = R"lua(
        environment.workpane.plugin = { directory = directory }
        local failures = 0
        local prompts = 0
        environment.workpane.notify = { error = function() failures = failures + 1 end }
        environment.workpane.ui.tree = function() return { set = function() end, command = function() end } end
        environment.workpane.files.stat = function()
            return environment.workpane.future(function() return nil end)
        end
        environment.workpane.dialogs = { prompt = function()
            prompts = prompts + 1
            return { await = function() return prompts == 1 and "outside.txt" or "outside-folder" end }
        end }

        require("workpane.task").run("test", "task", function()
            include("catalog").load()
            local handlers = { watched = function() end, moved = function() end, open = function() end }
            local tree = include("views/tree").new(TARGET, handlers, include("filesystem").native())
            tree:create(TARGET, false)
            tree:create(TARGET, true)
            host.test_result({ prompts = prompts, failures = failures })
        end)
    )lua";
    const auto result = run("local TARGET = " + json(target.generic_string()).dump() + "\n" + script);

    EXPECT_EQ(result["prompts"], 0);
    EXPECT_EQ(result["failures"], 2);
    EXPECT_FALSE(std::filesystem::exists(root / "outside.txt"));
    EXPECT_FALSE(std::filesystem::exists(root / "outside-folder"));
}

TEST_F(CodeEditorModuleTest, SelectsRemoteMoveDestinationsThroughTheirWorkspace) {
    const json results = run(R"lua(
        environment.workpane.plugin = { directory = directory }
        environment.workpane.ui = { tree = function() return { set = function() end, command = function() end } end }
        local chosen = "/work/destination"
        local selections = 0
        local moved = {}
        local backend = { remote = true }
        backend.canonical = function(path) return { await = function() return path end } end
        backend.stat = function() return { await = function() return { isDir = true } end } end
        environment.workpane.await = function(future) return future:await() end
        environment.workpane.dialogs = { selectFolder = function(options)
            assert(options.backend == backend and options.initial == "/work")
            selections = selections + 1
            return { await = function() return chosen end }
        end }
        require("workpane.task").run("test", "task", function()
            include("catalog").load()
            local tree = include("views/tree").new("/work", {}, backend)
            tree.relocate = function(_, source, destination) moved[#moved + 1] = { source, destination } end
            tree:move("/work/file.txt")
            chosen = nil
            tree:move("/work/file.txt")
            chosen = "/elsewhere"
            tree:move("/work/file.txt")
            host.test_result({ selections = selections, moved = moved })
        end)
    )lua");

    EXPECT_EQ(results["selections"], 3);
    EXPECT_EQ(results["moved"], json::array({json::array({"/work/file.txt", "/work/destination/file.txt"})}));
}

// Positions convert between the characters of the editor and the UTF-16 units of the protocol.
TEST_F(CodeEditorModuleTest, ConvertsPositionsForTheProtocol) {
    const json results = run(R"(
        local lsp = include("lsp")
        host.test_result({
            toUnits = lsp.toUnits("a\xF0\x9F\x98\x80b", 3),
            fromUnits = lsp.fromUnits("a\xF0\x9F\x98\x80b", 3),
            beyond = lsp.fromUnits("ab", 5),
        })
    )");

    EXPECT_EQ(results["toUnits"], 3);
    EXPECT_EQ(results["fromUnits"], 3);
    EXPECT_EQ(results["beyond"], 6);
}

} // namespace workpane::tests
