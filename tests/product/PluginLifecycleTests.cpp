#include "Error.h"
#include "app/Product.h"
#include "persistence/PreferenceStore.h"
#include "scripting/PluginRegistry.h"
#include "scripting/ScriptRuntime.h"
#include "support/HeadlessProduct.h"
#include "support/ProductDatabase.h"
#include "support/RootedPath.h"
#include "support/SurfaceReader.h"
#include "support/TemporaryDirectory.h"
#include "support/TerminalRecord.h"
#include "ui/model/Surface.h"
#include "ui/shell/Shell.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace workpane::tests {

using nlohmann::json;

// Drives every case of the life of a plugin the reader turns off and on, and reads the changes of state the plugin manager writes to the log.
class PluginLifecycleTest : public ::testing::Test {
  protected:
    enum class PullEnd { Program, Plugin, Product };

    static constexpr std::string_view installed{"settings:workpane:plugins:installed"};

    // Writes a plugin of the test into a folder of plugins, with its catalog and the definition given.
    static void install(const std::filesystem::path& folder, const std::string& id, const std::string& definition) {
        std::filesystem::create_directories(folder / id);
        std::ofstream(folder / id / "plugin.lua") << definition;
        std::ofstream(folder / id / "translations.lua") << "return { en = { [\"" << id << ".plugin.title\"] = \"" << id << "\" }, pt = { [\"" << id << ".plugin.title\"] = \"" << id << "\" } }";
    }

    // Stores the core preferences a test starts from, such as the folder of its plugins and the plugins turned off.
    static void prepare(const TemporaryDirectory& data, const json& document) {
        auto database = tests::ProductDatabase::open(data);
        ASSERT_TRUE(persistence::PreferenceStore::store(database, "workpane", document).hasValue());
    }

    // Opens the Plugins group of the settings through the search, which is how the reader reaches it.
    static void openPlugins(HeadlessProduct& product) {
        ASSERT_TRUE(product.navigate(ui::Shell::settingsDestination).hasValue());
        product.frame();
        product.frame();
        product.press(HeadlessProduct::command() | ImGuiKey_F);
        product.frame();
        product.type("plugin");
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.surface(installed) != nullptr; }));
        // clang-format on
    }

    // Answers the control of the row of a plugin in the Plugins section, found by the title of the row: its switch or the button that erases its data.
    [[nodiscard]] static std::optional<ui::NodeId> control(HeadlessProduct& product, std::string_view titleKey, std::string_view kind) {
        const SurfaceReader reader(product.declared(installed));
        const auto rows = reader.nodes("settingsRow");
        const auto controls = reader.nodes(kind);

        for (std::size_t index = 0; index < rows.size() && index < controls.size(); ++index) {
            if (reader.properties(rows[index])["label"].value("key", "") == titleKey) {
                return controls[index];
            }
        }

        return std::nullopt;
    }

    static void turn(HeadlessProduct& product, std::string_view titleKey, bool on) {
        const auto found = control(product, titleKey, "toggle");
        ASSERT_TRUE(found.has_value()) << titleKey;
        product.emit(installed, *found, "change", {{"checked", on}});
    }

    [[nodiscard]] static bool switchable(HeadlessProduct& product, std::string_view titleKey) {
        return SurfaceReader(product.declared(installed)).properties(*control(product, titleKey, "toggle")).value("enabled", true);
    }

    [[nodiscard]] static bool checked(HeadlessProduct& product, std::string_view titleKey) {
        return SurfaceReader(product.declared(installed)).properties(*control(product, titleKey, "toggle")).value("checked", false);
    }

    [[nodiscard]] static bool running(HeadlessProduct& product, std::string_view id) {
        return product.product().plugins().find(std::string(id)) != nullptr;
    }

    // Answers the changes of state the log recorded for a plugin, each written as the state it left and the state it entered.
    [[nodiscard]] static std::vector<std::string> transitions(HeadlessProduct& product, std::string_view plugin) {
        std::vector<std::string> found;

        for (const auto& row : product.query("SELECT details_json FROM logs__entries WHERE source = 'workpane' AND category = 'plugins' AND message = 'A plugin changed its state' ORDER BY sequence")) {
            const json details = json::parse(row["details_json"].get<std::string>());

            if (details.value("plugin", "") == plugin) {
                found.push_back(details.value("from", "none") + ">" + details.value("to", ""));
            }
        }

        return found;
    }

    // Answers whether the recorded changes of state of a plugin end with the ones given, in that order.
    [[nodiscard]] static bool endsWith(const std::vector<std::string>& recorded, const std::vector<std::string>& last) {
        return recorded.size() >= last.size() && std::equal(last.begin(), last.end(), recorded.end() - static_cast<std::ptrdiff_t>(last.size()));
    }

    [[nodiscard]] static std::vector<json> errors(HeadlessProduct& product) {
        return product.query("SELECT source, message, details_json FROM logs__entries WHERE level = 'error'");
    }

    static void verifyStagingRelease(bool closeProduct) {
        TemporaryDirectory data;
        TemporaryDirectory folder;
        HeadlessProduct product(data.path());
        ASSERT_TRUE(product.boot().hasValue());
        const auto staging = folder.path() / "staging";
        const auto source = "local FOLDER = " + json(staging.generic_string()).dump() + "\n" + R"lua(
            local bridge = require("workpane.bridge")
            local async = require("async")
            local waiting
            waiting, releaseStagingWork = async.deferred()
            require("workpane.task").run("remote-ssh", "suite", function()
                local stage = require("workpane.task").await(require("workpane.files").staging("remote-ssh", FOLDER))
                stage:run(function()
                    local written = require("fs").writeFile(stage.path .. "/accepted", string.rep("x", 16 << 20))
                    bridge.call("workpane_log", { plugin = "remote-ssh", level = "info", category = "suite", message = "staging-write-accepted", details = {} })

                    return async.promise(function()
                        local value, problem = written:await()
                        assert(problem == nil, problem)
                        waiting:await()

                        return value
                    end)
                end)
            end)
        )lua";
        ASSERT_TRUE(product.product().runtime().loadString(source, "staging-release").hasValue());
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return !product.query("SELECT sequence FROM logs__entries WHERE message = 'staging-write-accepted'").empty(); }));
        // clang-format on
        ASSERT_TRUE(std::filesystem::is_directory(staging));

        if (closeProduct) {
            product.stop();
            EXPECT_FALSE(std::filesystem::exists(staging));
            return;
        }

        openPlugins(product);
        turn(product, "remote-ssh.plugin.title", false);
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return !running(product, "remote-ssh"); }));
        // clang-format on
        product.settle(std::chrono::milliseconds(50));
        EXPECT_TRUE(std::filesystem::is_regular_file(staging / "accepted"));
        ASSERT_TRUE(product.product().runtime().loadString("releaseStagingWork()", "staging-finish").hasValue());
        // clang-format off
        EXPECT_TRUE(product.frameUntil([&]() { return !std::filesystem::exists(staging); }));
        // clang-format on
        EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
        product.stop();
    }

    void verifyPullRelease(PullEnd ending) {
        TemporaryDirectory data;
        HeadlessProduct product(data.path());
        product.processes().endsOnStop = false;
        ASSERT_TRUE(product.boot().hasValue());
        const auto executable = RootedPath::of("bin/pull-producer").generic_string();
        const auto directory = RootedPath::of("home").generic_string();
        const std::string source = "local PROGRAM = " + json(executable).dump() + "\nlocal DIRECTORY = " + json(directory).dump() + "\n" + R"lua(
            local process = require("workpane.process")
            local bridge = require("workpane.bridge")
            local identity = process.start("remote-ssh", { program = PROGRAM, directory = DIRECTORY, pullOutput = true, onExit = function(code)
                bridge.call("workpane_log", { plugin = "remote-ssh", level = "info", category = "pull-test", message = "The stopped pull producer ended", details = { code = code } })
            end })

            function stopPullTest()
                process.stop("remote-ssh", identity)
            end
        )lua";
        ASSERT_TRUE(product.product().runtime().loadString(source, "pull-release").hasValue());
        ASSERT_EQ(product.processes().events.size(), 1U);
        const auto events = product.processes().events.front();
        std::atomic<std::size_t> sent{0};
        // clang-format off
        std::thread producer([&events, &sent]() {
            for (std::size_t index = 0; index < 32; ++index) {
                events.output(process::ProcessStream::Output, std::string(65536, 'x'));
                ++sent;
            }

            events.exited({0, false});
        });

        EXPECT_TRUE(product.frameUntil([&sent]() { return sent.load() >= 16U; }));
        // clang-format on
        product.settle(std::chrono::milliseconds(30));
        EXPECT_EQ(sent.load(), 16U);

        if (ending == PullEnd::Product) {
            product.stop();
        } else if (ending == PullEnd::Plugin) {
            openPlugins(product);
            turn(product, "remote-ssh.plugin.title", false);
            // clang-format off
            EXPECT_TRUE(product.frameUntil([&product, &sent]() { return !running(product, "remote-ssh") && sent.load() == 32U; }));
            // clang-format on
            product.stop();
        } else {
            EXPECT_TRUE(product.product().runtime().loadString("stopPullTest()", "pull-stop").hasValue());
            // clang-format off
            EXPECT_TRUE(product.frameUntil([&product, &sent]() { return sent.load() == 32U && !product.query("SELECT sequence FROM logs__entries WHERE message = 'The stopped pull producer ended'").empty(); }));
            // clang-format on
            product.stop();
        }

        producer.join();
        EXPECT_EQ(sent.load(), 32U);
        EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    }

    void verifyOutputFlow(std::size_t chunkCount, std::size_t chunkSize) {
        TemporaryDirectory data;
        TemporaryDirectory plugins;
        prepare(data, {{"pluginFolders", json::array({plugins.path().generic_string()})}});
        install(plugins.path(), "output-flow", R"(
            local bytes = 0
            return {
                id = "output-flow", sdk = 6, titleKey = "output-flow.plugin.title",
                start = function()
                    local program = workpane.await(workpane.process.find("output-producer"))
                    workpane.process.start({ program = program, directory = workpane.system.home(), onOutput = function(chunks)
                        for _, chunk in ipairs(chunks) do
                            bytes = bytes + #chunk.text
                        end

                    end, onExit = function()
                        workpane.log.info("flow", "The process output drained", { bytes = bytes })
                    end })
                end,
            }
        )");
        HeadlessProduct product(data.path());
        product.processes().executables["output-producer"] = RootedPath::of("bin/output-producer");
        ASSERT_TRUE(product.boot().hasValue());
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.processes().events.size() == 1U; }));
        // clang-format on
        const auto output = product.processes().events.front().output;
        const auto exit = product.processes().events.front().exited;
        std::mutex mutex;
        std::condition_variable progress;
        bool started = false;
        bool finished = false;
        // clang-format off
        std::thread producer([&]() {
            for (std::size_t index = 0; index < chunkCount; ++index) {
                output(index % 2 == 0 ? process::ProcessStream::Output : process::ProcessStream::Error, std::string(chunkSize, 'x'));
                const std::lock_guard lock(mutex);
                started = true;
                progress.notify_one();
            }

            exit({0, false});
            const std::lock_guard lock(mutex);
            finished = true;
            progress.notify_one();
        });
        // clang-format on

        {
            std::unique_lock lock(mutex);
            // clang-format off
            EXPECT_TRUE(progress.wait_for(lock, std::chrono::seconds(5), [&]() { return started; }));
            EXPECT_FALSE(progress.wait_for(lock, std::chrono::seconds(1), [&]() { return finished; }));
            // clang-format on
        }

        // clang-format off
        const bool drained = product.frameUntil([&]() { return !product.query("SELECT details_json FROM logs__entries WHERE source = 'output-flow' AND message = 'The process output drained'").empty(); });
        // clang-format on
        product.stop();
        producer.join();
        EXPECT_TRUE(drained);
        EXPECT_TRUE(finished);
        const auto entries = product.query("SELECT details_json FROM logs__entries WHERE source = 'output-flow' AND message = 'The process output drained'");
        ASSERT_EQ(entries.size(), 1U);
        EXPECT_EQ(json::parse(entries.front()["details_json"].get<std::string>())["bytes"], chunkCount * chunkSize);
        EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    }
};

