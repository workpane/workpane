#pragma once

#include <nlohmann/json.hpp>

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace httplib {
class Server;
}

namespace workpane::tests {

// An AI service on the loopback interface that speaks the OpenAI-compatible protocol, lists its models, streams the turns a test scripts and records every request.
// A turn is streamed text, the tools the model calls, or a status other than 200 with its body.
class FakeProvider final {
  public:
    struct Turn final {
        std::string text;
        nlohmann::json toolCalls = nlohmann::json::array();
        std::string finishReason{"stop"};
        int status{200};
        std::string failure;
    };

    FakeProvider();
    ~FakeProvider();

    [[nodiscard]] static Turn answer(std::string text, std::string finishReason = "stop");
    [[nodiscard]] static Turn calls(nlohmann::json toolCalls);
    [[nodiscard]] static Turn refusal(int status, std::string failure);
    FakeProvider(const FakeProvider&) = delete;
    FakeProvider& operator=(const FakeProvider&) = delete;

    [[nodiscard]] std::string address() const;
    void script(Turn turn);
    [[nodiscard]] std::vector<nlohmann::json> requests() const;
    [[nodiscard]] std::vector<std::string> authorizations() const;

  private:
    [[nodiscard]] static std::string events(const Turn& turn);

    std::unique_ptr<httplib::Server> m_server;
    std::thread m_thread;
    int m_port{0};
    mutable std::mutex m_mutex;
    std::deque<Turn> m_turns;
    std::vector<nlohmann::json> m_requests;
    std::vector<std::string> m_authorizations;
};

} // namespace workpane::tests
