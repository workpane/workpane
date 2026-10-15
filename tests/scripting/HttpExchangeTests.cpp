#include "support/LocalPort.h"
#include "support/ScriptHarness.h"
#include "support/TemporaryDirectory.h"

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>

namespace workpane::tests {

class HttpExchangeTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // clang-format off
        const auto completed = [](const httplib::Request&, httplib::Response& response) {
            response.set_content(std::string("first\0second\n", 13), "application/octet-stream");
        };

        const auto blocked = [this](const httplib::Request& request, httplib::Response&) {
            m_entered = true;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

            while (!m_stopping && std::chrono::steady_clock::now() < deadline) {
                if (request.is_connection_closed()) {
                    m_disconnected = true;
                    return;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        };

        const auto streamed = [this](const httplib::Request&, httplib::Response& response) {
            const auto content = [this](std::size_t, std::size_t, httplib::DataSink& sink) {
                if (!sink.write("first\n", 6)) {
                    return false;
                }

                m_entered = true;
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

                while (!m_stopping && std::chrono::steady_clock::now() < deadline) {
                    if (!sink.is_writable()) {
                        m_disconnected = true;
                        return false;
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }

                return false;
            };

            response.set_content_provider(12, "text/plain", content);
        };
        // clang-format on

        m_server.Get("/complete", completed);
        m_server.Get("/blocked", blocked);
        m_server.Get("/stream", streamed);
        m_port = LocalPort::bind(m_server);
        ASSERT_GT(m_port, 0);
        // clang-format off
        m_thread = std::thread([this]() { std::ignore = m_server.listen_after_bind(); });
        // clang-format on
        m_server.wait_until_ready();
    }

    void TearDown() override {
        m_stopping = true;
        m_server.stop();

        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    [[nodiscard]] std::string url(std::string_view path) const {
        return "http://127.0.0.1:" + std::to_string(m_port) + std::string(path);
    }

    [[nodiscard]] bool entered(ScriptHarness& harness) const {
        // clang-format off
        return harness.pollUntil([this]() { return m_entered.load(); });
        // clang-format on
    }

    [[nodiscard]] bool disconnected(ScriptHarness& harness) const {
        // clang-format off
        return harness.pollUntil([this]() { return m_disconnected.load(); });
        // clang-format on
    }

  private:
    httplib::Server m_server;
    std::thread m_thread;
    std::atomic<bool> m_stopping{false};
    std::atomic<bool> m_entered{false};
    std::atomic<bool> m_disconnected{false};
    int m_port{0};
};

TEST_F(HttpExchangeTest, CompletesBufferedAndStreamingExchangesWithBinaryBodies) {
    ScriptHarness harness;
    const std::string source = R"lua(
        local http = require("http")
        local task = require("workpane.task")
        task.run("test", "test", function()
            local buffered = http.client.exchange({ url = URL, maxResponseBytes = 32 })
            local response, failure = buffered.promise:await()
            buffered:cancel()
            buffered:cancel()
            local chunks = {}
            local status
            local streamed = http.client.exchange({ url = URL, onResponse = function(value) status = value end }, function(chunk)
                chunks[#chunks + 1] = chunk
            end)
            local _, streamFailure = streamed.promise:await()
            streamed:cancel()
            host.test_result({ buffered = failure == nil and response.status == 200 and response.body == "first\0second\n", streamed = streamFailure == nil and status == 200 and table.concat(chunks) == "first\0second\n" })
        end)
    )lua";
    ASSERT_TRUE(harness.run("local URL = " + nlohmann::json(url("/complete")).dump() + "\n" + source).hasValue());
    // clang-format off
    ASSERT_TRUE(harness.pollUntil([&harness]() { return !harness.results.empty(); }));
    // clang-format on
    EXPECT_EQ(harness.results.front()["buffered"], true);
    EXPECT_EQ(harness.results.front()["streamed"], true);
    EXPECT_TRUE(harness.logged.empty());
}

TEST_F(HttpExchangeTest, CancelsABlockedResponseAndSettlesItsPromiseOnce) {
    ScriptHarness harness;
    const std::string source = R"lua(
        exchange = require("http").client.exchange({ url = URL, timeoutSeconds = 30 })
        require("workpane.task").run("test", "test", function()
            local value, failure = exchange.promise:await()
            local repeated, repeatedFailure = exchange.promise:await()
            host.test_result({ cancelled = value == nil and failure ~= nil, repeated = repeated == nil and repeatedFailure == failure })
        end)
    )lua";
    ASSERT_TRUE(harness.run("local URL = " + nlohmann::json(url("/blocked")).dump() + "\n" + source).hasValue());
    ASSERT_TRUE(entered(harness));
    const auto started = std::chrono::steady_clock::now();
    ASSERT_TRUE(harness.run("exchange:cancel()\nexchange:cancel()").hasValue());
    ASSERT_TRUE(disconnected(harness));
    // clang-format off
    ASSERT_TRUE(harness.pollUntil([&harness]() { return !harness.results.empty(); }));
    // clang-format on
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(3));
    EXPECT_EQ(harness.results.front()["cancelled"], true);
    EXPECT_EQ(harness.results.front()["repeated"], true);
    EXPECT_TRUE(harness.logged.empty());
}

TEST_F(HttpExchangeTest, InterruptsAnActiveStreamBeforeAnotherChunkArrives) {
    ScriptHarness harness;
    const std::string source = R"lua(
        local count = 0
        exchange = require("http").client.exchange({ url = URL, timeoutSeconds = 30 }, function(chunk)
            count = count + 1
            host.test_result({ chunk = chunk })
        end)
        require("workpane.task").run("test", "test", function()
            local _, failure = exchange.promise:await()
            host.test_result({ cancelled = failure ~= nil, chunks = count })
        end)
    )lua";
    ASSERT_TRUE(harness.run("local URL = " + nlohmann::json(url("/stream")).dump() + "\n" + source).hasValue());
    // clang-format off
    ASSERT_TRUE(harness.pollUntil([&harness]() { return !harness.results.empty(); }));
    // clang-format on
    ASSERT_EQ(harness.results.front()["chunk"], "first\n");
    ASSERT_TRUE(harness.run("exchange:cancel()").hasValue());
    ASSERT_TRUE(disconnected(harness));
    // clang-format off
    ASSERT_TRUE(harness.pollUntil([&harness]() { return harness.results.size() == 2U; }));
    // clang-format on
    EXPECT_EQ(harness.results.back()["cancelled"], true);
    EXPECT_EQ(harness.results.back()["chunks"], 1);
    EXPECT_TRUE(harness.logged.empty());
}