TEST_F(PluginLifecycleTest, KeepsAcceptedStagingWorkAliveAfterItsPluginIsWithdrawn) {
    verifyStagingRelease(false);
}

TEST_F(PluginLifecycleTest, ReleasesPrivateStagingAfterTheRuntimeFinishesAcceptedFileWork) {
    verifyStagingRelease(true);
}

TEST_F(PluginLifecycleTest, ReleasesStagingReservedBeforeAPluginStartFails) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    const auto staging = data.path() / "staging";
    install(folder.path(), "staging", R"(return { id = "staging", sdk = 6, titleKey = "staging.plugin.title", start = function()
        workpane.files.staging(workpane.app.dataDirectory .. "/staging")
        error("start refused")
    end })");
    prepare(data, {{"pluginFolders", json::array({folder.path().generic_string()})}});
    HeadlessProduct product(data.path());
    product.recordCalls("workpane_files_staging_ready");
    product.recordCalls("workpane_files_staging_close");
    ASSERT_TRUE(product.boot().hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.calls("workpane_files_staging_close") == 1U && !std::filesystem::exists(staging); }));
    // clang-format on
    EXPECT_EQ(product.calls("workpane_files_staging_ready"), 1U);
    EXPECT_FALSE(running(product, "staging"));
    product.stop();
    EXPECT_FALSE(std::filesystem::exists(staging));
}

TEST_F(PluginLifecycleTest, PreservesBinaryProcessStreamsWithoutChangingTextStreamDecoding) {
    TemporaryDirectory data;
    TemporaryDirectory plugins;
    prepare(data, {{"pluginFolders", json::array({plugins.path().generic_string()})}});
    install(plugins.path(), "binary-flow", R"(
        local crypto = require("crypto")

        return {
            id = "binary-flow", sdk = 6, titleKey = "binary-flow.plugin.title",
            start = function()
                local program = workpane.await(workpane.process.find("binary-producer"))

                for _, binary in ipairs({ true, false }) do
                    local output = ""
                    local diagnostic = ""

                    workpane.process.start({ program = program, directory = workpane.system.home(), binaryOutput = binary, onOutput = function(chunks)
                        for _, chunk in ipairs(chunks) do
                            if chunk.stream == "output" then
                                output = output .. chunk.text
                            else
                                diagnostic = diagnostic .. chunk.text
                            end
                        end

                    end, onExit = function(code, unsuccessful)
                        workpane.log.info("flow", "The process stream ended", { binary = binary, output = crypto.hexEncode(output), diagnostic = crypto.hexEncode(diagnostic), code = code, failed = unsuccessful })
                    end })
                end
            end,
        }
    )");
    HeadlessProduct product(data.path());
    product.processes().executables["binary-producer"] = RootedPath::of("bin/binary-producer");
    ASSERT_TRUE(product.boot().hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product]() { return product.processes().events.size() == 2U; }));
    // clang-format on

    for (const auto& events : product.processes().events) {
        events.output(process::ProcessStream::Output, std::string(1, '\xc3'));
        events.output(process::ProcessStream::Error, std::string("\0\xff", 2));
    }

    product.frame();

    for (std::size_t index = 0; index < product.processes().events.size(); ++index) {
        product.processes().events[index].output(process::ProcessStream::Output, std::string("\xa9\x80", 2));
        product.processes().ended[index] = true;
        product.processes().events[index].exited({7, false});
    }

    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product]() { return product.query("SELECT details_json FROM logs__entries WHERE source = 'binary-flow' AND message = 'The process stream ended'").size() == 2U; }));
    // clang-format on
    const auto rows = product.query("SELECT details_json FROM logs__entries WHERE source = 'binary-flow' AND message = 'The process stream ended'");

    for (const auto& row : rows) {
        const auto details = json::parse(row["details_json"].get<std::string>());
        const bool binary = details["binary"].get<bool>();
        EXPECT_EQ(details["output"], binary ? "c3a980" : "c3a9efbfbd");
        EXPECT_EQ(details["diagnostic"], binary ? "00ff" : "00efbfbd");
        EXPECT_EQ(details["code"], 7);
        EXPECT_EQ(details["failed"], true);
    }

    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(PluginLifecycleTest, PausesAProcessFloodUntilTheInterfaceDrainsItsOutput) {
    verifyOutputFlow(32, 1024 * 1024);
}

