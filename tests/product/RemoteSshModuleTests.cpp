#include "support/Resources.h"
#include "support/RootedPath.h"
#include "support/ScriptHarness.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace workpane::tests {

class RemoteSshModuleTest : public ::testing::Test {
  protected:
    [[nodiscard]] static nlohmann::json run(std::string_view body) {
        const std::string directory = (Resources::staged() / "plugins" / "remote-ssh").generic_string();
        ScriptHarness harness;
        const std::string prelude = R"lua(
            local directory = [==[)lua" +
                                    directory + R"lua(]==]
            local loaded = {}
            local environment = setmetatable({}, { __index = _G })
            local paths = require("workpane.paths")
            environment.workpane = { files = { absolute = function(path) return paths.absolute(require("platform").os(), path) end, paths = function(platform) return { normalizeRoot = function(path) return paths.normalizeRoot(platform, path) end } end } }
            environment.workpane.credentials = { read = function() return require("async").promise(function() end) end }
            environment.workpane.i18n = { text = function(value) return value end, translate = function(value) return value end }
            local executableDirectory = )lua" +
                                    nlohmann::json(RootedPath::of("usr/bin").generic_string()).dump() + R"lua(

            function environment.include(name)
                if loaded[name] == nil then
                    loaded[name] = assert(loadfile(directory .. "/" .. name .. ".lua", "t", environment))()
                end

                return loaded[name]
            end

            local include = environment.include
            local async = require("async")
            local socket = require("socket")
            local json = require("json")

            local function checked(promise)
                local value, failure = promise:await()
                assert(failure == nil, failure)

                return value
            end

            local function exact(peer, count)
                local source = ""

                while #source < count do
                    local chunk = checked(peer:receive(count - #source))
                    assert(chunk ~= "", "The proxy request ended early")
                    source = source .. chunk
                end

                return source
            end

            local function proxyHandshake(peer)
                assert(exact(peer, 3) == "\5\1\0")
                checked(peer:send("\5"))
                checked(peer:send("\0"))
                assert(exact(peer, 10) == "\5\1\0\1\127\0\0\1" .. string.pack(">I2", 4321))
                checked(peer:send("\5\0\0\1\0\0\0\0\0\0"))
            end

        )lua";
        const auto loaded = harness.run(prelude + std::string(body));
        EXPECT_TRUE(loaded.hasValue()) << (loaded.hasValue() ? "" : loaded.error().message);
        // clang-format off
        std::ignore = harness.pollUntil([&harness]() { return !harness.results.empty(); });
        // clang-format on
        EXPECT_EQ(harness.results.size(), 1U) << nlohmann::json(harness.logged).dump();
        EXPECT_TRUE(harness.logged.empty()) << nlohmann::json(harness.logged).dump();

        return harness.results.size() == 1 ? harness.results.front() : nlohmann::json();
    }
};

TEST_F(RemoteSshModuleTest, SerializesOwnedScopeCommandsAndRecyclesAdmissionWithoutReusingIdentities) {
    const auto result = run(R"lua(
        async.run(function()
            local commands = {}
            local responses = {}
            local inFlight = false
            local changed, change = async.deferred()
            local finished, finish = async.deferred()
            local connection = {
                finished = finished,
                write = function(_, value)
                    assert(not inFlight)
                    commands[#commands + 1] = value

                    if value == "quit\n" then
                        finish()
                    else
                        inFlight = true
                        local method, identity = value:match("^(%a+) ?(%d*)\n$")
                        responses[#responses + 1] = method == "ping" and "PONG" or (method == "open" and "OPEN " or "STOPPED ") .. identity
                    end

                    change()
                end,
                line = function(_, _, _, cancellation)
                    while #responses == 0 and not cancellation.closed do
                        checked(async.race({ changed, cancellation.finished }))
                        changed, change = async.deferred()
                    end

                    assert(not cancellation.closed)
                    checked(async.promise(function() end))
                    inFlight = false

                    return table.remove(responses, 1)
                end,
                close = function() end,
                stop = function() error("The healthy SSH scope was stopped") end,

                code = 0,
                failed = false,
            }

            local owner = include("scope").new(connection, "linux", "/tmp/private-scope", 123)
            local pending = {}

            for index = 1, 32 do
                pending[index] = async.promise(function()
                    return owner:open(1000)
                end)
            end

            local identities = checked(async.all(pending))
            local accepted, problem = pcall(owner.open, owner, 1000)
            assert(not accepted and problem.code == "ssh_program_limit")

            for _, identity in ipairs(identities) do
                owner:release(identity, 1000)
            end

            for index = 1, 1000 do
                local identity = owner:open(1000)
                assert(identity == 32 + index)
                owner:release(identity, 1000)
            end

            owner:command("ping", nil, 1000)
            assert(checked(owner:close()).value)
            host.test_result({ count = owner.count, waiting = owner.waiting, last = owner.nextIdentity, closed = owner.closed, commands = #commands })
        end)
    )lua");

    EXPECT_EQ(result, nlohmann::json({{"count", 0}, {"waiting", 0}, {"last", 1032}, {"closed", true}, {"commands", 2066}}));
}

TEST_F(RemoteSshModuleTest, ClosesAnOwnedScopeOnMismatchedRepliesAndRetainsAcceptedCleanup) {
    const auto result = run(R"lua(
        async.run(function()
            local stopped = false
            local joined = false
            local written, write = async.deferred()
            local stoppedFuture, stop = async.deferred()
            local answered = false
            local connection = {
                write = function() write() end,
                line = function()
                    checked(written)

                    if answered then
                        checked(stoppedFuture)
                        error(include("protocol").failure("ssh_connection_closed"), 0)
                    end

                    answered = true

                    return "OPEN 2"
                end,
                stop = function() stopped = true stop() end,
                close = function() joined = true end,
            }

            local owner = include("scope").new(connection, "linux", "/tmp/private-scope", 123)
            local accepted, problem = pcall(owner.open, owner, 1000)
            assert(not accepted and problem.code == "ssh_scope_response_invalid" and stopped)
            local cleanup = checked(owner:close())
            host.test_result({ closed = owner.closed, joined = joined, cleanup = cleanup.failure.code, count = owner.count, waiting = owner.waiting })
        end)
    )lua");

    EXPECT_EQ(result, nlohmann::json({{"closed", true}, {"joined", true}, {"cleanup", "ssh_cleanup_failed"}, {"count", 0}, {"waiting", 0}}));
}

TEST_F(RemoteSshModuleTest, ReleasesAProducerWhoseTransportEndsWithoutItsOwnedCompletion) {
    const auto result = run(R"lua(
        local options
        local waiting, finish = async.deferred()
        local released = false
        local releases = 0
        local scope = { closed = false, wait = function()
            checked(waiting)
            error(include("protocol").failure("ssh_program_request_invalid"), 0)
        end, release = function()
            if not released then
                released = true
                releases = releases + 1
                finish()
            end
        end }
        environment.workpane.process = {
            start = function(value) options = value return 1 end,
            stop = function() end,
            read = function() return async.promise(function()
                options.onExit(1, false)

                return { chunks = {}, ended = true, code = 1, failed = true }
            end) end,
        }

        async.run(function()
            local running = include("remote-program").new(scope, 1, { program = executableDirectory .. "/ssh", arguments = {}, directory = executableDirectory })
            local accepted, problem = pcall(running.read, running, 65536)
            assert(not accepted and problem.code == "ssh_program_failed" and released)
            assert(checked(running:close()).value)
            host.test_result({ code = problem.code, released = released, releases = releases, nativeEnded = running.nativeEnded })
        end)
    )lua");

    EXPECT_EQ(result, nlohmann::json({{"code", "ssh_program_failed"}, {"released", true}, {"releases", 1}, {"nativeEnded", true}}));
}

TEST_F(RemoteSshModuleTest, RefusesUnsafeWindowsBatchArgumentsBeforeProgramAdmission) {
    const auto result = run(R"lua(
        environment.workpane.files.paths = function(platform)
            return { absolute = function(path) return require("workpane.paths").absolute(platform, path) end }
        end

        local opened = 0
        local owner = { platform = "windows", path = "C:/private", open = function() opened = opened + 1 end }
        local launcher = include("launch").new(owner, {}, "C:/tools/ssh.exe", {}, "host")
        local codes = {}

        for _, argument in ipairs({ "one&two", "one|two", "%EXPANDED%", "!EXPANDED!", "embedded\"quote", "one\ntwo" }) do
            local accepted, problem = pcall(launcher.start, launcher, { program = "C:/tools/server.cmd", arguments = { argument }, directory = "C:/workspace", variables = {}, cleared = {} })
            assert(not accepted and problem.code == "process_argument_unsafe")
            codes[#codes + 1] = problem.code
        end

        host.test_result({ count = #codes, opened = opened })
    )lua");

    EXPECT_EQ(result, nlohmann::json({{"count", 6}, {"opened", 0}}));
}

TEST_F(RemoteSshModuleTest, QuotesNativeSourceArgumentsAndBoundsTheWindowsSourceEnvelope) {
    const auto result = run(R"lua(
        environment.workpane.plugin = { directory = directory }
        async.run(function()
            local source = include("source")
            local arguments = { "", "Unicode 雪 'quoted'\n$HOME `command`", "C:/folder path/file" }
            local posix = source.prepare("linux", "printf '%s' \"$1\"", arguments)
            assert(posix.input == "" and posix.command:find("'\\''quoted'\\''", 1, true) ~= nil)
            local windows = source.prepare("windows", "[Console]::WriteLine($sshArguments[1])", arguments)
            local decoded = require("crypto").base64Decode(windows.input:sub(1, -2))
            local encoded = assert(decoded:match("FromBase64String%('([^']+)'%)"))
            assert(json.encode(json.decode(require("crypto").base64Decode(encoded))) == json.encode(arguments))
            local accepted, problem = pcall(source.prepare, "windows", string.rep("x", 49152), arguments)
            assert(not accepted and problem.code == "ssh_launch_invalid")
            accepted, problem = pcall(source.prepare, "linux", "echo", { [2] = "sparse" })
            assert(not accepted and problem.code == "ssh_launch_invalid")
            accepted, problem = pcall(source.quote, "truncated\0suffix")
            assert(not accepted and problem.code == "ssh_launch_invalid")
            host.test_result({ arguments = #arguments, bounded = #windows.command <= 8191 and #decoded <= 49152, terminated = windows.input:sub(-1) == "\n" })
        end)
    )lua");

    EXPECT_EQ(result, nlohmann::json({{"arguments", 3}, {"bounded", true}, {"terminated", true}}));
}

TEST_F(RemoteSshModuleTest, ReleasesConsumedControlLinesWithoutImposingACumulativeOutputLimit) {
    const auto result = run(R"lua(
        local options
        environment.workpane.app = { dataDirectory = executableDirectory }
        environment.workpane.process = {
            start = function(value)
                options = value

                return 1
            end,
            stop = function() error("The consumed control stream was stopped") end,
        }

        local control = include("program").new("/usr/bin/ssh", {}, {}, nil)
        local cancelled, cancel = async.deferred()
        local owner = { closed = false, finished = cancelled }

        async.run(function()
            for index = 1, 2000 do
                local text = "REPLY " .. tostring(index) .. " " .. string.rep("x", 40)
                options.onOutput({ { stream = "output", text = text .. "\r\n" } })
                assert(control:line("REPLY ", 1000, owner) == text)
                assert(control.bytes == 0 and #control.lines == 0 and control.pending == "")
            end

            assert(control.failure == nil)
            cancel()
            host.test_result({ replies = 2000, retained = control.bytes })
        end)
    )lua");

    EXPECT_EQ(result, nlohmann::json({{"replies", 2000}, {"retained", 0}}));
}

TEST_F(RemoteSshModuleTest, DecodesFragmentedSftpPacketsAndPreservesOpaqueBinaryFields) {
    const auto result = run(R"lua(
        local packets = include("sftp-packets")
        local types = packets.types
        local text = packets.string
        local number = packets.integer
        local attributes = number(15) .. packets.offset(4294967297) .. number(42) .. number(43) .. number(0x81a0) .. number(100) .. number(200)
        local name = "name '[]; 雪.txt"
        local source = packets.frame(types.version, number(3) .. text("posix-rename@openssh.com") .. text("1"))
            .. packets.request(types.handle, 1, text("\0\255\128"))
            .. packets.request(types.data, 2, text("\0\255\128\195\169\r\n"))
            .. packets.request(types.name, 3, number(1) .. text(name) .. text("An opaque long name") .. attributes)
            .. packets.request(types.status, 4, number(1) .. text("End of file") .. text(""))
        local checked = 0

        for _, size in ipairs({ 1, 2, 3, 4, 5, 17, #source }) do
            local decoder = packets.decoder()
            local values = {}

            for position = 1, #source, size do
                decoder:feed(source:sub(position, position + size - 1), function(value)
                    values[#values + 1] = value
                end)
            end

            assert(decoder:complete() and #values == 5)
            assert(values[1].extensions["posix-rename@openssh.com"] == "1")
            assert(values[2].handle == "\0\255\128" and values[3].data == "\0\255\128\195\169\r\n")
            assert(values[4].entries[1].name == name)
            local actual = values[4].entries[1].attributes
            assert(actual.size == 4294967297 and actual.uid == 42 and actual.gid == 43 and actual.permissions == 0x81a0 and actual.accessed == 100 and actual.modified == 200)
            assert(values[5].status == 1)
            checked = checked + 1
        end

        host.test_result({ checked = checked })
    )lua");

    EXPECT_EQ(result, nlohmann::json({{"checked", 7}}));
}

TEST_F(RemoteSshModuleTest, RefusesMalformedOversizedAndAmbiguousSftpResponses) {
    const auto result = run(R"lua(
        local packets = include("sftp-packets")
        local types = packets.types
        local text = packets.string
        local number = packets.integer
        local invalid = {
            number(0),
            number(4),
            number(packets.maximumPacket + 1),
            packets.frame(types.version, number(4)),
            packets.frame(types.version, number(3) .. text("duplicate") .. text("1") .. text("duplicate") .. text("1")),
            packets.request(types.handle, 1, text("")),
            packets.request(types.handle, 1, text(string.rep("x", 257))),
            packets.request(types.handle, 1, text("handle") .. "trailing"),
            packets.request(types.handle, 1, number(42) .. "short"),
            packets.request(types.data, 1, text("")),
            packets.request(types.data, 1, text(string.rep("x", packets.maximumData + 1))),
            packets.request(types.attrs, 1, number(16)),
            packets.request(types.attrs, 1, number(1) .. number(0x80000000) .. number(0)),
            packets.request(types.attrs, 1, number(0x80000000) .. number(65)),
            packets.request(types.name, 1, number(4097)),
            packets.request(types.name, 1, number(1) .. text("name") .. text("long") .. number(1)),
            packets.request(types.status, 1, number(9) .. text("Unknown") .. text("")),
            packets.request(255, 1, ""),
        }

        for _, source in ipairs(invalid) do
            local accepted, failure = pcall(function()
                packets.decoder():feed(source, function() end)
            end)
            assert(not accepted and failure.code == "ssh_sftp_response_invalid")
        end

        local partial = packets.decoder()
        partial:feed(number(10) .. "part", function() end)
        assert(not partial:complete())
        host.test_result({ invalid = #invalid, partial = true })
    )lua");

    EXPECT_EQ(result, nlohmann::json({{"invalid", 18}, {"partial", true}}));
}

TEST_F(RemoteSshModuleTest, DecodesAFullSftpReadDeliveredOneByteAtATimeWithBoundedFragmentStorage) {
    const auto result = run(R"lua(
        local packets = include("sftp-packets")
        local decoder = packets.decoder()
        local content = string.rep("\0\255\128\195\169", 6553) .. "end"
        assert(#content == packets.maximumData)
        local source = packets.request(packets.types.data, 42, packets.string(content))
        local received

        for position = 1, #source do
            decoder:feed(source:sub(position, position), function(value)
                assert(received == nil)
                received = value
            end)
            assert(#decoder.parts < 4096 and #decoder.blocks <= 256)
        end

        assert(decoder:complete() and received.id == 42 and received.data == content)
        assert(#decoder.parts == 0 and #decoder.blocks == 0 and decoder.blockBytes == 0)
        host.test_result({ bytes = #received.data })
    )lua");

    EXPECT_EQ(result, nlohmann::json({{"bytes", 32768}}));
}

TEST_F(RemoteSshModuleTest, MatchesConcurrentSftpRepliesWithoutDisclosingServerDiagnostics) {
    const auto result = run(R"lua(
        local packets = include("sftp-packets")
        local types = packets.types
        local launched
        local sent = {}
        local stopped = 0
        local inputEnded = 0
        environment.workpane.process = {
            start = function(options)
                launched = options
                return 42
            end,
            write = function(identity, source)
                assert(identity == 42)
                sent[#sent + 1] = source
            end,
            stop = function(identity)
                assert(identity == 42)
                stopped = stopped + 1
                launched.onExit(255, false)
            end,
            endInput = function(identity)
                assert(identity == 42)
                inputEnded = inputEnded + 1
                launched.onExit(0, false)
            end,
        }

        async.promise(function()
            local client = include("sftp").new({ program = "ssh", arguments = { "-s", "host", "sftp" }, directory = executableDirectory })
            assert(launched.binaryOutput == true and launched.input == nil)
            local opening = async.promise(function()
                local value, problem = client:start(1000)
                assert(value, problem ~= nil and problem.code)

                return value
            end)

            while #sent < 1 do
                async.sleep(1):await()
            end
            assert(sent[1] == packets.frame(types.init, packets.integer(3)))
            launched.onOutput({ { stream = "output", text = packets.frame(types.version, packets.integer(3)) } })
            assert(checked(opening))
            local first = async.promise(function()
                return assert(client:request(types.realpath, packets.string("."), types.name, 1000))
            end)
            local second = async.promise(function()
                return assert(client:request(types.stat, packets.string("file"), types.attrs, 1000))
            end)
            while #sent < 3 do
                async.sleep(1):await()
            end
            assert(client.count == 2)
            local name = packets.request(types.name, 1, packets.integer(1) .. packets.string("/workspace 雪") .. packets.string("ignored") .. packets.integer(0))
            local attrs = packets.request(types.attrs, 2, packets.integer(1) .. packets.offset(99))
            launched.onOutput({ { stream = "error", text = "A private password must never appear in a failure" }, { stream = "output", text = attrs .. name:sub(1, 3) } })
            assert(checked(second).attributes.size == 99)
            launched.onOutput({ { stream = "output", text = name:sub(4) } })
            assert(checked(first).entries[1].name == "/workspace 雪" and client.count == 0 and client.bytes == 0)
            assert(client:close())
            assert(stopped == 0 and inputEnded == 1 and client.ended and client.diagnostic == nil and client.failure.detail == "")
            assert(client:close())
            host.test_result({ stopped = stopped, inputEnded = inputEnded, closed = client.closed })
        end)
    )lua");

    EXPECT_EQ(result, nlohmann::json({{"stopped", 0}, {"inputEnded", 1}, {"closed", true}}));
}

TEST_F(RemoteSshModuleTest, ClosesSftpOnUnexpectedRepliesDeadlinesAndAbandonedRequests) {
    const auto result = run(R"lua(
        local bridge = require("workpane.bridge")
        local packets = include("sftp-packets")
        local types = packets.types
        local launched
        local sent = {}
        local stopped = 0
        environment.workpane.process = {
            start = function(options)
                launched = options
                return 42
            end,
            write = function(_, source)
                sent[#sent + 1] = source
            end,
            stop = function()
                stopped = stopped + 1
                launched.onExit(255, false)
            end,
        }

        local function client()
            sent = {}
            local created = include("sftp").new({ program = "ssh", arguments = {}, directory = executableDirectory })
            local opening = async.promise(function()
                return created:start(1000)
            end)

            while #sent == 0 do
                async.sleep(1):await()
            end

            launched.onOutput({ { stream = "output", text = packets.frame(types.version, packets.integer(3)) } })
            assert(checked(opening))

            return created
        end

        async.promise(function()
            local codes = {}
            local replies = {
                packets.request(types.attrs, 999, packets.integer(0)),
                packets.request(types.handle, 1, packets.string("unexpected")),
                packets.request(types.status, 1, packets.integer(0) .. packets.string("") .. packets.string("")),
                packets.request(types.data, 1, packets.string("too long")),
                packets.frame(types.version, packets.integer(3)),
            }

            for _, response in ipairs(replies) do
                local current = client()
                local waiting = async.promise(function()
                    local _, failure = current:request(types.read, "", types.data, 1000, 3)
                    return failure.code
                end)

                while #sent < 2 do
                    async.sleep(1):await()
                end

                launched.onOutput({ { stream = "output", text = response } })
                assert(checked(waiting) == "ssh_sftp_response_invalid")
                assert(current.closed and current.ended and current.count == 0 and current.bytes == 0)
                codes[#codes + 1] = current.failure.code
            end

            local current = client()
            local _, expired = current:request(types.stat, "", types.attrs, 2)
            assert(expired.code == "ssh_request_timeout" and current.closed and current.ended)
            codes[#codes + 1] = expired.code
            current = client()
            local waiting, cancel = bridge.future(function()
                return current:request(types.open, "", types.handle, 1000)
            end)

            while #sent < 2 do
                async.sleep(1):await()
            end

            cancel()
            local _, abandoned = waiting:await()
            assert(abandoned.code == "plugin_not_running")
            assert(current.closed and current.ended and current.failure.code == "ssh_sftp_cancelled")
            codes[#codes + 1] = current.failure.code
            current = client()
            waiting, cancel = bridge.future(function()
                return current:request(types.open, "", types.handle, 1000)
            end)

            while #sent < 2 do
                async.sleep(1):await()
            end

            launched.onOutput({ { stream = "output", text = packets.request(types.handle, 1, packets.string("accepted-before-cancel")) } })
            cancel()
            local _, late = waiting:await()
            assert(late.code == "plugin_not_running")
            assert(current.closed and current.ended and current.failure.code == "ssh_sftp_cancelled")
            codes[#codes + 1] = current.failure.code
            current = client()
            launched.onOutput({ { stream = "output", text = packets.integer(20) .. "partial" } })
            launched.onExit(255, false)
            assert(current.closed and current.failure.code == "ssh_sftp_response_invalid")
            codes[#codes + 1] = current.failure.code
            host.test_result({ codes = codes, stopped = stopped })
        end)
    )lua");

    EXPECT_EQ(result["codes"], nlohmann::json::array({"ssh_sftp_response_invalid", "ssh_sftp_response_invalid", "ssh_sftp_response_invalid", "ssh_sftp_response_invalid", "ssh_sftp_response_invalid", "ssh_request_timeout", "ssh_sftp_cancelled", "ssh_sftp_cancelled", "ssh_sftp_response_invalid"}));
    EXPECT_EQ(result["stopped"], 8);
}

TEST_F(RemoteSshModuleTest, BoundsSftpAdmissionAndReleasesAllAcceptedWaitersOnClose) {
    const auto result = run(R"lua(
        local packets = include("sftp-packets")
        local types = packets.types
        local launched
        local writes = 0
        local stopped = 0
        local inputEnded = 0
        environment.workpane.process = {
            start = function(options)
                launched = options
                return 42
            end,
            write = function()
                writes = writes + 1
            end,
            stop = function()
                stopped = stopped + 1
                launched.onExit(255, false)
            end,
            endInput = function()
                inputEnded = inputEnded + 1
                launched.onExit(0, false)
            end,
        }

        async.promise(function()
            local client = include("sftp").new({ program = "ssh", arguments = {}, directory = executableDirectory })
            local opening = async.promise(function() return client:start(1000) end)

            while writes == 0 do
                async.sleep(1):await()
            end

            launched.onOutput({ { stream = "output", text = packets.frame(types.version, packets.integer(3)) } })
            assert(checked(opening))
            local waiting = {}

            for index = 1, 64 do
                waiting[index] = async.promise(function()
                    local _, failure = client:request(types.stat, packets.string("file"), types.attrs, 1000)
                    return failure.code
                end)
            end

            while writes < 65 do
                async.sleep(1):await()
            end

            local _, full = client:request(types.stat, "", types.attrs, 1000)
            assert(full.code == "ssh_channel_full" and client.count == 64 and not client.closed)
            assert(client:close())

            for _, pending in ipairs(waiting) do
                assert(checked(pending) == "ssh_connection_closed")
            end

            assert(client.count == 0 and client.bytes == 0 and next(client.pending) == nil)
            host.test_result({ stopped = stopped, inputEnded = inputEnded, writes = writes, closed = client.closed })
        end)
    )lua");

    EXPECT_EQ(result, nlohmann::json({{"stopped", 0}, {"inputEnded", 1}, {"writes", 65}, {"closed", true}}));
}

TEST_F(RemoteSshModuleTest, ForwardsBinaryBytesAndHalfClosesThroughBoundedSocksNegotiation) {
    const auto result = run(R"lua(
        local bridge = require("workpane.bridge")
        local task = require("workpane.task")
        local resources = require("workpane.resources")
        require("workpane.lifecycle").start("test")
        environment.workpane.resources = { scope = function() return resources.scope("test") end }
        environment.workpane.future = function(work) return task.future("test", work) end
        environment.workpane.await = task.await
        task.run("test", "test", function()
            local tested = {}
            for _, address in ipairs({ "\1\0\0\0\0\0\0", "\4" .. string.rep("\0", 18), "\3\3abc\0\0" }) do
                local listener = checked(socket.tcp.listen("127.0.0.1", 0))
                local owner = { closed = false, state = "connected", channel = { port = listener.port } }
                local server = bridge.future(function()
                    local peer = checked(listener:accept())
                    assert(exact(peer, 3) == "\5\1\0")
                    checked(peer:send("\5\0"))
                    assert(exact(peer, 10) == "\5\1\0\1\127\0\0\1" .. string.pack(">I2", 4321))
                    for index = 1, #address + 3 do
                        checked(peer:send(("\5\0\0" .. address):sub(index, index)))
                    end
                    local payload = exact(peer, 100003)
                    assert(payload == "\0\255\128" .. string.rep("binary", 16666) .. "tail")
                    assert(checked(peer:receive()) == "")
                    checked(peer:send(payload:reverse()))
                    checked(peer:shutdown("send"))
                    checked(peer:close())
                    return {}
                end)
                local current = assert(include("forward").new(owner, 4321, 0, function() end))
                local opened = task.await(current:start())
                assert(opened.failure == nil and opened.value.localPort > 0)
                local client = checked(socket.tcp.connect("127.0.0.1", opened.value.localPort, { maxQueuedBytes = 131072 }))
                checked(client:send("\0\255\128" .. string.rep("binary", 16666) .. "tail"))
                checked(client:shutdown("send"))
                assert(exact(client, 100003) == ("\0\255\128" .. string.rep("binary", 16666) .. "tail"):reverse())
                assert(checked(client:receive()) == "")
                checked(client:close())
                task.await(server)
                task.await(current:close())
                assert(current.count == 0 and next(current.peers) == nil and current.problem == nil)
                local reopened = checked(socket.tcp.listen("127.0.0.1", opened.value.localPort))
                checked(reopened:close())
                checked(listener:close())
                tested[#tested + 1] = address:byte(1)
            end
            host.test_result(tested)
        end)
    )lua");

    EXPECT_EQ(result, nlohmann::json::array({1, 4, 3}));
}

TEST_F(RemoteSshModuleTest, RefusesInvalidOrOccupiedForwardsAndClosesPendingSocketReads) {
    const auto result = run(R"lua(
        local task = require("workpane.task")
        local resources = require("workpane.resources")
        require("workpane.lifecycle").start("test")
        environment.workpane.resources = { scope = function() return resources.scope("test") end }
        environment.workpane.future = function(work) return task.future("test", work) end
        environment.workpane.await = task.await
        task.run("test", "test", function()
            local listener = checked(socket.tcp.listen("127.0.0.1", 0))
            local owner = { closed = false, state = "connected", channel = { port = listener.port } }
            local forward = include("forward")
            local _, invalid = forward.new(owner, 0, 0, function() end)
            local _, fractional = forward.new(owner, 4321, 1.5, function() end)
            local occupied = assert(forward.new(owner, 4321, listener.port, function() end))
            local refused = task.await(occupied:start())
            local current = assert(forward.new(owner, 4321, 0, function() end))
            local opened = task.await(current:start())
            local client = checked(socket.tcp.connect("127.0.0.1", opened.value.localPort))
            local peer = checked(listener:accept())
            assert(exact(peer, 3) == "\5\1\0")
            local blocked = peer:receive()
            task.await(current:close())
            assert(checked(blocked) == "" and checked(client:receive()) == "")
            checked(client:close())
            checked(peer:close())
            checked(listener:close())
            owner.closed = true
            local _, disconnected = forward.new(owner, 4321, 0, function() end)
            host.test_result({ invalid = invalid.code, fractional = fractional.code, occupied = refused.failure.code, disconnected = disconnected.code, active = current.count })
        end)
    )lua");

    EXPECT_EQ(result["invalid"], "ssh_forward_invalid");
    EXPECT_EQ(result["fractional"], "ssh_forward_invalid");
    EXPECT_EQ(result["occupied"], "ssh_forward_bind_failed");
    EXPECT_EQ(result["disconnected"], "ssh_connection_closed");
    EXPECT_EQ(result["active"], 0);
}

TEST_F(RemoteSshModuleTest, RefusesMalformedAndRejectedSocksRepliesWithoutExposingTheirBytes) {
    const auto result = run(R"lua(
        local bridge = require("workpane.bridge")
        local task = require("workpane.task")
        local resources = require("workpane.resources")
        require("workpane.lifecycle").start("test")
        environment.workpane.resources = { scope = function() return resources.scope("test") end }
        environment.workpane.future = function(work) return task.future("test", work) end
        environment.workpane.await = task.await
        task.run("test", "test", function()
            local codes = {}
            for _, reply in ipairs({ "\4\0\0\1", "\5\0\1\1", "\5\0\0\2", "\5\0\0\3\0", "\5\5\0\1" }) do
                local listener = checked(socket.tcp.listen("127.0.0.1", 0))
                local owner = { closed = false, state = "connected", channel = { port = listener.port } }
                local server = bridge.future(function()
                    local peer = checked(listener:accept())
                    assert(exact(peer, 3) == "\5\1\0")
                    checked(peer:send("\5\0"))
                    exact(peer, 10)
                    checked(peer:send(reply))
                    assert(checked(peer:receive()) == "")
                    checked(peer:close())
                    return {}
                end)
                local current = assert(include("forward").new(owner, 4321, 0, function() end))
                local opened = task.await(current:start())
                local client = checked(socket.tcp.connect("127.0.0.1", opened.value.localPort))
                assert(checked(client:receive()) == "")
                task.await(server)
                while current.problem == nil do async.sleep(1):await() end
                assert(current.problem.detail == "")
                codes[#codes + 1] = current.problem.code
                checked(client:close())
                task.await(current:close())
                checked(listener:close())
            end
            host.test_result(codes)
        end)
    )lua");

    EXPECT_EQ(result, nlohmann::json::array({"ssh_forward_failed", "ssh_forward_failed", "ssh_forward_failed", "ssh_forward_failed", "ssh_forward_refused"}));
}

TEST_F(RemoteSshModuleTest, BoundsActiveForwardedPeersAndClosesThemWhileProxyNegotiationWaits) {
    const auto result = run(R"lua(
        local task = require("workpane.task")
        local resources = require("workpane.resources")
        require("workpane.lifecycle").start("test")
        environment.workpane.resources = { scope = function() return resources.scope("test") end }
        environment.workpane.future = function(work) return task.future("test", work) end
        environment.workpane.await = task.await
        task.run("test", "test", function()
            local listener = checked(socket.tcp.listen("127.0.0.1", 0))
            local owner = { closed = false, state = "connected", channel = { port = listener.port } }
            local current = assert(include("forward").new(owner, 4321, 0, function() end))
            local opened = task.await(current:start())
            local clients = {}
            local peers = {}
            for index = 1, 16 do
                clients[index] = checked(socket.tcp.connect("127.0.0.1", opened.value.localPort))
                peers[index] = checked(listener:accept())
                assert(exact(peers[index], 3) == "\5\1\0")
            end
            assert(current.count == 16)
            local excess = checked(socket.tcp.connect("127.0.0.1", opened.value.localPort))
            assert(checked(excess:receive()) == "" and current.count == 16)
            task.await(current:close())
            for index = 1, 16 do
                assert(checked(clients[index]:receive()) == "" and checked(peers[index]:receive()) == "")
                checked(clients[index]:close())
                checked(peers[index]:close())
            end
            checked(excess:close())
            checked(listener:close())
            host.test_result({ active = current.count, released = next(current.peers) == nil })
        end)
    )lua");

    EXPECT_EQ(result["active"], 0);
    EXPECT_EQ(result["released"], true);
}

TEST_F(RemoteSshModuleTest, EndsAStalledForwardedHandshakeAtItsDeadlineAndReleasesBothSockets) {
    const auto result = run(R"lua(
        local task = require("workpane.task")
        local resources = require("workpane.resources")
        require("workpane.lifecycle").start("test")
        environment.workpane.resources = { scope = function() return resources.scope("test") end }
        environment.workpane.future = function(work) return task.future("test", work) end
        environment.workpane.await = task.await
        task.run("test", "test", function()
            local listener = checked(socket.tcp.listen("127.0.0.1", 0))
            local owner = { closed = false, state = "connected", channel = { port = listener.port } }
            local current = assert(include("forward").new(owner, 4321, 0, function() end))
            local opened = task.await(current:start())
            local client = checked(socket.tcp.connect("127.0.0.1", opened.value.localPort))
            local peer = checked(listener:accept())
            assert(exact(peer, 3) == "\5\1\0")
            assert(checked(client:receive()) == "" and checked(peer:receive()) == "")
            while current.problem == nil do async.sleep(1):await() end
            task.await(current:close())
            checked(client:close())
            checked(peer:close())
            checked(listener:close())
            host.test_result({ code = current.problem.code, active = current.count })
        end)
    )lua");

    EXPECT_EQ(result["code"], "ssh_forward_failed");
    EXPECT_EQ(result["active"], 0);
}

TEST_F(RemoteSshModuleTest, BindsWorkspaceCapabilitiesToTheirConsumerAndConnection) {
    const auto result = run(R"lua(
        local task = require("workpane.task")
        local closed = {}
        local events = {}
        local onConnections
        local onPlugins
        local current = { id = "session", profile = { id = "profile", name = "Remote" }, identity = { root = "/remote", platform = "linux" }, closed = false }
        current.request = function(self, method, parameters)
            if method == "process.start" then
                return { process = 21, group = parameters.group }
            end

            return method == "files.stat" and { kind = "directory", path = "/remote" } or {}
        end
        current.requests = { request = function(self, method, parameters)
            assert(method == "process.group.close")
            closed[#closed + 1] = parameters.group
            return {}
        end }
        current.reserveGroup = function() return require("crypto").hexEncode(require("crypto").randomBytes(32)) end
        loaded.connections = { current = function() return current end, watch = function(handler)
            onConnections = handler
            return function() end
        end }
        environment.workpane.task = function(work, ...) task.run("test", "test", work, ...) end
        environment.workpane.future = function(work, ...) return task.future("test", work, ...) end
        environment.workpane.await = task.await
        environment.workpane.events = { publish = function(topic, value) events[#events + 1] = value end }
        environment.workpane.app = { watchPlugins = function(handler)
            onPlugins = handler
            return function() end
        end }

        task.run("test", "test", function()
            local workspace = include("workspace")
            workspace.start()
            local _, stale = pcall(workspace.open, { profile = "profile", session = "old" }, "editor")
            local first = workspace.open({ profile = "profile", session = "session" }, "editor")
            local second = workspace.open({ profile = "profile", session = "session" }, "server")
            local opened = workspace.request({ workspace = first.workspace, method = "files.stat", parameters = { path = "." } }, "editor")
            local process = workspace.request({ workspace = first.workspace, method = "process.start", parameters = { program = "server", directory = "." } }, "editor")
            local _, foreign = pcall(workspace.request, { workspace = first.workspace, method = "files.stat", parameters = { path = "." } }, "server")
            local _, reserved = pcall(workspace.request, { workspace = first.workspace, method = "process.stop", parameters = { process = 21, group = "foreign" } }, "editor")
            local _, unknown = pcall(workspace.request, { workspace = first.workspace, method = "session.ping", parameters = {} }, "editor")
            local _, extra = pcall(workspace.open, { profile = "profile", session = "session", extra = true }, "editor")
            onPlugins({ { id = "editor", state = "stopping" }, { id = "server", state = "running" } })
            local stopping = workspace.request({ workspace = first.workspace, method = "files.stat", parameters = { path = "." } }, "editor")
            onPlugins({ { id = "editor", state = "disabled" }, { id = "server", state = "running" } })

            while #closed == 0 do
                async.sleep(1):await()
            end

            local retained = workspace.request({ workspace = second.workspace, method = "files.stat", parameters = { path = "." } }, "server")
            current = { id = "replacement", profile = { id = "profile" }, closed = false }
            onConnections()
            workspace.stop()
            host.test_result({ first = first, opened = opened, stopping = stopping, retained = retained, process = process, stale = stale.code, foreign = foreign.code, reserved = reserved.code, unknown = unknown.code, extra = extra.code, closed = closed, events = events })
        end)
    )lua");

    EXPECT_EQ(result["first"]["root"], "/remote");
    EXPECT_EQ(result["first"]["platform"], "linux");
    EXPECT_EQ(result["opened"]["path"], "/remote");
    EXPECT_EQ(result["stopping"]["path"], "/remote");
    EXPECT_EQ(result["retained"]["path"], "/remote");
    EXPECT_EQ(result["process"]["group"], result["first"]["workspace"]);
    EXPECT_EQ(result["stale"], "ssh_connection_closed");
    EXPECT_EQ(result["foreign"], "ssh_workspace_unavailable");
    EXPECT_EQ(result["reserved"], "ssh_workspace_request_invalid");
    EXPECT_EQ(result["unknown"], "ssh_workspace_request_invalid");
    EXPECT_EQ(result["extra"], "ssh_workspace_request_invalid");
    EXPECT_EQ(result["closed"].size(), 2U);
    EXPECT_EQ(result["events"].size(), 4U);
}

TEST_F(RemoteSshModuleTest, ReleasesAProfilePickerSessionWhenPreparationFailsOrItsFormCloses) {
    const auto result = run(R"lua(
        local task = require("workpane.task")
        local resources = require("workpane.resources")
        require("workpane.lifecycle").start("test")
        environment.workpane.resources = { scope = function() return resources.scope("test") end }
        environment.workpane.future = function(work) return task.future("test", work) end
        environment.workpane.await = task.await
        local current
        local mode
        local started
        local notifyStarted
        local waiting
        local finishStart
        local dialogs = 0
        loaded.connections = { createPreview = function(profile)
            current = { id = "exact", profile = profile, closed = false }
            current.start = function()
                if mode == "closed" then
                    notifyStarted()
                    return nil, { code = "ssh_connection_closed", message = "Closed", detail = "" }
                end

                return environment.workpane.future(function()
                    notifyStarted()

                    if mode == "cancel" then
                        waiting:await()
                    end

                    if mode == "credentials-cancel" or mode == "connection-loss" then
                        current.closed = true
                        current.problem = mode == "connection-loss" and { code = "ssh_connection_lost", message = "Lost", detail = "" } or nil
                    end

                    if current.problem ~= nil then
                        return { failure = current.problem }
                    end

                    return mode == "select" and { value = true } or { failure = { code = "ssh_connection_closed", message = "Closed", detail = "" } }
                end)
            end
            return current
        end, closePreview = function(id)
            assert(id == current.profile.id)
            return environment.workpane.future(function()
                current.closed = true
                finishStart()
            end)
        end }
        environment.workpane.dialogs = { selectFolder = function(options)
            assert(options.workspace.profile == current.profile.id and options.workspace.session == current.id)
            dialogs = dialogs + 1
            return environment.workpane.future(function() return "/root/folder" end)
        end }
        task.run("test", "test", function()
            local results = {}

            for _, action in ipairs({ "select", "cancel", "failure", "closed", "credentials-cancel", "connection-loss" }) do
                mode = action
                dialogs = 0
                started, notifyStarted = async.deferred()
                waiting, finishStart = async.deferred()
                local live = true
                local scope
                local selection = environment.workpane.future(function()
                    return include("folder-picker").profile({ id = "preview", root = "/root" }, function() return live end, function(owned) scope = owned end)
                end)
                checked(started)

                if mode == "cancel" then
                    live = false
                    checked(scope:close())
                end

                local selected, failure = selection:await()
                assert(current.closed)
                results[#results + 1] = { selected = selected or "", dialogs = dialogs, code = failure ~= nil and failure.code or "" }
            end

            host.test_result(results)
        end)
    )lua");

    ASSERT_EQ(result.size(), 6U);
    EXPECT_EQ(result[0]["selected"], "/root/folder");
    EXPECT_EQ(result[0]["dialogs"], 1);
    EXPECT_EQ(result[0]["code"], "");
    EXPECT_EQ(result[1]["dialogs"], 0);
    EXPECT_EQ(result[1]["code"], "");
    EXPECT_EQ(result[2]["dialogs"], 0);
    EXPECT_EQ(result[2]["code"], "ssh_connection_closed");
    EXPECT_EQ(result[3]["dialogs"], 0);
    EXPECT_EQ(result[3]["code"], "ssh_connection_closed");
    EXPECT_EQ(result[4]["dialogs"], 0);
    EXPECT_EQ(result[4]["code"], "");
    EXPECT_EQ(result[5]["dialogs"], 0);
    EXPECT_EQ(result[5]["code"], "ssh_connection_lost");
}

TEST_F(RemoteSshModuleTest, OwnsTemporaryConnectionsWithoutReplacingSavedSessions) {
    const auto result = run(R"lua(
        local task = require("workpane.task")
        require("workpane.lifecycle").start("test")
        environment.workpane.future = function(work) return task.future("test", work) end
        environment.workpane.await = task.await
        environment.workpane.task = function(work) return task.run("test", "test", work) end
        environment.workpane.events = { publish = function() end }
        local profile = { id = "saved", name = "Saved", root = "/root" }
        local closed = {}
        loaded.preferences = { list = function() return { profile } end, find = function() return profile end, watch = function()
            return function() end
        end }
        loaded.session = { new = function(configuration)
            local current = { id = configuration.id .. "-session", profile = configuration, state = "preparing", closed = false }
            current.start = function(self)
                return async.promise(function()
                    if self.profile.name == "Failed" then
                        return { failure = { code = "ssh_connection_failed", message = "Failed", detail = "" } }
                    end

                    self.state = "connected"
                    self.identity = { root = "/canonical", platform = "linux" }
                    return { value = true }
                end)
            end
            current.close = function(self)
                return async.promise(function()
                    self.closed = true
                    self.state = "disconnected"
                    closed[self.profile.id] = (closed[self.profile.id] or 0) + 1
                    return {}
                end)
            end
            return current
        end }
        local function preview(profile)
            local current = include("connections").createPreview(profile)
            checked(current:start())
            return current
        end

        task.run("test", "test", function()
            local connections = include("connections")
            connections.start()
            checked(connections.connect("saved"))
            local saved = connections.current("saved")
            local first = preview(profile)
            assert(first ~= saved and first.profile.id ~= profile.id and connections.current(first.profile.id) == first)
            assert(connections.list({}).profiles[1].session == saved.id)
            checked(connections.closePreview(first.profile.id))
            checked(connections.closePreview(first.profile.id))
            assert(first.closed and closed[first.profile.id] == 1 and connections.current(first.profile.id) == nil)
            local failed = connections.createPreview({ id = "failed", name = "Failed", host = "host", user = "", port = 0, identity = "", configuration = "" })
            local outcome = checked(failed:start())
            local failure = outcome.failure
            assert(failure.code == "ssh_connection_failed")
            checked(connections.closePreview(failed.profile.id))
            local previews = {}

            for index = 1, 8 do
                previews[index] = preview(profile)
            end

            local _, limit = pcall(connections.createPreview, profile)
            assert(limit.code == "ssh_connection_busy")
            connections.stop()
            assert(saved.closed)

            for _, preview in ipairs(previews) do
                assert(preview.closed and closed[preview.profile.id] == 1)
            end

            local _, stopping = pcall(connections.createPreview, profile)
            host.test_result({ unchanged = profile.id, closed = #previews, failed = failure.code, limit = limit.code, stopping = stopping.code })
        end)
    )lua");

    EXPECT_EQ(result["unchanged"], "saved");
    EXPECT_EQ(result["closed"], 8);
    EXPECT_EQ(result["failed"], "ssh_connection_failed");
    EXPECT_EQ(result["limit"], "ssh_connection_busy");
    EXPECT_EQ(result["stopping"], "ssh_connection_busy");
}

TEST_F(RemoteSshModuleTest, PublishesConnectionSnapshotsWithoutCredentialsOrRepeatedPortUpdates) {
    const auto result = run(R"lua(
        local task = require("workpane.task")
        local profiles = { { id = "profile", name = "Development", root = "/configured", identity = "/private/key", configuration = "/private/config" } }
        local onProfiles
        local onSession
        local snapshots = {}
        loaded.preferences = { list = function() return profiles end, find = function() return profiles[1] end, watch = function(handler)
            onProfiles = handler
            return function() onProfiles = nil end
        end }
        loaded.session = { new = function(profile, changed)
            local current = { id = "exact-session", profile = profile, state = "preparing", closed = false }
            onSession = changed
            current.start = function(self)
                self.identity = { root = "/canonical", platform = "linux" }
                self.state = "connected"
                changed()
                return async.promise(function() return { value = true } end)
            end
            current.close = function(self)
                self.closed = true
                self.state = "disconnected"
                changed()
                return async.promise(function() return {} end)
            end
            return current
        end }
        environment.workpane.task = function(work) task.run("test", "test", work) end
        environment.workpane.events = { publish = function(topic, value)
            assert(topic == "remote-ssh.connections.changed")
            snapshots[#snapshots + 1] = value
        end }
        task.run("test", "test", function()
            local connections = include("connections")
            connections.start()
            local original = connections.list({})
            local _, unknown = pcall(connections.list, { credentials = true })
            checked(connections.connect("profile"))
            local connected = connections.list({})
            local count = #snapshots
            onSession()
            onSession()
            assert(#snapshots == count)
            profiles[1].name = "Renamed"
            onProfiles()
            assert(#snapshots == count + 1)
            checked(connections.disconnect("profile"))
            local disconnected = connections.list({})
            connections.stop()
            assert(onProfiles == nil)
            host.test_result({ original = original, connected = connected, disconnected = disconnected, invalid = unknown.code, snapshots = #snapshots })
        end)
    )lua");

    EXPECT_EQ(result["original"]["profiles"][0]["root"], "/configured");
    EXPECT_FALSE(result["original"]["profiles"][0].contains("session"));
    EXPECT_EQ(result["connected"]["profiles"][0]["root"], "/canonical");
    EXPECT_EQ(result["connected"]["profiles"][0]["session"], "exact-session");
    EXPECT_EQ(result["connected"]["profiles"][0].size(), 6U);
    EXPECT_FALSE(result["connected"]["profiles"][0].contains("identity"));
    EXPECT_FALSE(result["connected"]["profiles"][0].contains("configuration"));
    EXPECT_FALSE(result["disconnected"]["profiles"][0].contains("session"));
    EXPECT_EQ(result["invalid"], "ssh_connection_request_invalid");
    EXPECT_EQ(result["snapshots"], 4);
}

TEST_F(RemoteSshModuleTest, WaitsForAcceptedWorkspaceRequestsAfterTheirConsumerIsWithdrawn) {
    const auto result = run(R"lua(
        local task = require("workpane.task")
        local waiting, finish = async.deferred()
        local accepted = false
        local groupClosed = false
        local onPlugins
        local current = { id = "session", profile = { id = "profile", name = "Remote" }, identity = { root = "/remote", platform = "linux" }, closed = false }
        current.request = function(self, method)
            if method == "process.group.open" then
                return {}
            end

            assert(method == "files.write")
            accepted = true
            waiting:await()

            return { revision = string.rep("a", 64) }
        end
        current.requests = { request = function(self, method)
            assert(method == "process.group.close")
            groupClosed = true

            return {}
        end }
        current.reserveGroup = function() return require("crypto").hexEncode(require("crypto").randomBytes(32)) end
        loaded.connections = { current = function() return current end, watch = function() return function() end end }
        environment.workpane.task = function(work, ...) task.run("test", "test", work, ...) end
        environment.workpane.future = function(work, ...) return task.future("test", work, ...) end
        environment.workpane.await = task.await
        environment.workpane.events = { publish = function() end }
        environment.workpane.app = { watchPlugins = function(handler)
            onPlugins = handler

            return function() end
        end }
        task.run("test", "test", function()
            local workspace = include("workspace")
            workspace.start()
            local opened = workspace.open({ profile = "profile", session = "session" }, "editor")
            local requested = task.future("editor", function()
                return workspace.request({ workspace = opened.workspace, method = "files.write", parameters = {} }, "editor")
            end)

            while not accepted do
                async.sleep(1):await()
            end

            task.forget("editor")
            onPlugins({ { id = "editor", state = "disabled" } })
            local closed = false
            local closing = task.future("test", function()
                workspace.close({ workspace = opened.workspace }, "editor")
                closed = true
            end)

            while not groupClosed do
                async.sleep(1):await()
            end

            local waited = not closed
            local _, refused = requested:await()
            finish()
            task.await(closing)
            workspace.stop()
            host.test_result({ waited = waited, closed = closed, cancelled = refused.code })
        end)
    )lua");

    EXPECT_EQ(result["waited"], true);
    EXPECT_EQ(result["closed"], true);
    EXPECT_EQ(result["cancelled"], "plugin_not_running");
}

TEST_F(RemoteSshModuleTest, RetainsAcceptedDocumentCompletionAfterItsCallerDeadlineUntilConsumerCleanup) {
    const auto result = run(R"lua(
        local task = require("workpane.task")
        local waiting, finish = async.deferred()
        local providerFinished = false
        local groupClosed = false
        local onPlugins
        local current = { id = "session", profile = { id = "profile", name = "Remote" }, identity = { root = "/remote", platform = "linux" }, closed = false }
        current.request = function(_, method, _, _, context)
            if method == "process.group.open" then
                return {}
            end

            assert(method == "files.write" and context.owner.caller == "editor")
            context.provider = async.promise(function()
                checked(waiting)
                providerFinished = true
            end)

            return nil, include("protocol").failure("ssh_request_timeout")
        end
        current.requests = { request = function(_, method)
            assert(method == "process.group.close")
            groupClosed = true

            return {}
        end }
        current.reserveGroup = function() return require("crypto").hexEncode(require("crypto").randomBytes(32)) end
        loaded.connections = { current = function() return current end, watch = function() return function() end end }
        environment.workpane.task = function(work, ...) task.run("test", "test", work, ...) end
        environment.workpane.future = function(work, ...) return task.future("test", work, ...) end
        environment.workpane.await = task.await
        environment.workpane.events = { publish = function() end }
        environment.workpane.app = { watchPlugins = function(handler)
            onPlugins = handler

            return function() end
        end }
        task.run("test", "test", function()
            local workspace = include("workspace")
            workspace.start()
            local opened = workspace.open({ profile = "profile", session = "session" }, "editor")
            local requested = task.future("editor", function()
                return workspace.request({ workspace = opened.workspace, method = "files.write", parameters = {} }, "editor")
            end)
            local _, expired = requested:await()
            assert(expired.code == "ssh_request_timeout" and not providerFinished)

            for _ = 1, 31 do
                workspace.open({ profile = "profile", session = "session" }, "other")
            end

            onPlugins({ { id = "editor", state = "disabled" }, { id = "other", state = "running" } })
            local closed = false
            local closing = task.future("test", function()
                workspace.close({ workspace = opened.workspace }, "editor")
                closed = true
            end)

            while not groupClosed do
                checked(async.sleep(1))
            end

            local accepted, full = pcall(workspace.open, { profile = "profile", session = "session" }, "other")
            assert(not accepted and not closed and not providerFinished)
            finish()
            task.await(closing)
            local after = workspace.open({ profile = "profile", session = "session" }, "other")
            workspace.stop()
            host.test_result({ deadline = expired.code, full = full.code, finished = providerFinished, closed = closed, renewed = after.workspace ~= opened.workspace })
        end)
    )lua");

    EXPECT_EQ(result, nlohmann::json({{"deadline", "ssh_request_timeout"}, {"full", "ssh_workspace_limit"}, {"finished", true}, {"closed", true}, {"renewed", true}}));
}

TEST_F(RemoteSshModuleTest, RetainsWorkspaceAdmissionUntilACancelledOpeningFinishes) {
    const auto result = run(R"lua(
        local task = require("workpane.task")
        local waiting, finish = async.deferred()
        local onPlugins
        local opening
        local closes = 0
        local current = { id = "session", profile = { id = "profile", name = "Remote" }, identity = { root = "/remote", platform = "linux" }, closed = false }
        current.request = function(self, method, parameters)
            if opening == nil then
                opening = parameters.group
                waiting:await()
            end

            return {}
        end
        current.requests = { request = function(self, method, parameters)
            assert(method == "process.group.close")
            closes = closes + 1
            return {}
        end }
        current.reserveGroup = function() return require("crypto").hexEncode(require("crypto").randomBytes(32)) end
        loaded.connections = { current = function() return current end, watch = function() return function() end end }
        environment.workpane.task = function(work, ...) task.run("test", "test", work, ...) end
        environment.workpane.future = function(work, ...) return task.future("test", work, ...) end
        environment.workpane.await = task.await
        environment.workpane.events = { publish = function() end }
        environment.workpane.app = { watchPlugins = function(handler)
            onPlugins = handler
            return function() end
        end }

        task.run("test", "test", function()
            local workspace = include("workspace")
            workspace.start()
            local pending = task.future("test", function()
                return workspace.open({ profile = "profile", session = "session" }, "editor")
            end)

            while opening == nil do
                async.sleep(1):await()
            end

            onPlugins({ { id = "editor", state = "disabled" }, { id = "other", state = "running" } })

            while closes == 0 do
                async.sleep(1):await()
            end

            for _ = 1, 31 do
                workspace.open({ profile = "profile", session = "session" }, "other")
            end

            local _, full = pcall(workspace.open, { profile = "profile", session = "session" }, "other")
            finish()
            local value, problem = pending:await()
            local after = workspace.open({ profile = "profile", session = "session" }, "other")
            workspace.stop()
            host.test_result({ full = full.code, empty = value == nil, cancelled = problem.code, after = after.workspace ~= opening, closes = closes })
        end)
    )lua");

    EXPECT_EQ(result["full"], "ssh_workspace_limit");
    EXPECT_EQ(result["empty"], true);
    EXPECT_EQ(result["cancelled"], "ssh_connection_closed");
    EXPECT_EQ(result["after"], true);
    EXPECT_EQ(result["closes"], 33);
}

TEST_F(RemoteSshModuleTest, DeliversTerminalInputInOrderAndRetainsOutputDuringBackpressure) {
    const auto result = run(R"lua(
        local crypto = require("crypto")
        local node = { handlers = {}, commands = {} }
        function node:on(name, handler)
            self.handlers[name] = handler
        end

        local payload = string.rep("x", 150000) .. "\3"
        local allDelivered, deliveryFinished = async.deferred()
        local delivered = ""
        local acknowledged = 0
        local refused = 0
        local output = ""
        function node:command(name, arguments)
            if name == "feed" then
                if refused < 2 then
                    refused = refused + 1
                    error({ code = "terminal_stream_full" }, 0)
                end

                output = output .. crypto.base64Decode(arguments.content)
            elseif name == "acknowledge" then
                acknowledged = acknowledged + arguments.bytes
                assert(acknowledged <= #delivered)

                if acknowledged == #payload then
                    deliveryFinished()
                end
            end

            self.commands[#self.commands + 1] = { name = name, arguments = arguments }
        end

        environment.workpane.ui = { terminal = function(props) assert(props.transport == "stream") return node end }
        local sizes = {}
        local writes = {}
        local reads = 0
        local session = { terminals = {}, closed = false }
        function session:request(method, parameters)
            if method == "terminal.start" then
                return { terminal = 1 }
            end

            if method == "terminal.history.close" then
                return {}
            end

            assert(parameters.terminal == 1)

            if method == "terminal.write" then
                checked(async.promise(function() async.sleep(2):await() end))
                local bytes = crypto.base64Decode(parameters.content)
                writes[#writes + 1] = #bytes
                delivered = delivered .. bytes
                return {}
            end

            if method == "terminal.resize" then
                sizes[#sizes + 1] = { parameters.columns, parameters.rows }
                return {}
            end

            assert(method == "terminal.read")
            reads = reads + 1

            if reads > 1 then
                checked(allDelivered)
            end

            return { content = crypto.base64Encode(reads == 1 and "\27[31mremote\27[0m\r\n" or ""), directory = "/remote", ended = acknowledged == #payload, code = 7 }
        end

        async.run(function()
            local owner = include("terminal").new(session, function() error("A terminal unexpectedly failed") end)
            session.terminals[owner] = true
            node.handlers["open-stream"]({ generation = 1, columns = 80, rows = 24 })
            node.handlers["resize-stream"]({ generation = 1, columns = 120, rows = 40 })
            node.handlers["input-stream"]({ generation = 1, content = crypto.base64Encode(payload) })
            checked(owner.current.pending)
            checked(owner:close())
            host.test_result({ delivered = #delivered, exact = delivered == payload, writes = writes, acknowledged = acknowledged, output = output, refused = refused, sizes = sizes, last = node.commands[#node.commands], remaining = next(session.terminals) ~= nil })
        end)
    )lua");

    EXPECT_EQ(result["delivered"], 150001);
    EXPECT_EQ(result["exact"], true);
    EXPECT_EQ(result["writes"], nlohmann::json::array({65536, 65536, 18929}));
    EXPECT_EQ(result["acknowledged"], 150001);
    EXPECT_EQ(result["output"], "\x1b[31mremote\x1b[0m\r\n");
    EXPECT_EQ(result["refused"], 2);
    EXPECT_EQ(result["sizes"], nlohmann::json::array({{120, 40}}));
    EXPECT_EQ(result["last"]["name"], "end-stream");
    EXPECT_EQ(result["last"]["arguments"]["code"], 7);
    EXPECT_EQ(result["remaining"], false);
}

TEST_F(RemoteSshModuleTest, ClosesAnAcceptedTerminalStartBeforeOpeningItsNextGeneration) {
    const auto result = run(R"lua(
        local crypto = require("crypto")
        local node = { handlers = {}, commands = {} }
        function node:on(name, handler)
            self.handlers[name] = handler
        end

        function node:command(name, arguments)
            self.commands[#self.commands + 1] = { name = name, arguments = arguments }
        end

        environment.workpane.ui = { terminal = function() return node end }
        local accepted, accept = async.deferred()
        local release, resume = async.deferred()
        local methods = {}
        local starts = 0
        local failures = 0
        local history
        local session = { terminals = {}, closed = false }
        function session:request(method, parameters)
            methods[#methods + 1] = { method, parameters.terminal or 0 }

            if method == "terminal.start" then
                assert(type(parameters.history) == "string" and #parameters.history == 32)

                if history ~= nil then
                    assert(history == parameters.history)
                end

                history = parameters.history
                starts = starts + 1

                if starts == 1 then
                    accept()
                    release:await()
                end

                return { terminal = starts }
            end

            if method == "terminal.read" then
                return { content = "PRIVATE invalid Base64", directory = "/remote", ended = false, code = 0 }
            end

            assert(method == "terminal.close" or method == "terminal.history.close")

            return {}
        end

        async.run(function()
            local owner = include("terminal").new(session, function(problem) assert(problem.code == "ssh_terminal_failed") failures = failures + 1 end)
            session.terminals[owner] = true
            node.handlers["open-stream"]({ generation = 1, columns = 80, rows = 24 })
            accepted:await()
            node.handlers["close-stream"]({ generation = 1 })
            node.handlers["open-stream"]({ generation = 2, columns = 90, rows = 30 })
            node.handlers["input-stream"]({ generation = 1, content = crypto.base64Encode("stale") })
            assert(starts == 1)
            resume()
            checked(owner.current.pending)
            checked(owner:close())
            host.test_result({ methods = methods, failures = failures, commands = node.commands, remaining = next(session.terminals) ~= nil })
        end)
    )lua");

    EXPECT_EQ(result["methods"], nlohmann::json::array({{"terminal.start", 0}, {"terminal.close", 1}, {"terminal.start", 0}, {"terminal.read", 2}, {"terminal.close", 2}, {"terminal.history.close", 0}}));
    EXPECT_EQ(result["failures"], 1);
    ASSERT_EQ(result["commands"].size(), 1U);
    EXPECT_EQ(result["commands"][0]["name"], "end-stream");
    EXPECT_EQ(result["commands"][0]["arguments"]["generation"], 2);
    EXPECT_EQ(result["remaining"], false);
    EXPECT_EQ(result.dump().find("PRIVATE"), std::string::npos);
}

TEST_F(RemoteSshModuleTest, ConnectsThroughOwnedNativeSshAndSftpAndReleasesEverySessionResource) {
    for (const std::string remotePlatform : {"macos", "windows"}) {
        const auto result = run(R"lua(
        local remotePlatform = )lua" +
                                nlohmann::json(remotePlatform).dump() + R"lua(
        local root = remotePlatform == "windows" and "C:/project" or "/project"
        local private = remotePlatform == "windows" and "C:/private" or "/private"
        local released = {}
        local stages = {}
        local writes = {}
        local created
        environment.workpane.app = { dataDirectory = executableDirectory }
        environment.workpane.task = function(handler) async.run(handler) end
        environment.workpane.process = { find = function(name)
            assert(name == "ssh")

            return async.promise(function() return executableDirectory .. "/ssh" end)
        end }
        loaded.discovery = { start = function()
            return { read = function() return remotePlatform == "windows" and "windows" or "posix" end, close = function() released.discovery = true end }
        end }
        loaded.source = { asset = function(name) return name end, prepare = function(platform)
            assert(platform == (remotePlatform == "windows" and "windows" or "linux"))

            return { command = "native-scope", input = platform == "windows" and "source\n" or "" }
        end }
        loaded.authentication = { new = function()
            return { channel = function() return { variables = {} } end, close = function()
                released.authentication = true

                return async.promise(function() end)
            end }
        end }
        loaded.program = { new = function(executable, arguments, _, input)
            assert(executable == executableDirectory .. "/ssh" and input == nil)
            assert(arguments[#arguments - 1] == "host" and arguments[#arguments] == "native-scope")

            return { line = function(_, prefix)
                if prefix == "PLATFORM " then
                    return prefix .. remotePlatform
                end

                assert(prefix == "SCOPE ")

                return prefix .. private .. " 123"
            end, write = function(_, value) writes[#writes + 1] = value end, close = function() released.connection = true end }
        end }
        loaded.scope = { new = function(connection, platform, directory, owner)
            assert(platform == remotePlatform and directory == private and owner == 123)

            return { account = function() return { account = "Reader", home = root, shell = root .. "/shell" } end, resolve = function(_, boundary, path)
                assert(boundary == root and path == root)

                return root
            end, close = function()
                released.scope = true
                connection:close()

                return async.promise(function() return { value = true } end)
            end }
        end }
        loaded.sftp = { new = function(options)
            assert(options.program == executableDirectory .. "/ssh")
            assert(options.arguments[#options.arguments - 2] == "-s" and options.arguments[#options.arguments - 1] == "host" and options.arguments[#options.arguments] == "sftp")

            return { start = function() return true end, close = function() released.client = true return true end }
        end }
        loaded["sftp-files"] = { new = function() return { realpath = function(_, path) return path end } end }
        loaded.backend = { new = function(_, _, _, _, host, _, boundary)
            assert(host == "host" and boundary == root)

            return { request = function(_, method, parameters)
                assert(method == "files.stat" and parameters.path == ".")

                return { kind = "directory", path = root }
            end, cancel = function() end, stop = function()
                released.backend = true

                return async.promise(function() end)
            end }
        end }
        async.run(function()
            created = assert(include("session").new({ id = "profile", name = "Development", host = "host", user = "", port = 0, identity = "", configuration = "", root = root, timeoutSeconds = 30, savePassword = false }, function() stages[#stages + 1] = created.state end))
            local outcome = checked(created:start())
            assert(outcome.value and created.state == "connected", outcome.failure ~= nil and outcome.failure.code or created.state)
            local closing = created:close()
            assert(created:close() == closing)
            assert(checked(closing).failure == nil)
            host.test_result({ stages = stages, released = released, state = created.state, writes = #writes })
        end)
    )lua");

        EXPECT_EQ(result["stages"], nlohmann::json::array({"detecting", "connecting", "transferring", "connected", "closing", "disconnected"}));
        EXPECT_EQ(result["state"], "disconnected");
        EXPECT_EQ(result["writes"], remotePlatform == "windows" ? 1 : 0);
        EXPECT_EQ(result["released"], nlohmann::json({{"authentication", true}, {"client", true}, {"backend", true}, {"connection", true}, {"scope", true}, {"discovery", true}}));
    }
}

TEST_F(RemoteSshModuleTest, ContinuesCleanupAfterAResourceFailsAndReturnsOnlyASafeReason) {
    const auto result = run(R"lua(
        local created = assert(include("session").new({ id = "profile", name = "Development", host = "host", user = "", port = 0, identity = "", configuration = "", root = "/project", timeoutSeconds = 30, savePassword = false }, function() end))
        local released = {}
        created.authentication = { close = function() error("PRIVATE credential", 0) end }
        created.client = { close = function() released.client = true return true end }
        created.connection = { close = function() released.connection = true end }
        async.run(function()
            local closing = created:close()
            assert(created:close() == closing)
            local outcome = checked(closing)
            host.test_result({ failure = outcome.failure, state = created.state, released = released })
        end)
    )lua");

    EXPECT_EQ(result["state"], "failed");
    EXPECT_EQ(result["failure"]["code"], "ssh_cleanup_failed");
    EXPECT_EQ(result["failure"]["detail"], "");
    EXPECT_EQ(result.dump().find("PRIVATE"), std::string::npos);
    EXPECT_EQ(result["released"], nlohmann::json({{"client", true}, {"connection", true}}));
}

TEST_F(RemoteSshModuleTest, CancelsPreparationBeforeStartingAuthenticationOrPrograms) {
    const auto result = run(R"lua(
        local entered, enter = async.deferred()
        local prepared, prepare = async.deferred()
        local inspected = false
        environment.workpane.process = { find = function()
            enter()

            return async.promise(function()
                checked(prepared)
                inspected = true

                return executableDirectory .. "/ssh"
            end)
        end }
        async.run(function()
            local created = assert(include("session").new({ id = "profile", name = "Development", host = "host", user = "", port = 0, identity = "", configuration = "", root = "/project", timeoutSeconds = 30, savePassword = false }, function() end))
            local pending = created:start()
            checked(entered)
            local closing = created:close()
            prepare()
            checked(closing)
            local result = checked(pending)
            host.test_result({ code = result.failure.code, state = created.state, inspected = inspected, noPrograms = created.connection == nil and created.authentication == nil })
        end)
    )lua");

    EXPECT_EQ(result["code"], "ssh_connection_closed");
    EXPECT_EQ(result["state"], "disconnected");
    EXPECT_EQ(result["inspected"], true);
    EXPECT_EQ(result["noPrograms"], true);
}

TEST_F(RemoteSshModuleTest, RefusesAFailedControlWaitWithoutSpinning) {
    const auto result = run(R"lua(
        environment.workpane.app = { dataDirectory = directory }
        environment.workpane.process = { start = function() return 1 end }
        local created = include("program").new("/usr/bin/ssh", {}, {}, nil)
        local rejected = async.promise(function()
            error("The control wait failed")
        end)

        async.run(function()
            local accepted, problem = pcall(created.line, created, "SCOPE ", 1000, { closed = false, finished = rejected })
            host.test_result({ accepted = accepted, code = problem.code })
        end)
    )lua");

    EXPECT_EQ(result["accepted"], false);
    EXPECT_EQ(result["code"], "ssh_program_failed");
}

TEST_F(RemoteSshModuleTest, StopsReadingControlOutputAfterTimeoutAndBoundsItsBuffer) {
    const auto result = run(R"lua(
        local options
        local stops = 0
        environment.workpane.app = { dataDirectory = directory }
        environment.workpane.process = { start = function(value) options = value return 1 end, stop = function() stops = stops + 1 end }
        local created = include("program").new("/usr/bin/ssh", {}, {}, nil)
        local finished, finish = async.deferred()
        local owner = { closed = false, finished = finished, finish = finish }
        async.run(function()
            local accepted, problem = pcall(created.line, created, "SCOPE ", 10, owner)
            assert(not accepted and problem.code == "ssh_connection_timeout")
            options.onOutput({ { stream = "output", text = "SCOPE valid\n" } })
            local line = created:line("SCOPE ", 1000, owner)
            options.onOutput({ { stream = "output", text = string.rep("x", 65537) } })
            options.onOutput({ { stream = "output", text = "PRIVATE credential" } })
            options.onExit(1, false)
            created:close()
            host.test_result({ code = problem.code, line = line, stops = stops, bounded = #created.pending <= 65536, failure = created.failure })
        end)
    )lua");

    EXPECT_EQ(result["code"], "ssh_connection_timeout");
    EXPECT_EQ(result["line"], "SCOPE valid");
    EXPECT_EQ(result["stops"], 1);
    EXPECT_EQ(result["bounded"], true);
    EXPECT_EQ(result["failure"], "ssh_output_invalid");
}

TEST_F(RemoteSshModuleTest, KeepsRequestsInOrderAndReservesCapacityForLeaseRenewal) {
    const auto result = run(R"lua(
        async.run(function()
            local started = {}
            local release = {}
            local queue
            local calls = {}
            local parameters = {}
            local transport = { cancel = function() end, request = function(_, method, parameters, cancellation)
                if method == "session.ping" then
                    assert(queue.active == 3 and #queue.waiting == 5)

                    return { renewed = true }
                end

                started[#started + 1] = parameters.index
                local finished, finish = async.deferred()
                release[parameters.sequence] = finish
                checked(async.race({ finished, cancellation.finished }))

                if cancellation.closed then
                    error(include("protocol").failure("ssh_connection_closed"), 0)
                end

                return { index = parameters.index }
            end }
            queue = include("requests").new(transport)

            for index = 1, 8 do
                parameters[index] = { index = index, sequence = index }
                calls[index] = async.promise(function()
                    local value, failure = queue:request("files.stat", parameters[index], 2000)
                    assert(failure == nil)

                    return value.index
                end)
            end

            while #started < 3 do checked(async.sleep(1)) end
            parameters[8].index = 99
            local ping = queue:ping(100)

            for index = 1, 8 do
                while release[index] == nil do checked(async.sleep(1)) end
                release[index]()
                checked(calls[index])
            end

            checked(queue:close())
            host.test_result({ started = started, renewed = ping.renewed, active = queue.active, waiting = #queue.waiting, released = next(queue.operations) == nil })
        end)
    )lua");

    EXPECT_EQ(result["started"], nlohmann::json::array({1, 2, 3, 4, 5, 6, 7, 8}));
    EXPECT_EQ(result["renewed"], true);
    EXPECT_EQ(result["active"], 0);
    EXPECT_EQ(result["waiting"], 0);
    EXPECT_EQ(result["released"], true);
}

TEST_F(RemoteSshModuleTest, BoundsQueuedRequestsExpiresBeforeAdmissionAndDisconnectsAllWaiters) {
    const auto result = run(R"lua(
        async.run(function()
            local started = 0
            local transport = { cancel = function() end, request = function(_, method, parameters, cancellation)
                started = started + 1
                checked(cancellation.finished)

                error(include("protocol").failure("ssh_connection_closed"), 0)
            end }
            local queue = include("requests").new(transport)
            local pending = {}

            for index = 1, 3 do
                pending[#pending + 1] = async.promise(function()
                    local _, failure = queue:request("files.stat", { index = index }, 2000)

                    return failure.code
                end)
            end

            while started < 3 do checked(async.sleep(1)) end
            local _, expired = queue:request("files.stat", {}, 10)
            assert(#queue.waiting == 0 and started == 3)

            for index = 1, 64 do
                pending[#pending + 1] = async.promise(function()
                    local _, failure = queue:request("files.stat", { index = index }, 2000)

                    return failure.code
                end)
            end

            while #queue.waiting < 64 do checked(async.sleep(1)) end
            local _, refused = queue:request("files.stat", {}, 100)
            checked(queue:close())
            local outcomes = checked(async.all(pending))
            local _, after = queue:request("files.stat", {}, 100)
            host.test_result({ expired = expired.code, refused = refused.code, after = after.code, started = started, outcomes = outcomes, active = queue.active, waiting = #queue.waiting, released = next(queue.operations) == nil })
        end)
    )lua");

    EXPECT_EQ(result["expired"], "ssh_request_timeout");
    EXPECT_EQ(result["refused"], "ssh_channel_full");
    EXPECT_EQ(result["after"], "ssh_connection_closed");
    EXPECT_EQ(result["started"], 3);
    ASSERT_EQ(result["outcomes"].size(), 67U);

    for (const auto& outcome : result["outcomes"]) {
        EXPECT_EQ(outcome, "ssh_connection_closed");
    }

    EXPECT_EQ(result["active"], 0);
    EXPECT_EQ(result["waiting"], 0);
    EXPECT_EQ(result["released"], true);
}

TEST_F(RemoteSshModuleTest, BoundsTheBytesRetainedByQueuedFileWritesAndReleasesThemOnDisconnect) {
    const auto result = run(R"lua(
        async.run(function()
            local started = 0
            local transport = { cancel = function() end, request = function(_, _, _, cancellation)
                started = started + 1
                checked(cancellation.finished)

                error(include("protocol").failure("ssh_connection_closed"), 0)
            end }
            local queue = include("requests").new(transport)
            local payload = string.rep("a", 4 * 1024 * 1024)
            local pending = {}

            for index = 1, 15 do
                pending[index] = async.promise(function()
                    local _, failure = queue:request("files.write", { path = "file.txt", content = payload, revision = "" }, 2000)

                    return failure.code
                end)
            end

            while #queue.waiting < 12 do checked(async.sleep(1)) end
            local _, refused = queue:request("files.write", { path = "file.txt", content = payload, revision = "" }, 100)
            local bounded = queue.bytes <= 64 * 1024 * 1024
            checked(queue:close())
            local outcomes = checked(async.all(pending))
            host.test_result({ refused = refused.code, bounded = bounded, started = started, bytes = queue.bytes, waiting = #queue.waiting, count = #outcomes })
        end)
    )lua");

    EXPECT_EQ(result["refused"], "ssh_channel_full");
    EXPECT_EQ(result["bounded"], true);
    EXPECT_EQ(result["started"], 3);
    EXPECT_EQ(result["bytes"], 0);
    EXPECT_EQ(result["waiting"], 0);
    EXPECT_EQ(result["count"], 15);
}

TEST_F(RemoteSshModuleTest, KeepsSilentProgramReadsPendingAndRetainsCapacityForOwnedStopping) {
    const auto result = run(R"lua(
        async.run(function()
            local waiting, finish = async.deferred()
            local started = false
            local ended = false
            local transport = { cancel = function() end, request = function(_, method)
                if method == "process.read" then
                    started = true
                    checked(waiting)

                    return { ended = true }
                end

                if method == "process.stop" then
                    finish()

                    return {}
                end

                assert(method == "files.stat")

                return { inspected = true }
            end }
            local queue = include("requests").new(transport)
            local reading = async.promise(function()
                local value, failure = queue:request("process.read", {}, 1)
                assert(failure == nil)
                ended = value.ended
            end)

            while not started do
                checked(async.sleep(1))
            end

            checked(async.sleep(10))
            assert(not ended and queue.active == 0)
            local inspected = queue:request("files.stat", {}, 100)
            assert(inspected.inspected and not ended)
            assert(queue:request("process.stop", {}, 100))
            checked(reading)
            checked(queue:close())
            host.test_result({ ended = ended, count = queue.count, bytes = queue.bytes, active = queue.active })
        end)
    )lua");

    EXPECT_EQ(result, nlohmann::json({{"ended", true}, {"count", 0}, {"bytes", 0}, {"active", 0}}));
}

TEST_F(RemoteSshModuleTest, RetainsAcceptedFileWorkAfterDeadlineAndJoinsItDuringDisconnect) {
    const auto result = run(R"lua(
        async.run(function()
            local waiting, finish = async.deferred()
            local accepted = false
            local settled = false
            local transport = { cancel = function() end, request = function(_, method)
                assert(method == "files.write")
                accepted = true
                checked(waiting)
                settled = true

                return { revision = string.rep("a", 64) }
            end }
            local queue = include("requests").new(transport)
            local context = { owner = { closed = false } }
            local _, expired = queue:request("files.write", {}, 10, context)
            assert(expired.code == "ssh_request_timeout" and accepted and not settled and queue.count == 1)
            local closed = false
            local closing = async.promise(function()
                checked(queue:close())
                closed = true
            end)
            checked(async.sleep(1))
            assert(not closed and not settled)
            finish()
            checked(closing)
            checked(context.provider)
            host.test_result({ expired = expired.code, closed = closed, settled = settled, bytes = queue.bytes, count = queue.count })
        end)
    )lua");

    EXPECT_EQ(result, nlohmann::json({{"expired", "ssh_request_timeout"}, {"closed", true}, {"settled", true}, {"bytes", 0}, {"count", 0}}));
}

TEST_F(RemoteSshModuleTest, DeliversAnAcceptedSaveRevisionWhenTheConnectionClosesBeforeItsReply) {
    const auto result = run(R"lua(
        async.run(function()
            local waiting, finish = async.deferred()
            local started = false
            local delivered = false
            local transport = { cancel = function() end, request = function(_, method)
                assert(method == "files.write")
                started = true
                checked(waiting)

                return { revision = string.rep("a", 64) }
            end }
            local queue = include("requests").new(transport)
            local saving = async.promise(function()
                local value, problem = queue:request("files.write", {}, 1000)
                assert(problem == nil and value.revision == string.rep("a", 64))
                delivered = true
            end)

            while not started do
                checked(async.sleep(1))
            end

            local closing = queue:close()
            checked(async.sleep(1))
            assert(not delivered and queue.count == 1)
            finish()
            checked(saving)
            checked(closing)
            local _, refused = queue:request("files.write", {}, 1000)
            host.test_result({ delivered = delivered, refused = refused.code, count = queue.count })
        end)
    )lua");

    EXPECT_EQ(result, nlohmann::json({{"delivered", true}, {"refused", "ssh_connection_closed"}, {"count", 0}}));
}

TEST_F(RemoteSshModuleTest, ClearsAMaskedCredentialBeforeAnsweringAndCancelsAnOpenPrompt) {
    const auto result = run(R"lua(
        local input
        local alert
        local options
        local answer, finish
        local button
        local listenerClosed = false
        local dialogClosed = false
        environment.workpane.ui = {
            label = function(value) return value end,
            column = function(_, children) return children end,
            textField = function(properties)
                assert(properties.password)
                input = { value = properties.value, get = function(self) return self.value end, set = function(self, patch) self.value = patch.value end, command = function() end, on = function(self, event, handler) assert(event == "submit") self.submit = handler end }

                return input
            end,
            alert = function(properties)
                alert = { visible = properties.visible, set = function(self, patch) self.visible = patch.visible end }

                return alert
            end,
        }

        environment.workpane.process = { askpass = function(handlers)
            return { variables = { token = "PRIVATE" }, close = function()
                handlers.onCancel()
                listenerClosed = true

                return async.promise(function() end)
            end }
        end }
        environment.workpane.dialogs = { custom = function(value)
            options = value
            answer, finish = async.deferred()
            button = nil

            return { close = function(_, selected)
                dialogClosed = true
                assert(input == nil or input.value == "")
                button = selected
                finish()
            end, await = function() checked(answer) return button end }
        end }
        local authentication = include("authentication").new()
        local firstChannel = authentication:channel()
        local secondChannel = authentication:channel()
        local queuedChannel = authentication:channel()
        local lastChannel = authentication:channel()
        async.run(function()
            local first = async.promise(function() return authentication:prompt(firstChannel, "reader@host\'s password: ", "input") end)
            while options == nil do checked(async.sleep(1)) end
            input.value = string.rep("x", 1024)
            input.submit()
            assert(alert.visible and not dialogClosed)
            input.value = "PRIVATE password"
            input.submit()
            assert(checked(first) == "PRIVATE password" and input.value == "")
            options = nil
            assert(authentication:prompt(secondChannel, "reader@host\'s password: ", "input") == "PRIVATE password" and options == nil)
            local second = async.promise(function() return authentication:prompt(secondChannel, "reader@host\'s password: ", "input") end)
            while options == nil do checked(async.sleep(1)) end
            local activeDialog = options
            local queued = async.promise(function() return authentication:prompt(queuedChannel, "Verification code: ", "input") end)
            local last = async.promise(function() return authentication:prompt(lastChannel, "Other challenge: ", "input") end)
            checked(queuedChannel:close())
            assert(checked(queued) == nil and options == activeDialog)
            checked(async.sleep(1))
            assert(options == activeDialog)
            input.value = "PRIVATE cancelled password"
            checked(authentication:close())
            assert(checked(last) == nil and authentication.count == 0 and next(authentication.credentials) == nil)
            local cancelled = checked(second)
            host.test_result({ masked = true, cleared = input.value == "", cancelled = cancelled == nil, listenerClosed = listenerClosed, afterClose = authentication:prompt(secondChannel, "reader@host\'s password: ", "input") == nil })
        end)
    )lua");

    EXPECT_EQ(result, nlohmann::json({{"masked", true}, {"cleared", true}, {"cancelled", true}, {"listenerClosed", true}, {"afterClose", true}}));
}

TEST_F(RemoteSshModuleTest, ClassifiesSshFailuresWithoutReturningTheirPrivateTranscript) {
    const auto result = run(R"lua(
        local options
        environment.workpane.app = { dataDirectory = directory }
        environment.workpane.process = { start = function(value) options = value return 1 end }
        local samples = {
            "REMOTE HOST IDENTIFICATION HAS CHANGED",
            "Host key verification failed",
            "Permission denied",
            "Could not resolve hostname",
            "Connection timed out",
            "Connection refused",
            "subsystem request failed",
            "Unknown failure",
        }

        async.run(function()
            local codes = {}
            local bounded = true
            local cleared = true

            for _, sample in ipairs(samples) do
                local created = include("program").new("/usr/bin/ssh", {}, {}, nil)
                local finished, finish = async.deferred()
                options.onOutput({ { stream = "error", text = string.rep("PRIVATE", 4000) .. sample:sub(1, 5) } })
                options.onOutput({ { stream = "error", text = sample:sub(6) .. " PRIVATE password\n" } })
                bounded = bounded and #created.diagnostic <= 16384
                options.onExit(255, false)
                local accepted, failure = pcall(created.line, created, "SCOPE ", 100, { closed = false, finished = finished, finish = finish })
                assert(not accepted and failure.detail == "")
                codes[#codes + 1] = failure.code
                cleared = cleared and created.diagnostic == nil
            end

            host.test_result({ codes = codes, bounded = bounded, cleared = cleared })
        end)
    )lua");

    EXPECT_EQ(result["codes"], nlohmann::json::array({"ssh_host_key_changed", "ssh_host_key_changed", "ssh_authentication_refused", "ssh_host_unresolved", "ssh_connection_timeout", "ssh_host_refused", "ssh_transfer_unavailable", "ssh_program_failed"}));
    EXPECT_EQ(result["bounded"], true);
    EXPECT_EQ(result["cleared"], true);
    EXPECT_EQ(result.dump().find("PRIVATE"), std::string::npos);
}

TEST_F(RemoteSshModuleTest, JoinsCancelledPlatformDiscoveryBeforeReleasingItsCredentialChannel) {
    const auto result = run(R"lua(
        local waiting, finish = async.deferred()
        local stopping, stop = async.deferred()
        local credentialCloses = 0
        local processCloses = 0
        loaded.program = { new = function(_, arguments, _, input)
            assert(arguments[#arguments - 1] == "host" and input == "")

            return { close = function()
                processCloses = processCloses + 1
                stop()
                waiting:await()
            end, line = function(_, prefix, _, cancellation)
                assert(prefix == "WORKPANE_nonce:")
                cancellation.finished:await()
                error({ code = "ssh_connection_closed" }, 0)
            end }
        end }
        local authentication = { channel = function()
            return { variables = {}, close = function()
                return async.promise(function() credentialCloses = credentialCloses + 1 end)
            end }
        end }
        async.run(function()
            local discovery = include("discovery").start("ssh", { "-T" }, "host", authentication, "nonce")
            local finished, cancel = async.deferred()
            local cancellation = { closed = false, finished = finished }
            local reading = async.promise(function()
                local accepted, failure = pcall(discovery.read, discovery, 1000, cancellation)
                assert(not accepted)

                return failure.code
            end)
            cancellation.closed = true
            cancel()
            local first = async.promise(function() discovery:close() end)
            stopping:await()
            local second = async.promise(function() discovery:close() end)
            assert(processCloses == 1 and credentialCloses == 0)
            finish()
            local completed = checked(async.allSettled({ reading, first, second }))
            discovery:close()
            host.test_result({ processes = processCloses, credentials = credentialCloses, completed = completed })
        end)
    )lua");

    EXPECT_EQ(result["processes"], 1);
    EXPECT_EQ(result["credentials"], 1);
    ASSERT_EQ(result["completed"].size(), 3U) << result;
    EXPECT_TRUE(result["completed"][0]["ok"].get<bool>()) << result;
    EXPECT_EQ(result["completed"][0]["value"], "ssh_connection_closed") << result;
    EXPECT_TRUE(result["completed"][1]["ok"].get<bool>()) << result;
    EXPECT_TRUE(result["completed"][2]["ok"].get<bool>()) << result;
}

TEST_F(RemoteSshModuleTest, DiscoversNativeCommandAndPowerShellHostsWithoutGuessingFromTheirAddress) {
    const auto result = run(R"lua(
        local discovery = include("discovery")
        local nonce = "0123456789abcdef0123456789abcdef"
        local marker = "WORKPANE_" .. nonce
        local values = {
            discovery.platform(marker .. ":Windows_NT:$env:OS", nonce),
            discovery.platform(marker .. ":%OS%:Windows_NT", nonce),
            discovery.platform(marker .. ":%OS%::OS", nonce),
            discovery.platform(marker .. ":%OS%:account-value:OS", nonce),
        }

        local refused = {}

        for _, line in ipairs({ marker .. ":%OS%:Linux", "WORKPANE_other:%OS%::OS", marker .. ":Windows_NT:other", marker .. ":%OS%::OS unexpected" }) do
            local accepted, failure = pcall(discovery.platform, line, nonce)
            assert(not accepted)
            refused[#refused + 1] = failure.code
        end

        host.test_result({ platforms = values, refused = refused, command = discovery.command(nonce) })
    )lua");

    EXPECT_EQ(result["platforms"], nlohmann::json::array({"windows", "windows", "posix", "posix"}));
    EXPECT_EQ(result["refused"], nlohmann::json::array({"ssh_platform_unavailable", "ssh_platform_unavailable", "ssh_platform_unavailable", "ssh_platform_unavailable"}));
    EXPECT_EQ(result["command"], "echo WORKPANE_0123456789abcdef0123456789abcdef:%OS%:$env:OS");
}

TEST_F(RemoteSshModuleTest, ClosesRemoteTerminalPagesAndReopensThemWithoutRetainingTheirOwners) {
    const auto result = run(R"lua(
        local nodes = {}
        environment.workpane.ui = setmetatable({}, { __index = function(_, kind)
            return function(properties, children)
                local node = { kind = kind, properties = properties, children = children or {}, handlers = {} }

                function node:set(patch)
                    for key, value in pairs(patch) do self.properties[key] = value end
                end

                function node:setChildren(children, patch)
                    self.children = children
                    self:set(patch)
                end

                function node:on(event, handler) self.handlers[event] = handler end

                function node:command() end

                nodes[#nodes + 1] = node

                return node
            end
        end })
        local subscriptions = 0
        local watch = function()
            subscriptions = subscriptions + 1

            return function() subscriptions = subscriptions - 1 end
        end
        local closed = 0
        local owners = {}
        local session = { id = "session", identity = { root = "/remote", platform = "linux" }, terminals = {}, forwards = { forward = { id = "forward", remotePort = 3000, localPort = 4321 } } }

        function session:openTerminal()
            local owner = { node = environment.workpane.ui.terminal({}) }

            function owner:close()
                assert(not self.closed)
                self.closed = true
                closed = closed + 1
            end

            owners[#owners + 1] = owner

            return owner
        end

        loaded.preferences = { list = function() return { { id = "profile", name = "Remote", root = "/remote" } } end, watch = watch }
        loaded.connections = { active = function() return true end, status = function() return "connected" end, current = function() return session end, watch = watch }
        environment.workpane.app = { watch = watch }
        environment.workpane.capabilities = { available = function() return false end, watch = watch }
        local root = include("view").build()
        local manager

        for _, node in ipairs(nodes) do
            if node.kind == "table" then manager = node end
        end

        manager.handlers.action({ id = "profile", action = "terminal" })
        assert(root.properties.closable and root.properties.items[1].closable == false)
        assert(#root.properties.items == 2 and #owners == 1)
        root.handlers.close({ id = "connections" })
        assert(#root.properties.items == 2)
        root.handlers.close({ id = "profile:profile" })
        assert(closed == 1 and #root.properties.items == 1 and root.properties.current == "connections")
        root.handlers.close({ id = "profile:profile" })
        manager.handlers.action({ id = "profile", action = "terminal" })
        assert(#owners == 2 and not owners[2].closed)
        root.handlers.close({ id = "profile:profile" })
        local baseline = subscriptions
        manager.handlers.action({ id = "profile", action = "ports" })
        assert(subscriptions == baseline + 3)
        root.handlers.select({ id = "connections" })
        root.handlers.close({ id = "ports:profile" })
        assert(subscriptions == baseline and session.forwards.forward ~= nil)
        manager.handlers.action({ id = "profile", action = "ports" })
        assert(subscriptions == baseline + 3)
        root.handlers.close({ id = "ports:profile" })
        host.test_result({ closed = closed, tabs = #root.properties.items, owners = #owners, subscriptions = subscriptions })
    )lua");

    EXPECT_EQ(result, nlohmann::json({{"closed", 2}, {"tabs", 1}, {"owners", 2}, {"subscriptions", 4}}));
}

TEST_F(RemoteSshModuleTest, PreservesProfilesSavedWhileAnotherProfilesCredentialRemovalIsPending) {
    const auto result = run(R"lua(
        local first = { id = "first", name = "First", host = "host", user = "", port = 0, identity = "", configuration = "", root = "/project", timeoutSeconds = 30, savePassword = true }
        local second = { id = "second", name = "Second", host = "other", user = "", port = 0, identity = "", configuration = "", root = "/other", timeoutSeconds = 30, savePassword = false }
        local saved = { first }
        local entered, enter = async.deferred()
        local waiting, finish = async.deferred()
        environment.workpane.preferences = { define = function()
            return { get = function() return json.decode(json.encode(saved)) end, set = function(_, key, value)
                assert(key == "profiles")

                return async.promise(function() saved = value end)
            end }
        end }
        loaded.credentials = { remove = function(profile, commit)
            assert(profile.id == "first" and profile.savePassword)
            enter()
            waiting:await()

            return commit()
        end }
        async.run(function()
            local preferences = include("preferences")
            preferences.define()
            local removing = async.promise(function() return preferences.remove("first") end)
            checked(entered)
            assert(preferences.save(second))
            finish()
            assert(checked(removing))
            host.test_result({ profiles = preferences.list() })
        end)
    )lua");

    ASSERT_EQ(result["profiles"].size(), 1U);
    EXPECT_EQ(result["profiles"][0]["id"], "second");
    EXPECT_EQ(result["profiles"][0]["root"], "/other");
}

TEST_F(RemoteSshModuleTest, ValidatesProfilesWithoutPersistingCredentialsOrChangingHostIdentity) {
    const auto result = run(R"lua(
        local profiles = include("profiles")
        local profile = { id = "test", name = "Development", host = "configured-alias", user = "", port = 0, identity = "", configuration = "", root = "/project", timeoutSeconds = 30, savePassword = false }
        local accepted = assert(profiles.validate(profile))
        local arguments = assert(profiles.arguments(profile))
        local codes = {}
        local cases = {
            { "name", " " }, { "host", "-oHostKeyAlias=other" }, { "host", "user@host" }, { "user", " " },
            { "port", 65536 }, { "identity", "relative/key" }, { "configuration", "bad\0path" }, { "root", "relative" },
            { "platform", "other" }, { "timeoutSeconds", 0 }, { "savePassword", "yes" }, { "password", "PRIVATE" },
        }

        for _, item in ipairs(cases) do
            local edited = {}

            for key, value in pairs(profile) do
                edited[key] = value
            end

            edited[item[1]] = item[2]
            local value, failure = profiles.validate(edited)
            assert(value == nil)
            codes[#codes + 1] = failure
        end

        profile.root = "c:"
        local drive = assert(profiles.validate(profile))
        profile.root = "C:relative"
        local _, relative = profiles.validate(profile)
        profile.root = ""
        local home = assert(profiles.validate(profile))
        host.test_result({ drive = drive.root, relative = relative, home = home.root, accepted = accepted, arguments = arguments, codes = codes, duplicate = profiles.validList({ profile, profile }), roots = { profiles.absolute("windows", "C:/project"), profiles.absolute("windows", "//server/share/folder"), profiles.absolute("windows", "C:relative"), profiles.absolute("windows", "/project"), profiles.absolute("linux", "/project"), profiles.absolute("macos", "/Users/test/Área"), profiles.absolute("linux", "/bad\0path") } })
    )lua");

    EXPECT_EQ(result["accepted"]["host"], "configured-alias");
    EXPECT_EQ(result["accepted"]["port"], 0);
    EXPECT_EQ(result["home"], "");
    EXPECT_EQ(result["drive"], "C:/");
    EXPECT_EQ(result["relative"], "ssh_profile_root_invalid");
    EXPECT_FALSE(result["accepted"].contains("password"));
    EXPECT_EQ(result["arguments"], nlohmann::json::array({"-T", "-o", "StrictHostKeyChecking=ask", "-o", "ExitOnForwardFailure=yes", "-o", "ForkAfterAuthentication=no", "-o", "ControlMaster=no", "-o", "ControlPersist=no", "-o", "ControlPath=none", "-o", "ConnectTimeout=30", "-o", "ServerAliveInterval=15", "-o", "ServerAliveCountMax=3"}));
    EXPECT_EQ(result["codes"], nlohmann::json::array({"ssh_profile_name_invalid", "ssh_profile_host_invalid", "ssh_profile_host_invalid", "ssh_profile_user_invalid", "ssh_profile_port_invalid", "ssh_profile_path_invalid", "ssh_profile_path_invalid", "ssh_profile_root_invalid", "ssh_profile_invalid", "ssh_profile_timeout_invalid", "ssh_profile_invalid", "ssh_profile_invalid"}));
    EXPECT_EQ(result["duplicate"], false);
    EXPECT_EQ(result["roots"], nlohmann::json::array({true, true, false, false, true, true, false}));
}

TEST_F(RemoteSshModuleTest, PreservesExplicitAccountsAndIpv6HostsInNativeSshArguments) {
    const auto result = run(R"lua(
        local profiles = include("profiles")
        local profile = { id = "test", name = "Development", host = "2001:db8::1", user = "", port = 22, identity = "", configuration = "", root = "/project", timeoutSeconds = 30, savePassword = false }
        local accounts = {}

        for _, user in ipairs({ "DOMAIN\\reader", "AzureAD\\Reader Name", "reader@example.org", "Reader Name", "café" }) do
            profile.user = user
            local accepted = assert(profiles.validate(profile))
            accounts[#accounts + 1] = { user = accepted.user, command = assert(profiles.arguments(profile)), host = accepted.host }
        end

        local failures = {}

        for _, user in ipairs({ " ", "-other", "bad\0name", "bad\nname", string.rep("x", 129) }) do
            profile.user = user
            local _, refused = profiles.validate(profile)
            failures[#failures + 1] = refused
        end

        host.test_result({ accounts = accounts, failures = failures })
    )lua");
    ASSERT_EQ(result["accounts"].size(), 5U);

    for (const auto& account : result["accounts"]) {
        const auto command = account["command"].get<std::vector<std::string>>();
        const auto position = std::ranges::find(command, "-l");
        ASSERT_NE(position, command.end());
        ASSERT_NE(position + 1, command.end());
        EXPECT_EQ(*(position + 1), account["user"].get<std::string>());
        EXPECT_EQ(account["host"], "2001:db8::1");
    }

    EXPECT_EQ(result["failures"], nlohmann::json::array({"ssh_profile_user_invalid", "ssh_profile_user_invalid", "ssh_profile_user_invalid", "ssh_profile_user_invalid", "ssh_profile_user_invalid"}));
}

TEST_F(RemoteSshModuleTest, RefusesCyclicDeepAndNonJsonRequestsBeforeEncoding) {
    const auto result = run(R"lua(
        local protocol = include("protocol")
        local cycle = {}
        cycle.loop = cycle
        local deepest = {}
        local nested = deepest

        for index = 1, 32 do
            nested.child = {}
            nested = nested.child
        end

        local codes = {}

        for _, value in ipairs({ cycle, deepest, { value = math.huge }, { value = "\255" }, { value = function() end } }) do
            local body, failure = protocol.request("files.read", value)
            assert(body == nil)
            codes[#codes + 1] = failure.code
        end

        local source = json.encode({ text = 'Literal "{[\\]}"', repeated = { "test", "test" } })
        local response, failure = protocol.decode(source)
        host.test_result({ codes = codes, response = response, valid = failure == nil })
    )lua");

    EXPECT_EQ(result["codes"], nlohmann::json::array({"ssh_request_invalid", "ssh_request_invalid", "ssh_request_invalid", "ssh_request_invalid", "ssh_request_invalid"}));
    EXPECT_EQ(result["valid"], true);
    EXPECT_EQ(result["response"]["text"], "Literal \"{[\\]}\"");
}

} // namespace workpane::tests