TEST_F(HttpExchangeTest, CancelsADiscardedOwnerWhileItsPromiseIsStillAwaited) {
    ScriptHarness harness;
    const std::string source = R"lua(
        exchange = require("http").client.exchange({ url = URL, timeoutSeconds = 30 })
        local promise = exchange.promise
        require("workpane.task").run("test", "test", function()
            local _, failure = promise:await()
            host.test_result({ cancelled = failure ~= nil })
        end)
    )lua";
    ASSERT_TRUE(harness.run("local URL = " + nlohmann::json(url("/blocked")).dump() + "\n" + source).hasValue());
    ASSERT_TRUE(entered(harness));
    ASSERT_TRUE(harness.run("exchange = nil\ncollectgarbage('collect')").hasValue());
    ASSERT_TRUE(disconnected(harness));
    // clang-format off
    ASSERT_TRUE(harness.pollUntil([&harness]() { return !harness.results.empty(); }));
    // clang-format on
    EXPECT_EQ(harness.results.front()["cancelled"], true);
    EXPECT_TRUE(harness.logged.empty());
}

TEST_F(HttpExchangeTest, RefusesInvalidTimeoutsCallbacksAndResponseBounds) {
    ScriptHarness harness;
    const std::string source = R"lua(
        local http = require("http")
        local invalidTimeout = pcall(http.client.exchange, { url = URL, timeoutSeconds = 0 })
        local invalidCallback = pcall(http.client.exchange, { url = URL }, "callback")
        require("workpane.task").run("test", "test", function()
            local bounded = http.client.exchange({ url = URL, maxResponseBytes = 2 })
            local _, failure = bounded.promise:await()
            host.test_result({ timeout = not invalidTimeout, callback = not invalidCallback, bounded = failure ~= nil })
        end)
    )lua";
    ASSERT_TRUE(harness.run("local URL = " + nlohmann::json(url("/complete")).dump() + "\n" + source).hasValue());
    // clang-format off
    ASSERT_TRUE(harness.pollUntil([&harness]() { return !harness.results.empty(); }));
    // clang-format on
    EXPECT_EQ(harness.results.front()["timeout"], true);
    EXPECT_EQ(harness.results.front()["callback"], true);
    EXPECT_EQ(harness.results.front()["bounded"], true);
    EXPECT_TRUE(harness.logged.empty());
}

TEST_F(HttpExchangeTest, ReachesOwnedExchangesThroughAnIsolatedPluginModule) {
    TemporaryDirectory folder;
    const auto plugin = folder.path() / "sample";
    std::filesystem::create_directories(plugin);
    std::ofstream(plugin / "translations.lua") << "return { en = {} }\n";
    std::ofstream(plugin / "plugin.lua") << "return { id = 'sample', sdk = 6, request = function(url) return require('http').client.exchange({url = url}) end }\n";
    ScriptHarness harness;
    const std::string source = R"lua(
        require("workpane.task").run("test", "test", function()
            local candidates = require("workpane.loader").discover({ FOLDER }, { paths = { data = "" }, languages = { "en" }, themes = {}, icons = {}, colors = {} })
            local exchange = candidates[1].candidate.definition.request(URL)
            local response, failure = exchange.promise:await()
            exchange:cancel()
            host.test_result({ answered = failure == nil and response.status == 200 and response.body == "first\0second\n" })
        end)
    )lua";
    ASSERT_TRUE(harness.run("local FOLDER = " + nlohmann::json(folder.path().generic_string()).dump() + "\nlocal URL = " + nlohmann::json(url("/complete")).dump() + "\n" + source).hasValue());
    // clang-format off
    ASSERT_TRUE(harness.pollUntil([&harness]() { return !harness.results.empty() || !harness.logged.empty(); }));
    // clang-format on
    ASSERT_TRUE(harness.logged.empty()) << nlohmann::json(harness.logged).dump();
    ASSERT_EQ(harness.results.size(), 1U);
    EXPECT_EQ(harness.results.front()["answered"], true);
}

} // namespace workpane::tests