TEST_F(PluginLifecycleTest, HashesExactBinaryRevisionsThroughTheSdkWorkerAndRejectsMalformedTransport) {
    TemporaryDirectory data;
    TemporaryDirectory plugins;
    prepare(data, {{"pluginFolders", json::array({plugins.path().generic_string()})}});
    install(plugins.path(), "hash-flow", R"lua(
        return {
            id = "hash-flow", sdk = 6, titleKey = "hash-flow.plugin.title",
            start = function()
                workpane.task(function()
                    assert(workpane.await(workpane.files.hash("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
                    assert(workpane.await(workpane.files.hash("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
                    assert(workpane.await(workpane.files.hash("\0\255")) == "06eb7d6a69ee19e5fbdf749018d3d2abfa04bcbd1365db312eb86dc7169389b8")
                    local accepted, problem = pcall(workpane.files.hash, string.rep("x", 4 * 1024 * 1024 + 1))
                    assert(not accepted and problem.code == "files_hash_invalid")
                    workpane.log.info("files", "The binary revisions were hashed")
                end)
            end,
        }
    )lua");
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product]() { return !product.query("SELECT rowid FROM logs__entries WHERE source = 'hash-flow' AND message = 'The binary revisions were hashed'").empty(); }));
    // clang-format on
    const std::string source = R"lua(
        local bridge = require("workpane.bridge")
        local task = require("workpane.task")
        task.run("remote-ssh", "test", function()
            local _, problem = bridge.request("workpane_files_hash", { plugin = "remote-ssh", content = "AB==" }):await()
            assert(problem.code == "files_hash_invalid" and problem.detail == "")
            bridge.call("workpane_log", { plugin = "remote-ssh", level = "info", category = "hash-test", message = "The malformed binary transport was refused" })
        end)
    )lua";
    product.recordCalls("workpane_log");
    ASSERT_TRUE(product.product().runtime().loadString(source, "hash-transport").hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product]() { const auto value = product.lastCall("workpane_log"); return value.is_object() && value.value("category", "") == "hash-test"; }));
    // clang-format on
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(PluginLifecycleTest, EndsOwnedInputThroughTheSdkAndKeepsOutputAndActualExitAvailable) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    product.recordCalls("workpane_log");
    const auto executable = RootedPath::of("bin/eof-producer").generic_string();
    const auto directory = RootedPath::of("home").generic_string();
    const std::string source = "local PROGRAM = " + json(executable).dump() + "\nlocal DIRECTORY = " + json(directory).dump() + "\n" + R"lua(
        local process = require("workpane.process")
        local bridge = require("workpane.bridge")
        local crypto = require("crypto")
        local task = require("workpane.task")
        local identity = process.start("remote-ssh", { program = PROGRAM, directory = DIRECTORY, pullOutput = true })
        process.write("remote-ssh", identity, "accepted\0\255")
        local accepted, failure = pcall(process.endInput, "code-editor", identity)
        assert(not accepted and failure.code == "process_unknown")
        process.endInput("remote-ssh", identity)
        accepted, failure = pcall(process.write, "remote-ssh", identity, "late")
        assert(not accepted and failure.code == "process_input_refused")

        task.run("remote-ssh", "test", function()
            local value = task.await(process.read("remote-ssh", identity))
            assert(value.ended and value.code == 7 and value.failed and #value.chunks == 1)
            bridge.call("workpane_log", { plugin = "remote-ssh", level = "info", category = "eof-test", message = "The owned input ended", details = { output = crypto.hexEncode(value.chunks[1].text) } })
        end)
    )lua";
    ASSERT_TRUE(product.product().runtime().loadString(source, "owned-eof").hasValue());
    ASSERT_EQ(product.processes().events.size(), 1U);
    EXPECT_EQ(product.processes().inputs.front(), std::string("accepted\0\xff", 10));
    EXPECT_FALSE(product.processes().stopped.front());
    product.processes().events.front().output(process::ProcessStream::Output, "final");
    product.processes().ended.front() = true;
    product.processes().events.front().exited({7, false});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product]() { const auto value = product.lastCall("workpane_log"); return value.is_object() && value.value("category", "") == "eof-test"; }));
    // clang-format on
    EXPECT_EQ(product.lastCall("workpane_log")["details"]["output"], "66696e616c");
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(PluginLifecycleTest, PullsExactProcessBytesAndReportsExitOnlyAfterTheFinalRead) {
    TemporaryDirectory data;
    TemporaryDirectory plugins;
    prepare(data, {{"pluginFolders", json::array({plugins.path().generic_string()})}});
    install(plugins.path(), "process-pull", R"(
        local crypto = require("crypto")

        return {
            id = "process-pull", sdk = 6, titleKey = "process-pull.plugin.title",
            start = function()
                local program = workpane.await(workpane.process.find("pull-producer"))
                local identity = workpane.process.start({ program = program, directory = workpane.system.home(), pullOutput = true })

                workpane.task(function()
                    local reads = {}
                    local output = ""
                    local diagnostic = ""

                    while true do
                        local value = workpane.await(workpane.process.read(identity, 2))
                        local count = 0

                        for _, chunk in ipairs(value.chunks) do
                            count = count + #chunk.text

                            if chunk.stream == "output" then
                                output = output .. chunk.text
                            else
                                diagnostic = diagnostic .. chunk.text
                            end
                        end

                        assert(count <= 2)
                        reads[#reads + 1] = { bytes = count, ended = value.ended }

                        if value.ended then
                            workpane.log.info("pull", "The process reads completed", { output = crypto.hexEncode(output), diagnostic = crypto.hexEncode(diagnostic), code = value.code, failed = value.failed, reads = reads })
                            break
                        end
                    end
                end)
            end,
        }
    )");
    HeadlessProduct product(data.path());
    product.processes().executables["pull-producer"] = RootedPath::of("bin/pull-producer");
    product.recordCalls("workpane_process_read");
    ASSERT_TRUE(product.boot().hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product]() { return product.calls("workpane_process_read") == 1U; }));
    // clang-format on
    EXPECT_TRUE(product.query("SELECT details_json FROM logs__entries WHERE source = 'process-pull' AND message = 'The process reads completed'").empty());
    ASSERT_EQ(product.processes().events.size(), 1U);
    product.processes().events.front().output(process::ProcessStream::Output, "ABC");
    product.processes().events.front().output(process::ProcessStream::Error, std::string("\0\xff", 2));
    product.processes().ended.front() = true;
    product.processes().events.front().exited({7, false});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product]() { return !product.query("SELECT details_json FROM logs__entries WHERE source = 'process-pull' AND message = 'The process reads completed'").empty(); }));
    // clang-format on
    const auto rows = product.query("SELECT details_json FROM logs__entries WHERE source = 'process-pull' AND message = 'The process reads completed'");
    ASSERT_EQ(rows.size(), 1U);
    const auto details = json::parse(rows.front()["details_json"].get<std::string>());
    EXPECT_EQ(details["output"], "414243");
    EXPECT_EQ(details["diagnostic"], "00ff");
    EXPECT_EQ(details["code"], 7);
    EXPECT_EQ(details["failed"], true);
    EXPECT_EQ(details["reads"], json::array({{{"bytes", 2}, {"ended", false}}, {{"bytes", 2}, {"ended", false}}, {{"bytes", 1}, {"ended", true}}}));
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(PluginLifecycleTest, BoundsTheNumberOfAlternatingOutputChunksBeforeDelivery) {
    verifyOutputFlow(32768, 1);
}

