#include "Result.h"
#include "localization/Localization.h"
#include "scripting/HostReply.h"
#include "scripting/PluginRegistry.h"
#include "scripting/ReplyChannel.h"
#include "scripting/ScriptRuntime.h"
#include "support/LocalPort.h"
#include "support/SampleManifest.h"
#include "support/ScriptHarness.h"
#include "support/TemporaryDirectory.h"

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace workpane::scripting {

using nlohmann::json;

TEST(ScriptRuntime, CallsHostFunctionsAndDeliversEventsToLua) {
    tests::ScriptHarness harness;
    int quiet = 0;
    // clang-format off
    ASSERT_TRUE(harness.runtime().registerFunction("test_quiet", ScriptRuntime::Effect::Background, [&quiet](const json&) { ++quiet; return HostReply::success(); }).hasValue());
    // clang-format on
    const auto before = harness.runtime().changes();
    ASSERT_TRUE(harness
                    .run(R"(
        host.test_quiet({})
        host.test_result({ step = "loaded", sum = 1 + 2 })
        host.on("test.ping", function(payload)
            host.test_result({ step = "event", value = payload.value })
        end)
    )")
                    .hasValue());

    harness.runtime().emit("test.ping", {{"value", "pong"}});
    // clang-format off
    ASSERT_TRUE(harness.pollUntil([&harness]() { return harness.results.size() == 2; }));
    // clang-format on
    EXPECT_EQ(harness.results[0]["sum"], 3);
    EXPECT_EQ(harness.results[1]["value"], "pong");
    EXPECT_EQ(quiet, 1);
    EXPECT_EQ(harness.runtime().changes(), before + 2);
    EXPECT_FALSE(harness.run("this is not lua").hasValue());
}

TEST(LuaSdk, RequestsSettleWithTheirValueOrTheirStructuredFailure) {
    tests::ScriptHarness harness;
    std::vector<std::int64_t> requests;
    // clang-format off
    harness.function("test_async", [&requests](const json& argument) { requests.push_back(argument["request"].get<std::int64_t>()); return HostReply::success(); });
    harness.function("test_refused", [](const json&) { return HostReply::failure({"test_refused", "Refused at once", "detail"}); });
    // clang-format on
    ASSERT_TRUE(harness
                    .run(R"(
        local bridge = require("workpane.bridge")
        local task = require("workpane.task")
        task.run("test", "test", function()
            local value, failure = bridge.request("test_async", {}):await()
            host.test_result({ step = "value", value = value.doubled, failed = failure ~= nil })
            local _, refused = bridge.request("test_async", {}):await()
            host.test_result({ step = "failure", code = refused.code, message = refused.message, text = tostring(refused) })
            local _, immediate = bridge.request("test_refused", {}):await()
            host.test_result({ step = "immediate", code = immediate.code, detail = immediate.detail })
            local ok, raised = pcall(task.await, bridge.request("test_refused", {}))
            host.test_result({ step = "raised", ok = ok, code = raised.code })
        end)
    )")
                    .hasValue());

    // clang-format off
    ASSERT_TRUE(harness.pollUntil([&requests]() { return requests.size() == 1; }));
    harness.replies().reply(requests[0], Result<json>::success({{"doubled", 6}}));
    ASSERT_TRUE(harness.pollUntil([&requests]() { return requests.size() == 2; }));
    harness.replies().reply(requests[1], Result<json>::failure({"test_failed", "The work failed", "why"}));
    ASSERT_TRUE(harness.pollUntil([&harness]() { return harness.results.size() == 4; }));
    // clang-format on

    EXPECT_EQ(harness.results[0]["value"], 6);
    EXPECT_EQ(harness.results[0]["failed"], false);
    EXPECT_EQ(harness.results[1]["code"], "test_failed");
    EXPECT_EQ(harness.results[1]["text"], "The work failed (code \"test_failed\", detail \"why\")");
    EXPECT_EQ(harness.results[2]["code"], "test_refused");
    EXPECT_EQ(harness.results[2]["detail"], "detail");
    EXPECT_EQ(harness.results[3]["ok"], false);
    EXPECT_EQ(harness.results[3]["code"], "test_refused");
    EXPECT_TRUE(harness.logged.empty());
}

// A closed runtime answers every host call with a structured error and never reaches the host again, whether or not its bootstrap ran.
TEST(ScriptRuntime, AnswersEveryCallAfterItClosesWithoutReachingTheHost) {
    tests::ScriptHarness harness;
    harness.runtime().close();

    EXPECT_TRUE(harness
                    .run(R"(
        local reply = host.test_result({ step = "late" })
        assert(reply.ok == false and reply.error.code == "bridge_closed", "the host answered after the bridge closed")
    )")
                    .hasValue());
    EXPECT_TRUE(harness.results.empty());
}

// A task awaiting what a worker of the runtime settles, such as a read of the disk, wakes the host instead of keeping it polling.
TEST(LuaSdk, WakesTheHostWhenAWorkerSettlesWhatATaskAwaits) {
    std::atomic<int> wakes{0};
    tests::ScriptHarness harness;
    // clang-format off
    harness.runtime().setWakeHandler([&wakes]() { ++wakes; });
    // clang-format on
    harness.runtime().poll();
    ASSERT_TRUE(harness
                    .run(R"(
        local fs = require("fs")
        local task = require("workpane.task")
        task.run("test", "test", function()
            local info = fs.stat("."):await()
            host.test_result({ step = "resumed", statted = info ~= nil })
        end)
    )")
                    .hasValue());

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    while (wakes.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_GT(wakes.load(), 0);
    // clang-format off
    ASSERT_TRUE(harness.pollUntil([&harness]() { return harness.results.size() == 1; }));
    // clang-format on
    EXPECT_EQ(harness.results[0]["statted"], true);
    EXPECT_TRUE(std::isinf(harness.runtime().idleSeconds()));
    EXPECT_TRUE(harness.logged.empty());
}

// A task that sleeps tells the host how long it may wait, so the loop sleeps until that timer instead of ticking.
TEST(LuaSdk, WaitsForTheNextTimerOfATask) {
    tests::ScriptHarness harness;
    ASSERT_TRUE(harness
                    .run(R"(
        local async = require("async")
        local task = require("workpane.task")
        task.run("test", "test", function()
            async.sleep(200):await()
            host.test_result({ step = "woke" })
        end)
    )")
                    .hasValue());

    harness.runtime().poll();
    const double idle = harness.runtime().idleSeconds();
    EXPECT_GT(idle, 0.1);
    EXPECT_LE(idle, 0.2);
    // clang-format off
    ASSERT_TRUE(harness.pollUntil([&harness]() { return harness.results.size() == 1; }));
    // clang-format on
    EXPECT_TRUE(std::isinf(harness.runtime().idleSeconds()));
    EXPECT_TRUE(harness.logged.empty());
}

// A stream that finished before Lua awaited it still answers after the head and every chunk it delivered, so the reader of a stream never sees its end first.
TEST(LuaSdk, AStreamAnswersAfterItsHeadAndItsChunks) {
    httplib::Server server;
    // clang-format off
    server.Get("/stream", [](const httplib::Request&, httplib::Response& response) { response.set_content("first\nsecond\n", "text/plain"); });
    // clang-format on
    const int port = tests::LocalPort::bind(server);
    // clang-format off
    std::thread serving([&server]() { std::ignore = server.listen_after_bind(); });
    // clang-format on
    server.wait_until_ready();
    tests::ScriptHarness harness;
    const std::string source = R"(
        local http = require("http")
        local task = require("workpane.task")
        local seen = {}
        task.run("test", "test", function()
            local stream = http.client.streamRaw({ url = "http://127.0.0.1:PORT/stream", method = "GET", headers = {} }, function() seen[#seen + 1] = "chunk" end, function(status) seen[#seen + 1] = "head " .. status end)
            local started = os.clock()
            while os.clock() - started < 0.5 do end
            local _, failure = stream:await()
            seen[#seen + 1] = "end " .. tostring(failure)
            host.test_result({ seen = table.concat(seen, ",") })
        end)
    )";
    ASSERT_TRUE(harness.run(std::regex_replace(source, std::regex("PORT"), std::to_string(port))).hasValue());
    // clang-format off
    const bool answered = harness.pollUntil([&harness]() { return harness.results.size() == 1; });
    // clang-format on
    server.stop();
    serving.join();

    ASSERT_TRUE(answered);
    const std::string seen = harness.results[0]["seen"];
    EXPECT_TRUE(seen.starts_with("head 200,chunk")) << seen;
    EXPECT_TRUE(seen.ends_with(",end nil")) << seen;
}

TEST(LuaSdk, TheBusDeliversEventsAndAnswersCapabilitiesWithStructuredFailures) {
    tests::ScriptHarness harness;
    ASSERT_TRUE(harness
                    .run(R"(
        local events = require("workpane.events")
        local lifecycle = require("workpane.lifecycle")
        local task = require("workpane.task")
        for _, owner in ipairs({ "listener", "provider", "caller", "intruder" }) do
            lifecycle.start(owner)
        end
        events.subscribe("listener", "provider.data.changed", function(payload, sender)
            host.test_result({ step = "event", sender = sender, value = payload.value })
            payload.value = 99
        end)
        events.provide("provider", "provider.data.read", function(payload)
            if payload.fail then
                error({ code = "provider_refused", message = "Refused by the provider", detail = "" })
            end
            if payload.leak then
                return { callback = function() end }
            end
            return { echoed = payload.value }
        end)
        task.run("caller", "test", function()
            local sent = { value = 1 }
            events.publish("provider", "provider.data.changed", sent)
            local _, carried = pcall(events.publish, "provider", "provider.data.changed", { callback = function() end })
            local _, leaked = events.request("caller", "provider.data.read", { leak = true }):await()
            host.test_result({ step = "copied", kept = sent.value, carried = carried.code, leaked = leaked.code })
            local answer = task.await(events.request("caller", "provider.data.read", { value = 5 }))
            host.test_result({ step = "answer", value = answer.echoed })
            local _, refused = events.request("caller", "provider.data.read", { fail = true }):await()
            host.test_result({ step = "refused", code = refused.code })
            local _, missing = events.request("caller", "nobody.data.read", {}):await()
            host.test_result({ step = "missing", code = missing.code })
            local provided = pcall(events.provide, "intruder", "provider.data.read", function() end)
            host.test_result({ step = "duplicate", provided = provided })
            local _, stranger = pcall(events.provide, "stranger", "stranger.data.read", function() end)
            host.test_result({ step = "stranger", code = stranger.code })
            local before = events.available("provider.data.read") and not events.available("nobody.data.read")
            events.forget("provider")
            host.test_result({ step = "available", before = before, after = events.available("provider.data.read") })
        end)
    )")
                    .hasValue());

    // clang-format off
    ASSERT_TRUE(harness.pollUntil([&harness]() { return harness.results.size() == 8; }));
    // clang-format on

    std::map<std::string, json> steps;

    for (const auto& result : harness.results) {
        steps[result["step"].get<std::string>()] = result;
    }

    EXPECT_EQ(steps["event"]["sender"], "provider");
    EXPECT_EQ(steps["answer"]["value"], 5);
    EXPECT_EQ(steps["refused"]["code"], "provider_refused");
    EXPECT_EQ(steps["missing"]["code"], "capability_unavailable");
    EXPECT_EQ(steps["stranger"]["code"], "plugin_not_running");
    EXPECT_EQ(steps["copied"]["kept"], 1);
    EXPECT_EQ(steps["copied"]["carried"], "event_payload_invalid");
    EXPECT_EQ(steps["copied"]["leaked"], "capability_value_invalid");
    EXPECT_EQ(steps["duplicate"]["provided"], false);
    EXPECT_EQ(steps["available"]["before"], true);
    EXPECT_EQ(steps["available"]["after"], false);
}

// A command sent before its node is mounted waits for the mount, and the waiting commands reach their components in the order they were sent.
TEST(LuaSdk, DeliversCommandsSentBeforeAMountAfterIt) {
    tests::ScriptHarness harness;
    std::vector<std::string> calls;
    // clang-format off
    harness.function("workpane_ui_mount", [&calls](const json&) { calls.emplace_back("mount"); return HostReply::success(); });
    harness.function("workpane_ui_children", [&calls](const json& argument) { calls.push_back("children:" + std::to_string(argument["children"].size())); return HostReply::success(); });
    harness.function("workpane_ui_command", [&calls](const json& argument) { calls.push_back("command:" + std::to_string(argument["node"].get<int>()) + ":" + argument["command"].get<std::string>()); return HostReply::success(); });
    // clang-format on
    ASSERT_TRUE(harness
                    .run(R"(
        local ui = require("workpane.ui")
        local field = ui.textField({})
        local editor = ui.codeEditor({})
        local unused = ui.label({ text = "" })
        editor:command("reveal", { line = 2, column = 1 })
        field:command("focus")
        unused:command("focus")
        local root = ui.column({}, { field })
        ui.mount("sample", "view:sample:main", root)
        root:setChildren({ field, editor })
        field:command("focus")
    )")
                    .hasValue());

    EXPECT_EQ(calls, (std::vector<std::string>{"mount", "command:1:focus", "children:2", "command:2:reveal", "command:1:focus"}));
}

// The SDK refuses a mistaken call with a structured error when it is made, and a command queued before a mount that the host refuses is written to the log instead of refusing the mount.
TEST(LuaSdk, RefusesMistakenCallsWithStructuredErrors) {
    tests::ScriptHarness harness;
    std::vector<std::string> calls;
    // clang-format off
    harness.function("workpane_ui_mount", [&calls](const json&) { calls.emplace_back("mount"); return HostReply::success(); });
    harness.function("workpane_ui_command", [&calls](const json&) { calls.emplace_back("command"); return HostReply::failure({"ui_command_unknown", "The component has no such command", ""}); });
    // clang-format on
    ASSERT_TRUE(harness
                    .run(R"(
        local bridge = require("workpane.bridge")
        local events = require("workpane.events")
        local http = require("workpane.http")
        local lifecycle = require("workpane.lifecycle")
        local logs = require("workpane.logs")
        local process = require("workpane.process")
        local preferences = require("workpane.preferences")
        local ui = require("workpane.ui")
        lifecycle.start("sample")
        local node = ui.label({ text = "" })
        local future = bridge.future(function() return 1 end)
        local attempts = {
            function() events.subscribe("sample", "sample.data.changed", 42) end,
            function() events.provide("sample", "sample.data.read", "answer") end,
            function() logs.subscribe("sample", {}) end,
            function() http.serve("sample", nil) end,
            function() process.start("sample", { program = "/bin/sh", onExit = 1 }) end,
            function() node:set(nil) end,
            function() node:on("click", "handler") end,
            function() node.set({}, {}) end,
            function() preferences.define("sample", nil) end,
            function() future.await({}) end,
        }

        local codes = {}
        for index, attempt in ipairs(attempts) do
            local _, failure = pcall(attempt)
            codes[index] = failure ~= nil and failure.code or "accepted"
        end
        local queued = ui.textField({})
        queued:command("focus")
        ui.mount("sample", "view:sample:main", ui.column({}, { queued }))
        host.test_result({ codes = table.concat(codes, ",") })
    )")
                    .hasValue());

    ASSERT_EQ(harness.results.size(), 1U);
    EXPECT_EQ(harness.results[0]["codes"], "event_handler_invalid,capability_handler_invalid,log_handler_invalid,http_options_invalid,process_options_invalid,ui_properties_invalid,ui_handler_invalid,ui_node_invalid,preferences_schema_invalid,bridge_future_invalid");
    EXPECT_EQ(calls, (std::vector<std::string>{"mount", "command"}));
    ASSERT_EQ(harness.logged.size(), 1U);
    EXPECT_EQ(harness.logged[0]["message"], "A command sent before its node was mounted failed");
}

// A path is absolute by the rule of its platform, and a file address writes and reads back a path of every platform, where a drive gains its slash and a network share names its server.
TEST(LuaSdk, WritesAndReadsThePathsOfEveryPlatform) {
    tests::ScriptHarness harness;
    ASSERT_TRUE(harness
                    .run(R"(
        local paths = require("workpane.paths")
        local api = require("workpane.api")
        local function info(platform)
            return { version = "", debug = false, platform = platform, architecture = "", paths = { data = "" }, languages = {}, themes = {}, icons = {}, colors = {} }
        end
        local linux = api.create("sample", "", info("linux"))
        local windows = api.create("sample", "", info("windows"))
        local path = "/tmp/a b/c#+\xC3\xA9.lua"
        local _, refused = pcall(linux.files.uri, "notes/a.lua")
        local _, refusedDrive = pcall(linux.files.uri, "C:/notes/a.lua")
        host.test_result({
            posix = { paths.absolute("macos", "/srv"), paths.absolute("linux", "C:/srv"), paths.absolute("linux", "srv"), paths.absolute("linux", 7) },
            windows = { paths.absolute("windows", "C:/srv"), paths.absolute("windows", "C:\\srv"), paths.absolute("windows", "//server/share"), paths.absolute("windows", "\\\\server\\share"), paths.absolute("windows", "/srv"), paths.absolute("windows", "C:srv"), paths.absolute("windows", "//") },
            uri = paths.uri(path),
            back = paths.path("linux", paths.uri(path)) == path,
            drive = paths.uri("C:\\work\\a.lua"),
            drivePath = paths.path("windows", "file:///C:/work/a.lua"),
            share = paths.uri("\\\\server\\share\\a.lua"),
            sharePath = paths.path("windows", "file://server/share/a.lua"),
            hostPath = paths.path("linux", "file://machine/home/a.lua"),
            other = paths.path("linux", "https://example.com/a.lua") == nil,
            sdk = { linux.files.absolute("/srv"), windows.files.absolute("/srv"), windows.files.uri("C:/work/a.lua"), windows.files.path("file://server/share/a.lua") },
            refused = { refused.code, refusedDrive.code },
        })
    )")
                    .hasValue());

    ASSERT_EQ(harness.results.size(), 1U);
    const json& results = harness.results[0];
    EXPECT_EQ(results["posix"], json::array({true, false, false, false}));
    EXPECT_EQ(results["windows"], json::array({true, true, true, true, false, false, false}));
    EXPECT_EQ(results["uri"], "file:///tmp/a%20b/c%23%2B%C3%A9.lua");
    EXPECT_EQ(results["back"], true);
    EXPECT_EQ(results["drive"], "file:///C:/work/a.lua");
    EXPECT_EQ(results["drivePath"], "C:/work/a.lua");
    EXPECT_EQ(results["share"], "file://server/share/a.lua");
    EXPECT_EQ(results["sharePath"], "//server/share/a.lua");
    EXPECT_EQ(results["hostPath"], "/home/a.lua");
    EXPECT_EQ(results["other"], true);
    EXPECT_EQ(results["sdk"], json::array({true, false, "file:///C:/work/a.lua", "//server/share/a.lua"}));
    EXPECT_EQ(results["refused"], json::array({"files_path_invalid", "files_path_invalid"}));
}

// New rows or items that no longer carry the selected entry clear the selection in the same patch, whether the plugin or the reader chose it.
TEST(LuaSdk, ClearsASelectionWhoseEntryNewRowsNoLongerCarry) {
    tests::ScriptHarness harness;
    std::vector<json> patches;
    // clang-format off
    harness.function("workpane_ui_mount", [](const json&) { return HostReply::success(); });
    harness.function("workpane_ui_patch", [&patches](const json& argument) { patches.push_back(argument["props"]); return HostReply::success(); });
    // clang-format on
    ASSERT_TRUE(harness
                    .run(R"(
        local ui = require("workpane.ui")
        local columns = { { id = "name", title = "Name", width = "stretch" } }
        local row = function(id) return { id = id, cells = { id } } end
        rows = ui.table({ columns = columns, rows = { row("a"), row("b") }, selected = "b" })
        items = ui.list({ items = { { id = "x", text = "X" }, { id = "y", text = "Y" } } })
        ui.mount("sample", "view:sample:main", ui.column({}, { rows, items }))
        rows:set({ rows = { row("a"), row("b"), row("c") } })
        rows:set({ rows = { row("a") } })
    )")
                    .hasValue());

    harness.runtime().emit("workpane.ui.events", {{"events", json::array({{{"surface", "view:sample:main"}, {"node", 2}, {"name", "select"}, {"value", {{"id", "y"}}}, {"state", {{"selected", "id"}}}}})}});
    harness.runtime().poll();
    ASSERT_TRUE(harness
                    .run(R"(
        items:set({ items = { { id = "x", text = "X" } } })
        assert(rows:get("selected") == "" and items:get("selected") == "")
    )")
                    .hasValue());

    ASSERT_EQ(patches.size(), 3U);
    EXPECT_FALSE(patches[0].contains("selected"));
    EXPECT_EQ(patches[1]["selected"], "");
    EXPECT_EQ(patches[2]["selected"], "");
}

// A node takes the properties an event names as fields of its value and the new order of the children it moved, whatever its kind, and a tab strip changes its tabs and pages in one call.
TEST(LuaSdk, TakesTheStateAnEventNames) {
    tests::ScriptHarness harness;
    std::vector<json> mounts;
    std::vector<json> replaced;
    // clang-format off
    harness.function("workpane_ui_mount", [&mounts](const json& argument) { mounts.push_back(argument["tree"]); return HostReply::success(); });
    harness.function("workpane_ui_children", [&replaced](const json& argument) { replaced.push_back(argument); return HostReply::success(); });
    // clang-format on
    ASSERT_TRUE(harness
                    .run(R"(
        local ui = require("workpane.ui")
        pages = { ui.label({ text = "A" }), ui.label({ text = "B" }) }
        tabs = ui.tabs({ items = { { id = "a", text = "A" }, { id = "b", text = "B" } }, current = "a", movable = true }, pages)
        ui.mount("sample", "view:sample:main", tabs)
    )")
                    .hasValue());

    const json moved = json::array({{{"id", "b"}, {"text", "B"}}, {{"id", "a"}, {"text", "A"}}});
    const json select = {{"surface", "view:sample:main"}, {"node", 3}, {"name", "select"}, {"value", {{"id", "b"}}}, {"state", {{"current", "id"}}}};
    const json move = {{"surface", "view:sample:main"}, {"node", 3}, {"name", "move"}, {"value", {{"id", "a"}, {"index", 1}, {"items", moved}}}, {"state", {{"items", "items"}}}, {"order", {2, 1}}};
    harness.runtime().emit("workpane.ui.events", {{"events", json::array({select, move})}});
    harness.runtime().poll();
    ASSERT_TRUE(harness
                    .run(R"(
        assert(tabs:get("current") == "b" and tabs:get("items")[1].id == "b")
        ui = require("workpane.ui")
        ui.mount("sample", "view:sample:main", tabs)
        tabs:setChildren({ pages[1] }, { items = { { id = "a", text = "A" } }, current = "a" })
        assert(tabs:get("current") == "a" and #tabs:get("items") == 1)
    )")
                    .hasValue());

    ASSERT_EQ(mounts.size(), 2U);
    EXPECT_EQ(mounts[1]["children"][0]["id"], 2);
    EXPECT_EQ(mounts[1]["children"][1]["id"], 1);
    ASSERT_EQ(replaced.size(), 1U);
    EXPECT_EQ(replaced[0]["props"]["current"], "a");
    EXPECT_EQ(replaced[0]["children"], json::array({{{"id", 1}, {"kept", true}}}));
}

// A stored preference its rule refuses reads as its default and is written to the log once, a record takes the defaults of the fields it lacks, and every refused declaration, value and key names its code.
TEST(LuaSdk, ReadsPreferencesThroughTheirRules) {
    tests::ScriptHarness harness;
    // clang-format off
    harness.function("workpane_preferences_read", [](const json& argument) { return HostReply::success(argument.contains("document") ? json::object() : json{{"count", 5}, {"ratio", 2.5}, {"tags", {"a", "a"}}, {"person", {{"name", "Ada"}}}, {"gone", 1}, {"flag", false}}); });
    // clang-format on
    ASSERT_TRUE(harness
                    .run(R"(
        local lifecycle = require("workpane.lifecycle")
        local preferences = require("workpane.preferences")
        lifecycle.start("sample")
        local schema = {
            count = { type = "integer", default = 1, minimum = 0, maximum = 10 },
            ratio = { type = "integer", default = 3 },
            tags = { type = "list", items = { type = "string" }, unique = true },
            person = { type = "record", fields = { name = { type = "string", default = "" }, age = { type = "integer", default = 7 } } },
            mode = { type = "string", default = "a", choices = { "a", "b" } },
            flag = { type = "boolean", default = true },
        }

        local store = preferences.define("sample", schema)
        local values = store:values()
        local function code(work)
            local _, failure = pcall(work)
            return failure ~= nil and (failure.code .. ":" .. failure.detail) or "accepted"
        end
        host.test_result({
            count = values.count, ratio = values.ratio, tags = #values.tags, name = values.person.name, age = values.person.age, flag = values.flag,
            unknownField = code(function() preferences.define("sample", { x = { type = "integer", default = 1, choices = { 1 } } }, { document = "other" }) end),
            missingDefault = code(function() preferences.define("sample", { x = { type = "integer" } }, { document = "third" }) end),
            badDefault = code(function() preferences.define("sample", { x = { type = "integer", default = 11, maximum = 10 } }, { document = "fourth" }) end),
            twice = code(function() preferences.define("sample", {}) end),
            document = code(function() preferences.define("sample", {}, { document = "Bad Name" }) end),
            nested = code(function() store:set("person", { name = "Ada", age = "old" }) end),
            unknownRecordField = code(function() store:set("person", { nickname = "A" }) end),
            choice = code(function() store:set("mode", "c") end),
            undeclared = code(function() store:get("missing") end),
            atomic = code(function() store:update({ count = 2, mode = "c" }) end),
            countAfter = store:get("count"),
            control = code(function() store:control("person") end),
            labels = code(function() store:control("mode", {}) end),
            as = code(function() store:control("flag", { as = "slider" }) end),
            methods = store.__index == nil and type(store.get) == "function",
        })
    )")
                    .hasValue());

    ASSERT_EQ(harness.results.size(), 1U);
    const json& seen = harness.results[0];
    EXPECT_EQ(seen["count"], 5);
    EXPECT_EQ(seen["ratio"], 3);
    EXPECT_EQ(seen["tags"], 0);
    EXPECT_EQ(seen["name"], "Ada");
    EXPECT_EQ(seen["age"], 7);
    EXPECT_EQ(seen["flag"], false);
    EXPECT_EQ(seen["unknownField"], "preferences_rule_invalid:x.choices");
    EXPECT_EQ(seen["missingDefault"], "preferences_rule_invalid:x");
    EXPECT_EQ(seen["badDefault"], "preferences_rule_invalid:x");
    EXPECT_EQ(seen["twice"], "preferences_defined:sample:");
    EXPECT_EQ(seen["document"], "preferences_document_invalid:Bad Name");
    EXPECT_EQ(seen["nested"], "preferences_value_invalid:person.age");
    EXPECT_EQ(seen["unknownRecordField"], "preferences_value_invalid:person.nickname");
    EXPECT_EQ(seen["choice"], "preferences_value_invalid:mode");
    EXPECT_EQ(seen["undeclared"], "preferences_key_undeclared:missing");
    EXPECT_EQ(seen["atomic"], "preferences_value_invalid:mode");
    EXPECT_EQ(seen["countAfter"], 5);
    EXPECT_EQ(seen["control"], "preferences_control_invalid:person");
    EXPECT_EQ(seen["labels"], "preferences_control_invalid:mode");
    EXPECT_EQ(seen["as"], "preferences_control_invalid:flag.slider");
    EXPECT_EQ(seen["methods"], true);
    ASSERT_EQ(harness.logged.size(), 2U) << json(harness.logged).dump();

    for (const auto& entry : harness.logged) {
        EXPECT_EQ(entry["level"], "warning");
        EXPECT_EQ(entry["category"], "preferences");
    }
}

// A change applies at once and reaches every watcher, and the failure of the latest write puts the whole committed document back and tells the watchers, as the host does.
// A watcher that stops itself while it is told of a change leaves every other watcher of that change its turn, for preferences, capabilities and the choices of the reader alike, and a bound control nobody holds is let go.
TEST(LuaSdk, AWatcherThatStopsItselfLeavesTheOthersTheirTurn) {
    tests::ScriptHarness harness;
    // clang-format off
    harness.function("workpane_preferences_read", [](const json&) { return HostReply::success(json::object()); });
    harness.function("workpane_preferences_write", [](const json&) { return HostReply::success(); });
    harness.function("workpane_app_info", [](const json&) { return HostReply::success({{"language", "en"}, {"theme", "green"}}); });
    // clang-format on
    ASSERT_TRUE(harness
                    .run(R"(
        local events = require("workpane.events")
        local lifecycle = require("workpane.lifecycle")
        local preferences = require("workpane.preferences")
        local reader = require("workpane.reader")
        lifecycle.start("sample")
        lifecycle.start("other")
        local heard = { preferences = 0, capabilities = 0, reader = 0 }
        local function watchTwice(watch, family)
            local stop
            stop = watch(function() stop() end)
            watch(function() heard[family] = heard[family] + 1 end)
        end

        local store = preferences.define("sample", { count = { type = "integer", default = 0 } })
        watchTwice(function(handler) return store:watch("count", handler) end, "preferences")
        store:set("count", 1)
        store:set("count", 2)

        watchTwice(function(handler) return events.watch("sample", "other.data.read", handler) end, "capabilities")
        events.provide("other", "other.data.read", function() return {} end)
        events.forget("other")

        watchTwice(function(handler) return reader.watch("sample", handler) end, "reader")

        -- A control nobody holds any more is collected instead of being kept by the watcher that binds it.
        local probe = setmetatable({}, { __mode = "v" })
        probe.control = store:control("count")
        collectgarbage()
        collectgarbage()
        store:set("count", 3)
        heard.released = probe.control == nil
        local result = host.test_result
        _G.finish = function() result(heard) end
    )")
                    .hasValue());
    harness.runtime().emit("workpane.language.changed", json::object());
    harness.runtime().emit("workpane.theme.changed", json::object());
    // clang-format off
    ASSERT_TRUE(harness.pollUntil([&harness]() { return harness.run("finish()").hasValue() && harness.results.back()["reader"] == 2; })) << json(harness.results).dump();
    // clang-format on
    harness.results.erase(harness.results.begin(), harness.results.end() - 1);

    ASSERT_EQ(harness.results.size(), 1U);
    EXPECT_EQ(harness.results[0]["preferences"], 3);
    EXPECT_EQ(harness.results[0]["capabilities"], 2);
    EXPECT_EQ(harness.results[0]["reader"], 2);
    EXPECT_TRUE(harness.results[0]["released"]);
}

TEST(LuaSdk, PutsTheCommittedPreferencesBackWhenTheLatestWriteFails) {
    tests::ScriptHarness harness;
    std::vector<std::int64_t> writes;
    std::vector<json> documents;
    // clang-format off
    harness.function("workpane_preferences_read", [](const json&) { return HostReply::success(json::object()); });
    harness.function("workpane_preferences_write", [&writes, &documents](const json& argument) { writes.push_back(argument["request"].get<std::int64_t>()); documents.push_back(argument["values"]); return HostReply::success(); });
    // clang-format on
    ASSERT_TRUE(harness
                    .run(R"(
        local lifecycle = require("workpane.lifecycle")
        local preferences = require("workpane.preferences")
        local task = require("workpane.task")
        lifecycle.start("sample")
        local store = preferences.define("sample", { count = { type = "integer", default = 1 }, name = { type = "string", default = "" } })
        local seen = {}
        local stop = store:watch("count", function(value) seen[#seen + 1] = "count=" .. value end)
        store:watch(function(value, key) seen[#seen + 1] = key end)
        task.run("sample", "test", function()
            local first = store:set("count", 7)
            local second = store:set("count", 8)
            local unchanged = store:set("count", 8)
            local _, firstFailure = first:await()
            local _, secondFailure = second:await()
            unchanged:await()
            local afterRollback = store:get("count")
            stop()
            store:set("name", "kept")
            store:reset()
            host.test_result({ firstFailed = firstFailure ~= nil, secondCode = secondFailure ~= nil and secondFailure.code or "", count = afterRollback, seen = table.concat(seen, ","), reset = store:get("count") .. "|" .. store:get("name") })
        end)
    )")
                    .hasValue());
    // clang-format off
    ASSERT_TRUE(harness.pollUntil([&writes]() { return writes.size() == 2; }));
    // clang-format on
    harness.replies().reply(writes[0], Result<json>::success(nullptr));
    harness.replies().reply(writes[1], Result<json>::failure({"database_busy", "The database is busy", ""}));
    // clang-format off
    ASSERT_TRUE(harness.pollUntil([&harness]() { return harness.results.size() == 1; })) << json(harness.logged).dump();
    // clang-format on

    const json& seen = harness.results[0];
    EXPECT_FALSE(seen["firstFailed"]);
    EXPECT_EQ(seen["secondCode"], "database_busy");
    EXPECT_EQ(seen["count"], 7);
    EXPECT_EQ(seen["seen"], "count=7,count,count=8,count,count=7,count,name,count,name");
    EXPECT_EQ(seen["reset"], "1|");
    EXPECT_EQ(documents[0], json({{"count", 7}, {"name", ""}}));
    EXPECT_EQ(documents[1], json({{"count", 8}, {"name", ""}}));
    ASSERT_EQ(harness.logged.size(), 1U);
    EXPECT_EQ(harness.logged[0]["message"], "A preference document could not be written");
}

// Plugins start in dependency order, a missing dependency or a cycle refuses a plugin, a plugin waits for a dependency that does not run, a plugin off by default starts only once the reader turned it on, and the reader switches plugins while the product runs, dependents first.
TEST(LuaSdk, StartsPluginsInDependencyOrderAndSwitchesThemWhileRunning) {
    tests::TemporaryDirectory folder;
    const std::vector<std::tuple<std::string, std::string, std::string>> plugins{{"a", "", ""}, {"b", "\"a\"", ""}, {"c", "\"missing\"", ""}, {"d", "\"e\"", ""}, {"e", "\"d\"", ""}, {"f", "\"c\"", ""}, {"g", "", ""}, {"h", "\"g\"", ""}, {"i", "", "offByDefault = true, "}, {"j", "", "offByDefault = true, "}};

    for (const auto& [id, dependencies, extra] : plugins) {
        std::filesystem::create_directories(folder.path() / id);
        std::ofstream(folder.path() / id / "translations.lua") << "return { en = {} }\n";
        std::ofstream(folder.path() / id / "plugin.lua") << "return { id = \"" << id << "\", titleKey = \"" << id << ".plugin.title\", " << extra << "dependencies = { " << dependencies << " }, start = function() end, stop = function() end }\n";
    }

    tests::ScriptHarness harness;
    std::vector<std::string> calls;
    // clang-format off
    harness.function("workpane_plugin_catalog", [](const json&) { return HostReply::success(); });
    harness.function("workpane_plugin_register", [&calls](const json& argument) { calls.push_back("+" + argument["id"].get<std::string>()); return HostReply::success(); });
    harness.function("workpane_plugin_remove", [&calls](const json& argument) { calls.push_back("-" + argument["plugin"].get<std::string>()); return HostReply::success(); });
    harness.function("workpane_translate", [](const json& argument) { return HostReply::success(argument["key"]); });
    harness.function("workpane_notify", [](const json&) { return HostReply::success(); });
    harness.function("workpane_audio_forget", [](const json&) { return HostReply::success(); });
    harness.function("workpane_http_forget", [](const json&) { return HostReply::success(); });
    harness.function("workpane_process_forget", [](const json&) { return HostReply::success(); });
    // clang-format on
    const std::string source = R"(
        local plugins = require("workpane.plugins")
        local task = require("workpane.task")
        task.run("test", "test", function()
            plugins.boot({ paths = { plugins = { FOLDER }, data = "", database = "" }, languages = {}, themes = {}, icons = {}, colors = {} }, { g = false, j = true })
            local function states()
                local seen = {}
                for _, plugin in ipairs(plugins.list()) do
                    seen[plugin.id] = plugin.state .. (plugin.failure ~= nil and (":" .. plugin.failure.code) or "")
                end
                return seen
            end
            local booted = states()
            local dependents = table.concat(plugins.dependents("a"), ",")
            task.await(plugins.disable("a"))
            local disabled = states()
            task.await(plugins.enable("a"))
            local enabled = states()
            task.await(plugins.enable("g"))
            host.test_result({ booted = booted, dependents = dependents, disabled = disabled, enabled = enabled, released = states() })
        end)
    )";
    ASSERT_TRUE(harness.run("local FOLDER = " + json(folder.path().generic_string()).dump() + "\n" + source).hasValue());
    // clang-format off
    ASSERT_TRUE(harness.pollUntil([&harness]() { return harness.results.size() == 1; })) << json(harness.logged).dump();
    // clang-format on

    const json& seen = harness.results[0];
    EXPECT_EQ(seen["booted"], json({{"a", "running"}, {"b", "running"}, {"c", "refused:plugin_dependency_missing"}, {"d", "refused:plugin_dependency_cycle"}, {"e", "refused:plugin_dependency_cycle"}, {"f", "waiting:plugin_dependency_inactive"}, {"g", "disabled"}, {"h", "waiting:plugin_dependency_inactive"}, {"i", "disabled"}, {"j", "running"}}));
    EXPECT_EQ(seen["dependents"], "b");
    EXPECT_EQ(seen["disabled"]["a"], "disabled");
    EXPECT_EQ(seen["disabled"]["b"], "waiting:plugin_dependency_inactive");
    EXPECT_EQ(seen["enabled"]["a"], "running");
    EXPECT_EQ(seen["enabled"]["b"], "running");
    EXPECT_EQ(seen["released"]["g"], "running");
    EXPECT_EQ(seen["released"]["h"], "running");
    EXPECT_EQ(calls, (std::vector<std::string>{"+a", "+b", "+j", "-b", "-a", "+a", "+b", "+g", "+h"}));

    // Every change of state is logged with where it came from, and the three refusals are the only errors.
    std::vector<std::string> errors;
    std::map<std::string, std::string> transitions;

    for (const auto& entry : harness.logged) {
        if (entry["level"] == "error") {
            errors.push_back(entry["message"]);
            continue;
        }

        EXPECT_EQ(entry["category"], "plugins");
        const json& details = entry["details"];
        const std::string plugin = details["plugin"];
        transitions[plugin] += (transitions[plugin].empty() ? "" : ",") + details.value("from", std::string()) + ">" + details["to"].get<std::string>();
    }

    EXPECT_EQ(errors, (std::vector<std::string>(3, "A plugin could not be loaded")));
    EXPECT_EQ(transitions["a"], ">discovered,discovered>starting,starting>running,running>stopping,stopping>disabled,disabled>waiting,waiting>starting,starting>running");
    EXPECT_EQ(transitions["b"], ">discovered,discovered>starting,starting>running,running>stopping,stopping>waiting,waiting>starting,starting>running");
    EXPECT_EQ(transitions["c"], ">discovered,discovered>refused");
    EXPECT_EQ(transitions["f"], ">discovered,discovered>waiting");
    EXPECT_EQ(transitions["g"], ">discovered,discovered>disabled,disabled>waiting,waiting>starting,starting>running");
    EXPECT_EQ(transitions["i"], ">discovered,discovered>disabled");
    EXPECT_EQ(transitions["j"], ">discovered,discovered>starting,starting>running");
}

// A plugin reaches only the standard library, a restricted os and the allowed Varn modules, and whatever it changes in the standard library stays its own.
TEST(LuaSdk, KeepsEachPluginInsideItsOwnEnvironment) {
    tests::TemporaryDirectory folder;

    for (const std::string id : {"alpha", "beta"}) {
        std::filesystem::create_directories(folder.path() / id);
        std::ofstream(folder.path() / id / "translations.lua") << "return { en = {} }\n";
    }

    std::ofstream(folder.path() / "alpha" / "plugin.lua") << R"(
        local _, refusal = pcall(require, "io")
        local _, missing = pcall(include, "missing")
        print()
        print("")
        table.insert = nil
        string.upper = nil
        return { id = "alpha", seen = { io = refusal.code, missing = missing.code, execute = type(os.execute), metatable = getmetatable(""), sdk = type(package) } }
    )";
    std::ofstream(folder.path() / "beta" / "plugin.lua") << R"(
        local list = {}
        table.insert(list, "kept")
        return { id = "beta", seen = { list = list[1], upper = ("kept"):upper() } }
    )";
    tests::ScriptHarness harness;
    const std::string source = R"(
        local loader = require("workpane.loader")
        local task = require("workpane.task")
        task.run("test", "test", function()
            local candidates, refused = loader.discover({ FOLDER }, { paths = { data = "" }, languages = {}, themes = {}, icons = {}, colors = {} })
            host.test_result({ alpha = candidates[1].candidate.definition.seen, beta = candidates[2].candidate.definition.seen, refused = #refused })
        end)
    )";
    ASSERT_TRUE(harness.run("local FOLDER = " + json(folder.path().generic_string()).dump() + "\n" + source).hasValue());
    // clang-format off
    ASSERT_TRUE(harness.pollUntil([&harness]() { return harness.results.size() == 1; })) << json(harness.logged).dump();
    // clang-format on

    const json& seen = harness.results[0];
    EXPECT_EQ(seen["refused"], 0);
    EXPECT_EQ(seen["alpha"]["io"], "plugin_module_refused");
    EXPECT_EQ(seen["alpha"]["missing"], "plugin_module_missing");
    EXPECT_EQ(seen["alpha"]["execute"], "nil");
    EXPECT_EQ(seen["alpha"]["metatable"], "string");
    EXPECT_EQ(seen["alpha"]["sdk"], "nil");
    EXPECT_EQ(seen["beta"]["list"], "kept");
    EXPECT_EQ(seen["beta"]["upper"], "KEPT");
}

// A patch or children the host refuses leave the node, its children and its surface as they were on this side too, so the view keeps working with what the host shows.
// A value nested deeper than the runtime reads back is refused before the host keeps it, and a reply the runtime cannot read is a structured error instead of a nil.
TEST(LuaSdk, RefusesValuesNestedDeeperThanTheRuntimeReads) {
    tests::ScriptHarness harness;
    json deep = 1;

    for (int level = 0; level < 210; ++level) {
        deep = json{{"inner", deep}};
    }

    // clang-format off
    harness.function("test_echo", [](const json& argument) { return HostReply::success(argument); });
    harness.function("test_deep", [&deep](const json&) { return HostReply::success(deep); });
    // clang-format on
    ASSERT_TRUE(harness
                    .run(R"(
        local bridge = require("workpane.bridge")
        local nested = {}
        local cursor = nested
        for _ = 1, 70 do
            cursor.inner = {}
            cursor = cursor.inner
        end
        local _, refused = pcall(bridge.call, "test_echo", { value = nested })
        local shallow = bridge.call("test_echo", { value = { a = { b = 1 } } })
        local _, unread = pcall(bridge.call, "test_deep", {})
        host.test_result({ refused = refused.code, shallow = shallow.value.a.b, unread = unread.code })
    )")
                    .hasValue());

    ASSERT_EQ(harness.results.size(), 1U);
    EXPECT_EQ(harness.results[0]["refused"], "json_field_depth");
    EXPECT_EQ(harness.results[0]["shallow"], 1);
    EXPECT_EQ(harness.results[0]["unread"], "bridge_reply_invalid");
}

TEST(LuaSdk, KeepsANodeAsItWasWhenTheHostRefusesAChange) {
    tests::ScriptHarness harness;
    std::vector<std::string> commands;
    // clang-format off
    harness.function("workpane_ui_mount", [](const json&) { return HostReply::success(); });
    harness.function("workpane_ui_patch", [](const json& argument) { return argument["props"].value("value", "") == "refused" ? HostReply::failure({"ui_property_invalid", "Refused", ""}) : HostReply::success(); });
    harness.function("workpane_ui_children", [](const json&) { return HostReply::failure({"ui_surface_too_large", "Refused", ""}); });
    harness.function("workpane_ui_command", [&commands](const json& argument) { commands.push_back(argument["command"].get<std::string>()); return HostReply::success(); });
    // clang-format on
    ASSERT_TRUE(harness
                    .run(R"(
        local lifecycle = require("workpane.lifecycle")
        local ui = require("workpane.ui")
        lifecycle.start("sample")
        local field = ui.textField({ value = "kept" })
        local first = ui.label({ text = "first" })
        local column = ui.column({}, { field, first })
        ui.mount("sample", "view:sample:main", column)

        local patched = pcall(field.set, field, { value = "refused" })
        local kept = field:get("value")
        local replaced = pcall(column.setChildren, column, { ui.label({ text = "second" }) })
        first:command("reveal")
        field:set({ value = "accepted" })

        host.test_result({ patched = patched, kept = kept, replaced = replaced, value = field:get("value") })
    )")
                    .hasValue());

    ASSERT_EQ(harness.results.size(), 1U);
    EXPECT_EQ(harness.results[0]["patched"], false);
    EXPECT_EQ(harness.results[0]["kept"], "kept");
    EXPECT_EQ(harness.results[0]["replaced"], false);
    EXPECT_EQ(harness.results[0]["value"], "accepted");
    EXPECT_EQ(commands, std::vector<std::string>{"reveal"});
}

TEST(LuaSdk, KeepsTheModulesAndObjectsOfEachPluginToItself) {
    tests::TemporaryDirectory folder;

    for (const std::string id : {"alpha", "beta"}) {
        std::filesystem::create_directories(folder.path() / id);
        std::ofstream(folder.path() / id / "translations.lua") << "return { en = {} }\n";
    }

    std::ofstream(folder.path() / "alpha" / "plugin.lua") << R"(
        require("json").encode = nil
        require("async").sleep = nil
        workpane.app.languages[1] = "forged"
        local node = workpane.ui.column({}, {})
        local owned = setmetatable({}, { kind = "owned" })
        local _, refused = pcall(workpane.ui.column, "bad")
        local future = workpane.dialogs.alert({ title = "t", message = "m" })
        return { id = "alpha", seen = { datetime = getmetatable(require("datetime").now()), owned = getmetatable(owned).kind, node = getmetatable(node), empty = next(node) == nil, nodeMethods = node.__index == nil and type(node.set) == "function", failureMethods = refused.__index == nil and getmetatable(refused) == "workpane.failure", futureMethods = future.__index == nil and type(future.await) == "function" } }
    )";
    std::ofstream(folder.path() / "beta" / "plugin.lua") << R"(
        return { id = "beta", seen = { encode = type(require("json").encode), sleep = type(require("async").sleep), language = workpane.app.languages[1] } }
    )";
    tests::ScriptHarness harness;
    const std::string source = R"(
        local loader = require("workpane.loader")
        local task = require("workpane.task")
        task.run("test", "test", function()
            local candidates = loader.discover({ FOLDER }, { paths = { data = "" }, languages = { "en" }, themes = {}, icons = {}, colors = {} })
            host.test_result({ alpha = candidates[1].candidate.definition.seen, beta = candidates[2].candidate.definition.seen, encode = type(require("json").encode) })
        end)
    )";
    ASSERT_TRUE(harness.run("local FOLDER = " + json(folder.path().generic_string()).dump() + "\n" + source).hasValue());
    // clang-format off
    ASSERT_TRUE(harness.pollUntil([&harness]() { return harness.results.size() == 1; })) << json(harness.logged).dump();
    // clang-format on

    const json& seen = harness.results[0];
    EXPECT_EQ(seen["alpha"]["datetime"], "workpane.protected");
    EXPECT_EQ(seen["alpha"]["owned"], "owned");
    EXPECT_EQ(seen["alpha"]["node"], "workpane.node");
    EXPECT_EQ(seen["alpha"]["empty"], true);
    EXPECT_EQ(seen["alpha"]["nodeMethods"], true);
    EXPECT_EQ(seen["alpha"]["failureMethods"], true);
    EXPECT_EQ(seen["alpha"]["futureMethods"], true);
    EXPECT_EQ(seen["beta"]["encode"], "function");
    EXPECT_EQ(seen["beta"]["sleep"], "function");
    EXPECT_EQ(seen["beta"]["language"], "en");
    EXPECT_EQ(seen["encode"], "function");
}

TEST(PluginRegistry, AcceptsAValidManifestAndRefusesBrokenOnes) {
    const std::filesystem::path plugins = "/opt/workpane/plugins";
    const std::vector<std::filesystem::path> folders{plugins, "/home/reader/plugins"};
    localization::Localization localization;
    PluginRegistry registry;

    for (const auto* identifier : {"sample", "other", "twice"}) {
        ASSERT_TRUE(tests::SampleManifest::install(localization, identifier).hasValue()) << identifier;
    }

    auto parsed = PluginRegistry::parse(tests::SampleManifest::create("sample", plugins), folders, localization);
    ASSERT_TRUE(parsed.hasValue()) << parsed.error().code << " " << parsed.error().detail;
    ASSERT_TRUE(registry.add(std::move(parsed.value())).hasValue());
    ASSERT_EQ(registry.navigation().size(), 1U);
    EXPECT_EQ(registry.navigation()[0].destination(), "sample:main");

    auto duplicate = PluginRegistry::parse(tests::SampleManifest::create("sample", plugins), folders, localization);
    EXPECT_EQ(registry.add(std::move(duplicate.value())).error().code, "plugin_duplicate");

    auto taken = PluginRegistry::parse(tests::SampleManifest::create("other", plugins), folders, localization);
    EXPECT_EQ(registry.add(std::move(taken.value())).error().code, "plugin_navigation_order_taken");

    // Two destinations of one plugin in one position are refused as well, even where nothing else holds that position.
    json twice = tests::SampleManifest::create("twice", plugins);
    twice["navigation"][0]["order"] = 20;
    twice["navigation"].push_back(twice["navigation"][0]);
    twice["navigation"][1]["id"] = "second";
    auto doubled = PluginRegistry::parse(twice, folders, localization);
    ASSERT_TRUE(doubled.hasValue()) << doubled.error().code;
    EXPECT_EQ(registry.add(std::move(doubled.value())).error().code, "plugin_navigation_order_taken");

    // Two hyphens in a row would give a table prefix that starts the prefix of another plugin.
    for (const auto* identifier : {"Bad Id", "-sample", "sample-", "sample--notes"}) {
        EXPECT_EQ(PluginRegistry::parse(tests::SampleManifest::create(identifier, plugins), folders, localization).error().code, "plugin_identifier_invalid") << identifier;
    }

    json elsewhere = tests::SampleManifest::create("sample", plugins);
    elsewhere["directory"] = "/tmp/sample";
    EXPECT_EQ(PluginRegistry::parse(elsewhere, folders, localization).error().code, "plugin_directory_invalid");
    EXPECT_TRUE(PluginRegistry::parse(tests::SampleManifest::create("sample", folders[1]), folders, localization).hasValue());
    EXPECT_EQ(PluginRegistry::parse(tests::SampleManifest::create("sample", "/home/reader"), folders, localization).error().code, "plugin_directory_invalid");

    // A title is spelled by the catalog the plugin installed when it was discovered, so a plugin without one is refused.
    json untranslated = tests::SampleManifest::create("sample", plugins);
    untranslated["titleKey"] = "sample.plugin.missing";
    EXPECT_EQ(PluginRegistry::parse(untranslated, folders, localization).error().code, "plugin_title_untranslated");
    EXPECT_EQ(PluginRegistry::parse(tests::SampleManifest::create("uninstalled", plugins), folders, localization).error().code, "plugin_title_untranslated");

    json shortcut = tests::SampleManifest::create("sample", plugins);
    shortcut["navigation"][0]["shortcuts"] = json::array({{{"id", "refresh"}, {"keys", "mod+r"}}});
    EXPECT_TRUE(PluginRegistry::parse(shortcut, folders, localization).hasValue());

    for (const auto& keys : {"r", "mod+q", "mod+1", "mod+unknown"}) {
        shortcut["navigation"][0]["shortcuts"] = json::array({{{"id", "refresh"}, {"keys", keys}}});
        EXPECT_EQ(PluginRegistry::parse(shortcut, folders, localization).error().code, "navigation_shortcut_invalid") << keys;
    }

    shortcut["navigation"][0]["shortcuts"] = json::array({{{"id", "refresh"}, {"keys", "mod+r"}}, {{"id", "reload"}, {"keys", "mod+r"}}});
    EXPECT_EQ(PluginRegistry::parse(shortcut, folders, localization).error().code, "navigation_shortcut_invalid");

    registry.remove("sample");
    EXPECT_TRUE(registry.navigation().empty());
    EXPECT_EQ(registry.find("sample"), nullptr);
}

// A band stands across the window at a position nobody else claims, from 16 to 320 points tall or hidden at zero, and keeps the height its plugin gives it later.
TEST(PluginRegistry, KeepsTheBandsOfEveryPluginApart) {
    const std::filesystem::path plugins = "/opt/workpane/plugins";
    localization::Localization localization;
    PluginRegistry registry;

    for (const auto* identifier : {"sample", "other"}) {
        ASSERT_TRUE(tests::SampleManifest::install(localization, identifier).hasValue()) << identifier;
    }

    // clang-format off
    const auto banded = [&plugins](const std::string& id, std::int64_t order, std::int64_t height) {
        json manifest = tests::SampleManifest::create(id, plugins);
        manifest["navigation"][0]["order"] = order;
        manifest["bands"] = json::array({{{"id", "strip"}, {"titleKey", id + ".band.strip"}, {"placement", "bottom"}, {"order", order}, {"height", height}}});

        return manifest;
    };
    // clang-format on

    auto sample = PluginRegistry::parse(banded("sample", 10, 80), {plugins}, localization);
    ASSERT_TRUE(sample.hasValue()) << sample.error().code << " " << sample.error().detail;
    ASSERT_TRUE(registry.add(std::move(sample.value())).hasValue());
    ASSERT_EQ(registry.bands().size(), 1U);
    EXPECT_EQ(registry.bands()[0].surface(), "band:sample:strip");
    EXPECT_EQ(registry.bands()[0].height, 80);

    auto taken = PluginRegistry::parse(banded("other", 10, 40), {plugins}, localization);
    ASSERT_TRUE(taken.hasValue());
    taken.value().navigation[0].order = 20;
    EXPECT_EQ(registry.add(std::move(taken.value())).error().code, "plugin_band_order_taken");

    // Every height a band cannot take is refused by the rule of bands, whether it is too small, too large or negative.
    for (const std::int64_t height : {-40, 1, 15, 321}) {
        const auto refused = PluginRegistry::parse(banded("other", 30, height), {plugins}, localization);
        ASSERT_FALSE(refused.hasValue()) << height;
        EXPECT_EQ(refused.error().code, "shell_band_height_invalid") << height;
    }

    EXPECT_TRUE(PluginRegistry::parse(banded("other", 30, 0), {plugins}, localization).hasValue());
    EXPECT_TRUE(registry.resizeBand("sample", "strip", 0).hasValue());
    EXPECT_EQ(registry.bands()[0].height, 0);
    EXPECT_TRUE(registry.resizeBand("sample", "strip", 120).hasValue());
    EXPECT_EQ(registry.bands()[0].height, 120);
    EXPECT_EQ(registry.resizeBand("sample", "strip", 8).error().code, "shell_band_height_invalid");
    EXPECT_EQ(registry.resizeBand("sample", "strip", -8).error().code, "shell_band_height_invalid");
    EXPECT_EQ(registry.resizeBand("sample", "strip", 400).error().code, "shell_band_height_invalid");
    EXPECT_EQ(registry.resizeBand("sample", "missing", 40).error().code, "shell_band_unknown");
    EXPECT_EQ(registry.resizeBand("other", "strip", 40).error().code, "shell_band_unknown");
}

} // namespace workpane::scripting
