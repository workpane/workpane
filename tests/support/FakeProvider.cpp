#include "support/FakeProvider.h"
#include "support/LocalPort.h"

#include <httplib.h>

#include <cstddef>
#include <string>
#include <tuple>
#include <utility>

namespace workpane::tests {

using nlohmann::json;

FakeProvider::FakeProvider() : m_server(std::make_unique<httplib::Server>()) {
    // clang-format off
    m_server->Get("/v1/models", [](const httplib::Request&, httplib::Response& response) {
        response.set_content(json{{"object", "list"}, {"data", json::array({{{"id", "fake-large"}}, {{"id", "fake-small"}}})}}.dump(), "application/json");
    });
    // clang-format on

    // clang-format off
    m_server->Post("/v1/chat/completions", [this](const httplib::Request& request, httplib::Response& response) {
        Turn turn;
        bool scripted = false;

        {
            const std::lock_guard lock(m_mutex);
            m_requests.push_back(json::parse(request.body, nullptr, false));
            m_authorizations.push_back(request.get_header_value("Authorization"));

            if (!m_turns.empty()) {
                turn = std::move(m_turns.front());
                m_turns.pop_front();
                scripted = true;
            }
        }

        if (!scripted) {
            response.status = 500;
            response.set_content(R"({"error":{"type":"test","message":"No turn was scripted"}})", "application/json");
            return;
        }

        if (turn.status != 200) {
            response.status = turn.status;
            response.set_content(turn.failure, "application/json");
            return;
        }

        response.set_content(events(turn), "text/event-stream");
    });
    // clang-format on

    m_port = LocalPort::bind(*m_server);
    // clang-format off
    m_thread = std::thread([this]() { std::ignore = m_server->listen_after_bind(); });
    // clang-format on
    m_server->wait_until_ready();
}

FakeProvider::~FakeProvider() {
    m_server->stop();
    m_thread.join();
}

FakeProvider::Turn FakeProvider::answer(std::string text, std::string finishReason) {
    return Turn{std::move(text), json::array(), std::move(finishReason), 200, std::string()};
}

FakeProvider::Turn FakeProvider::calls(json toolCalls) {
    return Turn{std::string(), std::move(toolCalls), "tool_calls", 200, std::string()};
}

FakeProvider::Turn FakeProvider::refusal(int status, std::string failure) {
    return Turn{std::string(), json::array(), std::string(), status, std::move(failure)};
}

std::string FakeProvider::address() const {
    return "http://127.0.0.1:" + std::to_string(m_port) + "/v1";
}

void FakeProvider::script(Turn turn) {
    const std::lock_guard lock(m_mutex);
    m_turns.push_back(std::move(turn));
}

std::vector<json> FakeProvider::requests() const {
    const std::lock_guard lock(m_mutex);
    return m_requests;
}

std::vector<std::string> FakeProvider::authorizations() const {
    const std::lock_guard lock(m_mutex);
    return m_authorizations;
}

// The text arrives word by word, each tool call in one delta, then the reason the turn ended, the usage and the closing marker.
std::string FakeProvider::events(const Turn& turn) {
    std::string stream;
    std::size_t start = 0;

    while (start < turn.text.size()) {
        const std::size_t space = turn.text.find(' ', start);
        const std::size_t end = space == std::string::npos ? turn.text.size() : space + 1;
        stream += "data: " + json{{"choices", json::array({{{"index", 0}, {"delta", {{"content", turn.text.substr(start, end - start)}}}}})}}.dump() + "\n\n";
        start = end;
    }

    for (std::size_t index = 0; index < turn.toolCalls.size(); ++index) {
        const json& call = turn.toolCalls[index];
        const json delta = {{"tool_calls", json::array({{{"index", index}, {"id", call["id"]}, {"type", "function"}, {"function", {{"name", call["name"]}, {"arguments", call["arguments"].dump()}}}}})}};
        stream += "data: " + json{{"choices", json::array({{{"index", 0}, {"delta", delta}}})}}.dump() + "\n\n";
    }

    stream += "data: " + json{{"choices", json::array({{{"index", 0}, {"delta", json::object()}, {"finish_reason", turn.finishReason}}})}}.dump() + "\n\n";
    stream += "data: " + json{{"choices", json::array()}, {"usage", {{"prompt_tokens", 120}, {"completion_tokens", 30}}}}.dump() + "\n\n";
    stream += "data: [DONE]\n\n";

    return stream;
}

} // namespace workpane::tests