TEST_F(PluginLifecycleTest, HoldsPullOutputWithinItsNativeBoundUntilItsReaderStarts) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    const auto executable = RootedPath::of("bin/pull-producer").generic_string();
    const auto directory = RootedPath::of("home").generic_string();
    const std::string source = "local PROGRAM = " + json(executable).dump() + "\nlocal DIRECTORY = " + json(directory).dump() + "\n" + R"lua(
        local process = require("workpane.process")
        local task = require("workpane.task")
        local bridge = require("workpane.bridge")
        local identity = process.start("remote-ssh", { program = PROGRAM, directory = DIRECTORY, pullOutput = true })

        function startPullReader()
            task.run("remote-ssh", "test", function()
                local count = 0

                while true do
                    local value = task.await(process.read("remote-ssh", identity))

                    for _, chunk in ipairs(value.chunks) do
                        count = count + #chunk.text
                    end

                    if value.ended then
                        bridge.call("workpane_log", { plugin = "remote-ssh", level = "info", category = "pull-test", message = "The bounded pull completed", details = { bytes = count, code = value.code } })
                        break
                    end
                end
            end)
        end
    )lua";
    product.recordCalls("workpane_log");
    ASSERT_TRUE(product.product().runtime().loadString(source, "pull-backpressure").hasValue());
    ASSERT_EQ(product.processes().events.size(), 1U);
    const auto events = product.processes().events.front();
    std::atomic<std::size_t> sent{0};
    // clang-format off
    std::thread producer([&events, &sent]() {
        for (std::size_t index = 0; index < 32; ++index) {
            events.output(process::ProcessStream::Output, std::string(65536, 'x'));
            ++sent;
        }

        events.exited({9, false});
    });

    const bool bounded = product.frameUntil([&sent]() { return sent.load() >= 16U; });
    // clang-format on
    product.settle(std::chrono::milliseconds(30));
    EXPECT_TRUE(bounded);
    EXPECT_EQ(sent.load(), 16U);
    const auto accepted = product.product().runtime().loadString("startPullReader()", "pull-start");
    bool completed = false;

    if (accepted.hasValue()) {
        // clang-format off
        completed = product.frameUntil([&product]() { const auto value = product.lastCall("workpane_log"); return value.is_object() && value.value("message", "") == "The bounded pull completed"; });
        // clang-format on
    }

    if (!completed) {
        product.stop();
    }

    producer.join();
    ASSERT_TRUE(accepted.hasValue());
    ASSERT_TRUE(completed);
    EXPECT_EQ(product.lastCall("workpane_log")["details"], json({{"bytes", 2097152}, {"code", 9}}));
    EXPECT_EQ(sent.load(), 32U);
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(PluginLifecycleTest, ReleasesUnreadPullProducersWhenTheirPluginIsWithdrawn) {
    verifyPullRelease(PullEnd::Plugin);
}

TEST_F(PluginLifecycleTest, StopsUnreadPullProducersWithoutRequiringAnotherRead) {
    verifyPullRelease(PullEnd::Program);
}

TEST_F(PluginLifecycleTest, KeepsACancelledPullReaderUntilTheActualNativeExitAndDiscardsUnreadBytes) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    product.processes().endsOnStop = false;
    ASSERT_TRUE(product.boot().hasValue());
    const auto executable = RootedPath::of("bin/pull-producer").generic_string();
    const auto directory = data.path().generic_string();
    const std::string source = "local PROGRAM = " + json(executable).dump() + "\nlocal DIRECTORY = " + json(directory).dump() + "\n" + R"lua(
        local process = require("workpane.process")
        local bridge = require("workpane.bridge")
        local task = require("workpane.task")
        local identity = process.start("remote-ssh", { program = PROGRAM, directory = DIRECTORY, pullOutput = true, onExit = function(code, unsuccessful)
            bridge.call("workpane_log", { plugin = "remote-ssh", level = "info", category = "pull-test", message = "The cancelled producer exited", details = { code = code, failed = unsuccessful } })
        end })

        task.run("remote-ssh", "test", function()
            local reply = task.await(process.read("remote-ssh", identity))
            assert(reply.ended and #reply.chunks == 0)
            bridge.call("workpane_log", { plugin = "remote-ssh", level = "info", category = "pull-test", message = "The cancelled reader completed", details = { code = reply.code, failed = reply.failed } })
        end)

        function cancelPullReader()
            process.stop("remote-ssh", identity)
        end
    )lua";
    product.recordCalls("workpane_process_read");
    ASSERT_TRUE(product.product().runtime().loadString(source, "pull-cancelled-reader").hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product]() { return product.calls("workpane_process_read") == 1U; }));
    // clang-format on
    ASSERT_TRUE(product.product().runtime().loadString("cancelPullReader()", "pull-cancel").hasValue());
    ASSERT_EQ(product.processes().events.size(), 1U);
    product.processes().events.front().output(process::ProcessStream::Output, "discarded after cancellation");
    product.frame();
    EXPECT_TRUE(product.query("SELECT details_json FROM logs__entries WHERE message IN ('The cancelled producer exited', 'The cancelled reader completed')").empty());
    product.processes().events.front().exited({9, true});
    product.processes().ended.front() = true;
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product]() { return product.query("SELECT details_json FROM logs__entries WHERE message IN ('The cancelled producer exited', 'The cancelled reader completed')").size() == 2U; }));
    // clang-format on

    for (const auto& row : product.query("SELECT details_json FROM logs__entries WHERE message IN ('The cancelled producer exited', 'The cancelled reader completed')")) {
        EXPECT_EQ(json::parse(row["details_json"].get<std::string>()), json({{"code", 9}, {"failed", true}}));
    }

    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(PluginLifecycleTest, RunsAnExplicitTerminalProgramWithOwnedInputResizeAndFinalOutput) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    const auto initialTerminals = product.terminals().processes.size();
    const auto executable = RootedPath::of("bin/ssh").generic_string();
    const auto directory = data.path().generic_string();
    const std::string source = "local PROGRAM = " + json(executable).dump() + "\nlocal DIRECTORY = " + json(directory).dump() + "\n" + R"lua(
        local process = require("workpane.process")
        local bridge = require("workpane.bridge")
        local task = require("workpane.task")
        local crypto = require("crypto")
        local identity = process.start("remote-ssh", { program = PROGRAM, arguments = { "-tt", "literal '[]; 雪" }, directory = DIRECTORY, variables = { SSH_ASKPASS_REQUIRE = "force" }, cleared = { "SSH_ASKPASS" }, terminal = { columns = 80, rows = 24 }, pullOutput = true })
        process.resize("remote-ssh", identity, 120, 40)
        process.write("remote-ssh", identity, "commands\r")

        task.run("remote-ssh", "test", function()
            local output = ""

            while true do
                local value = task.await(process.read("remote-ssh", identity, 2))

                for _, chunk in ipairs(value.chunks) do
                    assert(chunk.stream == "output")
                    output = output .. chunk.text
                end

                if value.ended then
                    bridge.call("workpane_log", { plugin = "remote-ssh", level = "info", category = "pty-test", message = "The terminal program completed", details = { content = crypto.hexEncode(output), code = value.code } })
                    break
                end
            end
        end)
    )lua";
    product.recordCalls("workpane_log");
    ASSERT_TRUE(product.product().runtime().loadString(source, "terminal-program").hasValue());
    ASSERT_EQ(product.terminals().processes.size(), initialTerminals + 1U);
    const auto terminal = product.terminals().processes.back();
    ASSERT_TRUE(terminal->launch.program.has_value());
    EXPECT_EQ(terminal->launch.program->program.generic_string(), executable);
    EXPECT_EQ(terminal->launch.program->arguments, std::vector<std::string>({"-tt", "literal '[]; 雪"}));
    EXPECT_EQ(terminal->launch.program->variables, (std::vector<std::pair<std::string, std::string>>{{"SSH_ASKPASS_REQUIRE", "force"}}));
    EXPECT_EQ(terminal->launch.program->cleared, std::vector<std::string>({"SSH_ASKPASS"}));

    {
        const std::lock_guard lock(terminal->mutex);
        EXPECT_EQ(terminal->input, "commands\r");
        EXPECT_EQ(terminal->sizes, (std::vector<std::pair<int, int>>{{120, 40}}));
        terminal->output = std::string("A\0\xff", 3);
        terminal->exit = 7;
        terminal->notifiesDuringRead = true;
    }

    terminal->launch.arrived();
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product]() { const auto value = product.lastCall("workpane_log"); return value.is_object() && value.value("message", "") == "The terminal program completed"; }));
    // clang-format on
    EXPECT_EQ(product.lastCall("workpane_log")["details"], json({{"content", "4100ff"}, {"code", 7}}));
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(PluginLifecycleTest, ReleasesUnreadPullProducersWhenTheProductCloses) {
    verifyPullRelease(PullEnd::Product);
}

