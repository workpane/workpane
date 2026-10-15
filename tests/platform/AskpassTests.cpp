#include "platform/NativeProcesses.h"
#include "process/ProcessEvents.h"
#include "process/ProcessLaunch.h"
#include "support/LocalPort.h"
#include "support/TemporaryDirectory.h"

#include <gtest/gtest.h>
#include <httplib.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace workpane::platform {

class AskpassTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // clang-format off
        m_server.Post("/credential", [this](const httplib::Request& request, httplib::Response& response) {
            const std::lock_guard lock(m_mutex);
            ++m_requests;
            m_authenticated = request.get_header_value("Authorization") == "Bearer " + m_token;
            m_prompt = request.body;
            m_kind = request.get_header_value("X-Workpane-Prompt");
            response.status = m_status;
            response.set_header("Location", "/credential");
            response.set_content(m_answer, "text/plain");
        });
        // clang-format on

        const int port = tests::LocalPort::bind(m_server);
        ASSERT_GT(port, 0);
        m_variables = {{"WORKPANE_ASKPASS_PORT", std::to_string(port)}, {"WORKPANE_ASKPASS_TOKEN", m_token}};
        // clang-format off
        m_thread = std::thread([this]() { std::ignore = m_server.listen_after_bind(); });
        // clang-format on
    }

    void TearDown() override {
        if (m_process != nullptr) {
            m_process->stop();
            m_process.reset();
        }

        m_server.stop();

        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    void run(const std::vector<std::string>& arguments = {"Password:"}) {
        process::ProcessLaunch launch{WORKPANE_TEST_ASKPASS, arguments, m_data.path(), m_variables, {"SSH_ASKPASS_PROMPT"}, ""};
        // clang-format off
        process::ProcessEvents events{
            [this](process::ProcessStream stream, std::string bytes) {
                const std::lock_guard lock(m_mutex);
                (stream == process::ProcessStream::Output ? m_output : m_error) += bytes;
            },
            [this](process::ProcessEnd ended) {
                const std::lock_guard lock(m_mutex);
                m_exit = ended;
                m_finished.notify_one();
            },
        };
        // clang-format on

        auto started = m_launcher.start(launch, std::move(events));
        ASSERT_TRUE(started.hasValue());
        m_process = std::move(started.value());
        std::unique_lock lock(m_mutex);
        // clang-format off
        ASSERT_TRUE(m_finished.wait_for(lock, std::chrono::seconds(10), [this]() { return m_exit.has_value(); }));
        // clang-format on
    }

    void answer(std::string value, int status = 200) {
        const std::lock_guard lock(m_mutex);
        m_answer = std::move(value);
        m_status = status;
    }

    tests::TemporaryDirectory m_data;
    NativeProcesses m_launcher;
    httplib::Server m_server;
    std::thread m_thread;
    std::unique_ptr<process::Process> m_process;
    std::mutex m_mutex;
    std::condition_variable m_finished;
    std::vector<std::pair<std::string, std::string>> m_variables;
    std::optional<process::ProcessEnd> m_exit;
    std::string m_output;
    std::string m_error;
    std::string m_prompt;
    std::string m_kind;
    std::string m_answer{"private test credential"};
    const std::string m_token = std::string(64, 'a');
    int m_status{200};
    int m_requests{0};
    bool m_authenticated{false};
};

TEST_F(AskpassTest, ReturnsTheCredentialOnlyThroughItsStandardOutput) {
    run();
    ASSERT_TRUE(m_exit.has_value());
    EXPECT_EQ(m_exit->code, 0);
    EXPECT_FALSE(m_exit->crashed);
    EXPECT_EQ(m_output, m_answer + "\n");
    EXPECT_TRUE(m_error.empty());
    EXPECT_TRUE(m_authenticated);
    EXPECT_EQ(m_prompt, "Password:");
    EXPECT_EQ(m_kind, "input");
}

TEST_F(AskpassTest, ReturnsAnExplicitHostConfirmation) {
    m_variables.emplace_back("SSH_ASKPASS_PROMPT", "confirm");
    answer("yes");
    run({"Trust this host fingerprint?"});
    ASSERT_TRUE(m_exit.has_value());
    EXPECT_EQ(m_exit->code, 0);
    EXPECT_EQ(m_output, "yes\n");
    EXPECT_EQ(m_kind, "confirm");
}

TEST_F(AskpassTest, RefusesMalformedCredentialsWithoutContactingTheServer) {
    m_variables[1].second = "not-a-session-token";
    run();
    ASSERT_TRUE(m_exit.has_value());
    EXPECT_EQ(m_exit->code, 1);
    EXPECT_EQ(m_requests, 0);
    EXPECT_TRUE(m_output.empty());
    EXPECT_TRUE(m_error.empty());
}

TEST_F(AskpassTest, RefusesAnOversizedEnvironmentHintWithoutCrashingOrRequestingCredentials) {
    m_variables.emplace_back("SSH_ASKPASS_PROMPT", std::string(129, 'x'));
    run();
    ASSERT_TRUE(m_exit.has_value());
    EXPECT_EQ(m_exit->code, 1);
    EXPECT_FALSE(m_exit->crashed);
    EXPECT_EQ(m_requests, 0);
    EXPECT_TRUE(m_output.empty());
    EXPECT_TRUE(m_error.empty());
}

TEST_F(AskpassTest, RefusesAnOversizedAnswerWithoutPrintingItsContent) {
    answer(std::string(1024, 'x'));
    run();
    ASSERT_TRUE(m_exit.has_value());
    EXPECT_EQ(m_exit->code, 1);
    EXPECT_TRUE(m_output.empty());
    EXPECT_TRUE(m_error.empty());
}

TEST_F(AskpassTest, RefusesAnAnswerContainingAnotherLine) {
    answer("credential\nprivate diagnostic");
    run();
    ASSERT_TRUE(m_exit.has_value());
    EXPECT_EQ(m_exit->code, 1);
    EXPECT_TRUE(m_output.empty());
    EXPECT_TRUE(m_error.empty());
}

TEST_F(AskpassTest, RefusesRedirectsWithoutSendingCredentialsToAnotherAddress) {
    answer("private test credential", 302);
    run();
    ASSERT_TRUE(m_exit.has_value());
    EXPECT_EQ(m_exit->code, 1);
    EXPECT_EQ(m_requests, 1);
    EXPECT_TRUE(m_output.empty());
    EXPECT_TRUE(m_error.empty());
}

} // namespace workpane::platform