TEST_F(PluginLifecycleTest, RefusesForeignAndConcurrentPullReadersAndReleasesAnAcceptedReader) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    const auto executable = RootedPath::of("bin/pull-producer").generic_string();
    const auto directory = RootedPath::of("home").generic_string();
    const std::string source = "local PROGRAM = " + json(executable).dump() + "\nlocal DIRECTORY = " + json(directory).dump() + "\n" + R"lua(
        local process = require("workpane.process")
        local bridge = require("workpane.bridge")
        local task = require("workpane.task")
        local identity = process.start("remote-ssh", { program = PROGRAM, directory = DIRECTORY, pullOutput = true })

        task.run("terminal", "test", function()
            local _, foreign = process.read("terminal", identity):await()
            assert(foreign.code == "process_unknown")
            local accepted = process.read("remote-ssh", identity)
            local _, duplicate = process.read("remote-ssh", identity):await()
            assert(duplicate.code == "process_read_pending")
            process.forget("remote-ssh")
            local _, withdrawn = accepted:await()
            assert(withdrawn.code == "plugin_not_running")
            bridge.call("workpane_log", { plugin = "terminal", level = "info", category = "pull-test", message = "The accepted reader was released", details = {} })
        end)
    )lua";
    product.recordCalls("workpane_log");
    ASSERT_TRUE(product.product().runtime().loadString(source, "pull-ownership").hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product]() { const auto value = product.lastCall("workpane_log"); return value.is_object() && value.value("message", "") == "The accepted reader was released"; }));
    // clang-format on
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(PluginLifecycleTest, CancelsTheTasksOfAPluginBeforeItIsStartedAgain) {
    TemporaryDirectory data;
    TemporaryDirectory plugins;
    prepare(data, {{"pluginFolders", json::array({plugins.path().generic_string()})}});
    install(plugins.path(), "late-task", R"(
        local async = require("async")
        local fs = require("fs")
        return {
            id = "late-task", sdk = 6, titleKey = "late-task.plugin.title",
            start = function()
                workpane.task(function()
                    while fs.stat(workpane.plugin.directory .. "/release"):await() == nil do
                        async.sleep(10):await()
                    end
                    workpane.log.info("lifecycle", "The old task resumed", {})
                end)
            end,
        }
    )");
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    openPlugins(product);
    turn(product, "late-task.plugin.title", false);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !running(product, "late-task") && switchable(product, "late-task.plugin.title"); }));
    // clang-format on
    install(plugins.path(), "late-task", R"(
        return {
            id = "late-task", sdk = 6, titleKey = "late-task.plugin.title",
            start = function()
                workpane.log.info("lifecycle", "The new instance started", {})
            end,
        }
    )");
    turn(product, "late-task.plugin.title", true);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return running(product, "late-task") && switchable(product, "late-task.plugin.title"); }));
    // clang-format on
    std::ofstream(plugins.path() / "late-task" / "release") << "ready";
    product.settle(std::chrono::milliseconds(150));
    EXPECT_TRUE(product.query("SELECT message FROM logs__entries WHERE source = 'late-task' AND message = 'The old task resumed'").empty());
    EXPECT_EQ(product.query("SELECT message FROM logs__entries WHERE source = 'late-task' AND message = 'The new instance started'").size(), 1U);
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// The Terminal builds its view ahead of time, and after the reader turns it off and on again the view comes back with its workspace and a new shell.
TEST_F(PluginLifecycleTest, BringsBackThePreloadedViewOfAPluginTurnedOffAndOn) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    const std::string view = "view:terminal:workspace";
    ASSERT_TRUE(product.navigate("terminal:workspace").hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(view) != nullptr && SurfaceReader(product.declared(view)).nodes("terminal").size() == 1U; }));
    // clang-format on

    openPlugins(product);
    turn(product, "terminal.plugin.title", false);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !running(product, "terminal") && product.surface(view) == nullptr; }));
    // clang-format on
    turn(product, "terminal.plugin.title", true);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(view) != nullptr && SurfaceReader(product.declared(view)).nodes("terminal").size() == 1U; }));
    // clang-format on
    EXPECT_EQ(product.terminals().processes.size(), 2U);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return endsWith(transitions(product, "terminal"), {"running>stopping", "stopping>disabled", "disabled>waiting", "waiting>starting", "starting>running"}); }));
    // clang-format on
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    product.stop();
}

// The games and the gallery wait for the reader to turn them on in every build, and a plugin turned on is remembered across a restart while the others stay off.
TEST_F(PluginLifecycleTest, KeepsThePluginsOffByDefaultUntilTheReaderTurnsThemOn) {
    TemporaryDirectory data;
    {
        HeadlessProduct product(data.path());
        ASSERT_TRUE(product.boot().hasValue());
        openPlugins(product);
        product.settle(std::chrono::milliseconds(200));
        EXPECT_FALSE(running(product, "task-hero"));
        EXPECT_FALSE(running(product, "flappy-bird"));
        EXPECT_FALSE(running(product, "components"));
        EXPECT_TRUE(product.product().shell().bands().empty());
        EXPECT_FALSE(product.navigate("flappy-bird:game").hasValue());
        EXPECT_FALSE(product.navigate("components:gallery").hasValue());
        EXPECT_FALSE(checked(product, "task-hero.plugin.title"));
        EXPECT_FALSE(checked(product, "flappy-bird.plugin.title"));
        EXPECT_FALSE(checked(product, "components.plugin.title"));
        EXPECT_EQ(transitions(product, "task-hero"), (std::vector<std::string>{"none>discovered", "discovered>disabled"}));
        EXPECT_EQ(transitions(product, "components"), (std::vector<std::string>{"none>discovered", "discovered>disabled"}));

        turn(product, "task-hero.plugin.title", true);
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.surface("band:task-hero:adventure") != nullptr; }));
        // clang-format on
        EXPECT_EQ(product.product().preferences().document("workpane")["pluginSwitches"], json({{"task-hero", true}}));
        EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
        product.stop();
    }

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface("band:task-hero:adventure") != nullptr; }));
    // clang-format on
    EXPECT_FALSE(running(product, "flappy-bird"));
    product.stop();
}

// A band leaves the window with its plugin and comes back when the reader turns the plugin on again.
TEST_F(PluginLifecycleTest, BringsBackTheBandOfAPluginTurnedOffAndOn) {
    TemporaryDirectory data;
    prepare(data, {{"pluginSwitches", {{"task-hero", true}}}});
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    const std::string band = "band:task-hero:adventure";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(band) != nullptr; }));
    // clang-format on

    openPlugins(product);
    turn(product, "task-hero.plugin.title", false);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(band) == nullptr && product.product().shell().bands().empty(); }));
    // clang-format on
    turn(product, "task-hero.plugin.title", true);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(band) != nullptr && product.product().shell().bands().size() == 1U; }));
    // clang-format on
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    product.stop();
}

// A plugin turned off while its view is on screen leaves the reader on the first destination left, and its view and its settings section are built again once it is back.
TEST_F(PluginLifecycleTest, RebuildsTheViewAndTheSectionOfAPluginTurnedOffWhileOnScreen) {
    TemporaryDirectory data;
    prepare(data, {{"pluginSwitches", {{"flappy-bird", true}}}});
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    const std::string view = "view:flappy-bird:game";
    const std::string section = "settings:flappy-bird:flappy-bird:general";
    openPlugins(product);
    ASSERT_TRUE(product.navigate("flappy-bird:game").hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(view) != nullptr; }));
    // clang-format on

    turn(product, "flappy-bird.plugin.title", false);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !running(product, "flappy-bird") && product.surface(view) == nullptr; }));
    // clang-format on
    EXPECT_NE(product.product().shell().destination(), "flappy-bird:game");

    turn(product, "flappy-bird.plugin.title", true);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return running(product, "flappy-bird"); }));
    // clang-format on
    ASSERT_TRUE(product.navigate("flappy-bird:game").hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(view) != nullptr; }));
    // clang-format on
    ASSERT_TRUE(product.navigate(ui::Shell::settingsDestination).hasValue());
    product.frame();
    product.frame();
    product.press(HeadlessProduct::command() | ImGuiKey_F);
    product.frame();
    product.type("Bird");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(section) != nullptr; }));
    // clang-format on
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    product.stop();
}

// Every switch waits while its plugin starts, a request that arrives all the same waits for the start, the preloaded view asked for meanwhile is built once the start finishes, and the plugin then stops.
TEST_F(PluginLifecycleTest, SerializesASwitchPressedWhileThePluginStarts) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    install(folder.path(), "slow", R"(return { id = "slow", sdk = 6, titleKey = "slow.plugin.title",
        navigation = { { id = "main", titleKey = "slow.plugin.title", icon = "file", placement = "primary", order = 960, preload = true, view = function() return workpane.ui.label({ text = "slow" }) end } },
        start = function() require("async").sleep(300):await() end })");
    prepare(data, {{"pluginFolders", json::array({folder.path().generic_string()})}, {"pluginSwitches", {{"slow", false}}}});

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    openPlugins(product);
    turn(product, "slow.plugin.title", true);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return endsWith(transitions(product, "slow"), {"waiting>starting"}); }));
    // clang-format on
    product.frame();
    EXPECT_FALSE(switchable(product, "slow.plugin.title"));
    EXPECT_FALSE(switchable(product, "logs.plugin.title"));
    turn(product, "slow.plugin.title", false);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return endsWith(transitions(product, "slow"), {"disabled>waiting", "waiting>starting", "starting>running", "running>stopping", "stopping>disabled"}); }));
    // clang-format on
    EXPECT_FALSE(running(product, "slow"));

    turn(product, "slow.plugin.title", true);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface("view:slow:main") != nullptr; }));
    // clang-format on
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    product.stop();
}

// A plugin whose start fails when the reader turns it on is refused and told, and nothing it asked for is built.
TEST_F(PluginLifecycleTest, RefusesAPluginWhoseStartFailsWhenItIsTurnedOn) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    install(folder.path(), "fragile", R"(return { id = "fragile", sdk = 6, titleKey = "fragile.plugin.title",
        navigation = { { id = "main", titleKey = "fragile.plugin.title", icon = "file", placement = "primary", order = 961, preload = true, view = function() return workpane.ui.label({ text = "fragile" }) end } },
        start = function() error("the start failed") end })");
    prepare(data, {{"pluginFolders", json::array({folder.path().generic_string()})}, {"pluginSwitches", {{"fragile", false}}}});

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    openPlugins(product);
    turn(product, "fragile.plugin.title", true);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return endsWith(transitions(product, "fragile"), {"waiting>starting", "starting>refused"}); }));
    // clang-format on
    product.settle(std::chrono::milliseconds(200));
    EXPECT_FALSE(running(product, "fragile"));
    EXPECT_EQ(product.surface("view:fragile:main"), nullptr);
    EXPECT_TRUE(product.product().shell().toasts().showing("A plugin could not be loaded", "The plugin \"fragile\" was not loaded, and its features are unavailable"));
    ASSERT_EQ(errors(product).size(), 1U) << json(errors(product)).dump();
    product.stop();
}

// A plugin whose folder vanished while it was turned off is refused by name when the reader turns it on.
TEST_F(PluginLifecycleTest, RefusesAPluginWhoseFolderVanishedWhileItWasOff) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    install(folder.path(), "vanishing", R"(return { id = "vanishing", sdk = 6, titleKey = "vanishing.plugin.title" })");
    prepare(data, {{"pluginFolders", json::array({folder.path().generic_string()})}, {"pluginSwitches", {{"vanishing", false}}}});

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    openPlugins(product);
    std::filesystem::remove_all(folder.path() / "vanishing");
    turn(product, "vanishing.plugin.title", true);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return endsWith(transitions(product, "vanishing"), {"disabled>refused"}); }));
    // clang-format on
    EXPECT_FALSE(running(product, "vanishing"));
    EXPECT_EQ(errors(product).size(), 1U);
    product.stop();
}

// Turning off a plugin others depend on stops it first so its switch never springs back, stops its dependents, keeps every switch waiting until the change ends and records the plugin once.
TEST_F(PluginLifecycleTest, StopsADependencyWithItsDependentsWhileEverySwitchWaits) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    install(folder.path(), "base", R"(return { id = "base", sdk = 6, titleKey = "base.plugin.title" })");
    install(folder.path(), "child", R"(return { id = "child", sdk = 6, titleKey = "child.plugin.title", dependencies = { "base" }, stop = function() require("async").sleep(400):await() end })");
    prepare(data, {{"pluginFolders", json::array({folder.path().generic_string()})}});

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    openPlugins(product);
    turn(product, "base.plugin.title", false);
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return endsWith(transitions(product, "child"), {"running>stopping"}); }));
    // clang-format on
    product.frame();
    EXPECT_EQ(transitions(product, "base").back(), "running>stopping");
    EXPECT_FALSE(checked(product, "base.plugin.title"));
    EXPECT_FALSE(switchable(product, "base.plugin.title"));
    EXPECT_FALSE(switchable(product, "child.plugin.title"));

    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return endsWith(transitions(product, "base"), {"running>stopping", "stopping>disabled"}) && endsWith(transitions(product, "child"), {"running>stopping", "stopping>waiting"}); }));
    // clang-format on
    product.frame();
    EXPECT_TRUE(switchable(product, "base.plugin.title"));
    EXPECT_TRUE(checked(product, "child.plugin.title"));
    EXPECT_EQ(product.product().preferences().document("workpane")["pluginSwitches"], json({{"base", false}}));
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    product.stop();
}

// A plugin the reader turned off stays off with its switch off when its code no longer reads, and only turning it on tells the failure.
TEST_F(PluginLifecycleTest, KeepsAPluginOffWhenItsCodeNoLongerReads) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    install(folder.path(), "broken", "return { id = ");
    prepare(data, {{"pluginFolders", json::array({folder.path().generic_string()})}, {"pluginSwitches", {{"broken", false}}}});

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    openPlugins(product);
    product.settle(std::chrono::milliseconds(200));
    EXPECT_TRUE(endsWith(transitions(product, "broken"), {"discovered>refused", "refused>disabled"}));
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();

    const SurfaceReader reader(product.declared(installed));
    const auto rows = reader.nodes("settingsRow");
    const auto toggles = reader.nodes("toggle");
    // clang-format off
    const auto row = std::ranges::find_if(rows, [&reader](ui::NodeId id) { return reader.properties(id)["label"] == "broken"; });
    // clang-format on
    ASSERT_NE(row, rows.end());
    const ui::NodeId toggle = toggles[static_cast<std::size_t>(row - rows.begin())];
    EXPECT_FALSE(reader.properties(toggle).value("checked", true));

    product.emit(installed, toggle, "change", {{"checked", true}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return endsWith(transitions(product, "broken"), {"disabled>refused"}) && errors(product).size() == 1U; }));
    // clang-format on
    product.stop();
}

// Closing the product while a plugin stops lets that stop finish and never runs it a second time.
TEST_F(PluginLifecycleTest, StopsEachPluginOnceWhenTheProductClosesDuringAChange) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    install(folder.path(), "lingering", R"(local store
        return { id = "lingering", sdk = 6, titleKey = "lingering.plugin.title",
        start = function() store = workpane.preferences.define({ stops = { type = "integer", default = 0 } }) end,
        stop = function()
            workpane.await(store:set("stops", store:get("stops") + 1))
            require("async").sleep(300):await()
        end })");
    prepare(data, {{"pluginFolders", json::array({folder.path().generic_string()})}});

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    openPlugins(product);
    turn(product, "lingering.plugin.title", false);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return endsWith(transitions(product, "lingering"), {"running>stopping"}); }));
    // clang-format on
    product.stop();
    EXPECT_EQ(product.product().preferences().document("lingering").value("stops", 0), 1);
}

// The data of a plugin is offered for erasing only while the plugin stays off, never while it starts again.
TEST_F(PluginLifecycleTest, OffersToEraseTheDataOfAPluginOnlyWhileItIsOff) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    install(folder.path(), "patient", R"(return { id = "patient", sdk = 6, titleKey = "patient.plugin.title", migrations = { { "CREATE TABLE patient__items(id INTEGER PRIMARY KEY) STRICT" } }, start = function()
        require("async").sleep(300):await()
    end })");
    prepare(data, {{"pluginFolders", json::array({folder.path().generic_string()})}});

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    openPlugins(product);
    // clang-format off
    const auto erasable = [&product]() { return SurfaceReader(product.declared(installed)).properties(*control(product, "patient.plugin.title", "button")).value("visible", false); };
    ASSERT_TRUE(product.frameUntil([&]() { return running(product, "patient"); }));
    // clang-format on
    turn(product, "patient.plugin.title", false);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !running(product, "patient") && erasable(); }));
    // clang-format on

    turn(product, "patient.plugin.title", true);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return endsWith(transitions(product, "patient"), {"waiting>starting"}); }));
    // clang-format on
    product.frame();
    EXPECT_FALSE(erasable());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return running(product, "patient"); }));
    // clang-format on
    product.frame();
    EXPECT_FALSE(erasable());
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    product.stop();
}

// Erasing the data of a plugin that is off and turning it on again starts it afresh, with its tables created again and its preferences back to their defaults.
TEST_F(PluginLifecycleTest, StartsAPluginAfreshAfterItsDataWasErased) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    install(folder.path(), "keeper", R"(return { id = "keeper", sdk = 6, titleKey = "keeper.plugin.title", migrations = { { "CREATE TABLE keeper__items(id INTEGER PRIMARY KEY) STRICT" } }, start = function()
        local store = workpane.preferences.define({ count = { type = "integer", default = 0 } })
        workpane.log.info("keeper", tostring(store:get("count")))
        store:set("count", store:get("count") + 1)
    end })");
    prepare(data, {{"pluginFolders", json::array({folder.path().generic_string()})}});

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().preferences().document("keeper").value("count", 0) == 1; }));
    // clang-format on
    openPlugins(product);
    turn(product, "keeper.plugin.title", false);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !running(product, "keeper") && SurfaceReader(product.declared(installed)).properties(*control(product, "keeper.plugin.title", "button")).value("visible", false); }));
    // clang-format on
    product.emit(installed, *control(product, "keeper.plugin.title", "button"), "click", json::object());
    product.frame();
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().preferences().document("keeper").empty() && product.query("SELECT name FROM sqlite_master WHERE name = 'keeper__items'").empty(); }));
    // clang-format on

    // The second start reads the default count again, and its entry reaches the log a few frames after the start.
    turn(product, "keeper.plugin.title", true);
    const std::string counts = "SELECT message FROM logs__entries WHERE source = 'keeper' AND category = 'keeper' ORDER BY sequence";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().preferences().document("keeper").value("count", 0) == 1 && !product.query("SELECT name FROM sqlite_master WHERE name = 'keeper__items'").empty() && product.query(counts).size() == 2U; }));
    // clang-format on
    const auto counted = product.query(counts);
    EXPECT_EQ(counted[0]["message"], "0");
    EXPECT_EQ(counted[1]["message"], "0");
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    product.stop();
}

TEST_F(PluginLifecycleTest, BacksUpAndResetsAnEnabledPluginWithMismatchedStoredData) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    install(folder.path(), "keeper", R"(return { id = "keeper", sdk = 6, titleKey = "keeper.plugin.title", migrations = { { "CREATE TABLE keeper__items(id INTEGER PRIMARY KEY) STRICT" } } })");
    prepare(data, {{"pluginFolders", json::array({folder.path().generic_string()})}});

    {
        auto database = tests::ProductDatabase::open(data);
        ASSERT_TRUE(database.executeScript("CREATE TABLE keeper__items(id INTEGER PRIMARY KEY, old_column TEXT) STRICT; INSERT INTO core_plugin_schemas(plugin_id, version) VALUES('keeper', 1);").hasValue());
    }

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    openPlugins(product);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return running(product, "keeper") && switchable(product, "keeper.plugin.title"); }));
    // clang-format on
    const auto backups = data.path() / "plugin-data-recovery" / "keeper";
    ASSERT_TRUE(std::filesystem::is_directory(backups));
    const auto backup = std::filesystem::directory_iterator(backups)->path();
    auto saved = persistence::Database::open(backup, true);
    ASSERT_TRUE(saved.hasValue());
    const auto original = saved.value().query("PRAGMA table_info(keeper__items)");
    ASSERT_TRUE(original.hasValue());
    EXPECT_EQ(original.value().size(), 2U);
    const auto columns = product.query("PRAGMA table_info(keeper__items)");
    ASSERT_EQ(columns.size(), 1U);
    EXPECT_EQ(columns.front()["name"], "id");
    product.stop();
}

TEST_F(PluginLifecycleTest, RecoversNewerEditorAndLogsDataBeforeOpeningAProject) {
    TemporaryDirectory data;
    TemporaryDirectory projectFolder;

    {
        HeadlessProduct original(data.path());
        ASSERT_TRUE(original.boot().hasValue());
        original.stop();
    }

    {
        auto database = tests::ProductDatabase::open(data);
        ASSERT_TRUE(database.executeScript("UPDATE core_plugin_schemas SET version = 2 WHERE plugin_id IN ('code-editor', 'logs');").hasValue());
    }

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    openPlugins(product);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return running(product, "code-editor") && running(product, "logs"); }));
    // clang-format on
    for (const std::string plugin : {"code-editor", "logs"}) {
        const auto backups = data.path() / "plugin-data-recovery" / plugin;
        ASSERT_TRUE(std::filesystem::is_directory(backups));
        const auto backup = std::filesystem::directory_iterator(backups)->path();
        auto saved = persistence::Database::open(backup, true);
        ASSERT_TRUE(saved.hasValue());
        const auto old = saved.value().query("SELECT version FROM core_plugin_schemas WHERE plugin_id = ?", {plugin});
        ASSERT_TRUE(old.hasValue());
        EXPECT_EQ(old.value().front()["version"], 2);
        EXPECT_EQ(product.query("SELECT version FROM core_plugin_schemas WHERE plugin_id = ?", {plugin}).front()["version"], 1);
    }

    EXPECT_TRUE(product.navigate("code-editor:editor").hasValue());
    const std::string openFolder = "local path = " + json(projectFolder.path().generic_string()).dump() + R"lua(
        require("workpane.task").run("browser", "suite", function()
            require("workpane.task").await(require("workpane.events").request("browser", "workspace.folder.open", { path = path }))
        end)
    )lua";
    ASSERT_TRUE(product.product().runtime().loadString(openFolder, "recovered-editor-folder").hasValue());
    const std::string canonicalFolder = std::filesystem::canonical(projectFolder.path()).generic_string();
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.query("SELECT root_path FROM code_editor__workspaces WHERE root_path = ?", {canonicalFolder}).empty(); }));
    // clang-format on
    product.stop();
}

TEST_F(PluginLifecycleTest, TriesOneFreshStartWhenAPluginStillRefusesItsPersistedRows) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    install(folder.path(), "keeper", R"(return { id = "keeper", sdk = 6, titleKey = "keeper.plugin.title", migrations = { { "CREATE TABLE keeper__items(id INTEGER PRIMARY KEY) STRICT" } }, start = function() workpane.database.invalid("The stored rows cannot be read") end })");
    prepare(data, {{"pluginFolders", json::array({folder.path().generic_string()})}});
    HeadlessProduct product(data.path());
    product.recordCalls("workpane_plugin_data_recover");
    ASSERT_TRUE(product.boot().hasValue());
    openPlugins(product);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.calls("workpane_plugin_data_recover") == 1U && !running(product, "keeper") && switchable(product, "keeper.plugin.title"); }));
    // clang-format on
    product.settle(std::chrono::milliseconds(50));
    EXPECT_EQ(product.calls("workpane_plugin_data_recover"), 1U);
    const auto backups = data.path() / "plugin-data-recovery" / "keeper";
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(backups), std::filesystem::directory_iterator()), 1);
    product.stop();
}

TEST_F(PluginLifecycleTest, LeavesObsoleteDataOfADisabledPluginUntilItIsEnabled) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    install(folder.path(), "keeper", R"(return { id = "keeper", sdk = 6, titleKey = "keeper.plugin.title", migrations = { { "CREATE TABLE keeper__items(id INTEGER PRIMARY KEY) STRICT" } } })");
    prepare(data, {{"pluginFolders", json::array({folder.path().generic_string()})}, {"pluginSwitches", {{"keeper", false}}}});

    {
        auto database = tests::ProductDatabase::open(data);
        ASSERT_TRUE(database.executeScript("CREATE TABLE keeper__items(id INTEGER PRIMARY KEY, old_column TEXT) STRICT; INSERT INTO keeper__items VALUES(7, 'preserved'); INSERT INTO core_plugin_schemas(plugin_id, version) VALUES('keeper', 1);").hasValue());
    }

    HeadlessProduct product(data.path());
    product.recordCalls("workpane_plugin_data_recover");
    ASSERT_TRUE(product.boot().hasValue());
    openPlugins(product);
    EXPECT_FALSE(running(product, "keeper"));
    EXPECT_EQ(product.calls("workpane_plugin_data_recover"), 0U);
    EXPECT_EQ(product.query("SELECT old_column FROM keeper__items").front()["old_column"], "preserved");
    turn(product, "keeper.plugin.title", true);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return running(product, "keeper") && switchable(product, "keeper.plugin.title"); }));
    // clang-format on
    EXPECT_EQ(product.calls("workpane_plugin_data_recover"), 1U);
    EXPECT_TRUE(product.query("SELECT id FROM keeper__items").empty());
    product.stop();
}

TEST_F(PluginLifecycleTest, PreservesObsoleteDataWhenItsPrivateBackupCannotBeCreated) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    install(folder.path(), "keeper", R"(return { id = "keeper", sdk = 6, titleKey = "keeper.plugin.title", migrations = { { "CREATE TABLE keeper__items(id INTEGER PRIMARY KEY) STRICT" } } })");
    prepare(data, {{"pluginFolders", json::array({folder.path().generic_string()})}});

    {
        auto database = tests::ProductDatabase::open(data);
        ASSERT_TRUE(database.executeScript("CREATE TABLE keeper__items(id INTEGER PRIMARY KEY, old_column TEXT) STRICT; INSERT INTO keeper__items VALUES(7, 'preserved'); INSERT INTO core_plugin_schemas(plugin_id, version) VALUES('keeper', 1);").hasValue());
    }

    std::ofstream(data.path() / "plugin-data-recovery") << "Occupied";
    HeadlessProduct product(data.path());
    product.recordCalls("workpane_plugin_data_recover");
    ASSERT_TRUE(product.boot().hasValue());
    openPlugins(product);
    EXPECT_FALSE(running(product, "keeper"));
    EXPECT_EQ(product.calls("workpane_plugin_data_recover"), 1U);
    EXPECT_EQ(product.query("SELECT old_column FROM keeper__items").front()["old_column"], "preserved");
    EXPECT_TRUE(SurfaceReader(product.declared(installed)).showsKey("workpane.plugins.data-recovery-failed"));
    product.stop();
}

TEST_F(PluginLifecycleTest, PreservesRefusedDataAndControlAvailabilityWhenErasureFails) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    install(folder.path(), "keeper", R"(return { id = "keeper", sdk = 6, titleKey = "keeper.plugin.title", migrations = { { "CREATE TABLE keeper__items(id INTEGER PRIMARY KEY) STRICT" } } })");
    prepare(data, {{"pluginFolders", json::array({folder.path().generic_string()})}});

    {
        auto database = tests::ProductDatabase::open(data);
        ASSERT_TRUE(database.executeScript("CREATE TABLE keeper__items(id INTEGER PRIMARY KEY, old_column TEXT) STRICT; INSERT INTO keeper__items VALUES(7, 'preserved'); CREATE TABLE guarded__references(item INTEGER REFERENCES keeper__items(id) ON DELETE RESTRICT) STRICT; INSERT INTO guarded__references VALUES(7); INSERT INTO core_plugin_schemas(plugin_id, version) VALUES('keeper', 1);").hasValue());
        ASSERT_TRUE(persistence::PreferenceStore::store(database, "keeper", {{"preserved", true}}).hasValue());
    }

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    openPlugins(product);
    const auto button = control(product, "keeper.plugin.title", "button");
    ASSERT_TRUE(button.has_value());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(installed)).properties(*button).value("visible", false); }));
    // clang-format on
    product.recordCalls("workpane_plugin_data_erase");
    product.emit(installed, *button, "click", json::object());
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() {
        const json call = product.lastCall("workpane_plugin_data_erase");
        return call.is_object() && call.value("target", "") == "keeper" && switchable(product, "keeper.plugin.title");
    }));
    // clang-format on

    EXPECT_FALSE(running(product, "keeper"));
    const auto rows = product.query("SELECT old_column FROM keeper__items");
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows.front()["old_column"], "preserved");
    EXPECT_TRUE(product.product().preferences().document("keeper").value("preserved", false));
    EXPECT_TRUE(SurfaceReader(product.declared(installed)).properties(*button).value("enabled", false));
    product.stop();
}

// A plugin the reader turned off stays off across a restart, is discovered and listed, and the product closes without an error while it is off.
TEST_F(PluginLifecycleTest, KeepsAPluginOffAcrossARestart) {
    TemporaryDirectory data;
    {
        HeadlessProduct product(data.path());
        ASSERT_TRUE(product.boot().hasValue());
        openPlugins(product);
        turn(product, "logs.plugin.title", false);
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return !running(product, "logs") && product.product().preferences().document("workpane")["pluginSwitches"] == json({{"logs", false}}); }));
        // clang-format on
        product.stop();
    }

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    product.settle(std::chrono::milliseconds(200));
    EXPECT_FALSE(running(product, "logs"));
    openPlugins(product);
    ASSERT_TRUE(control(product, "logs.plugin.title", "toggle").has_value());
    EXPECT_FALSE(SurfaceReader(product.declared(installed)).properties(*control(product, "logs.plugin.title", "toggle")).value("checked", true));
    product.stop();
}

} // namespace workpane::tests
