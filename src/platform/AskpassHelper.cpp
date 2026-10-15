#include "platform/AskpassHelper.h"

#include <httplib.h>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <system_error>

namespace workpane::platform {

Result<std::string> AskpassHelper::refused() {
    return Result<std::string>::failure({"askpass_refused", "The credential request could not be completed", {}});
}

Result<std::string> AskpassHelper::request(int port, std::string_view token, std::string_view prompt, std::string_view kind) {
    // clang-format off
    const bool validToken = token.size() == 64 && std::ranges::all_of(token, [](char character) { return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'); });
    // clang-format on

    if (port < 1 || port > 65535 || !validToken || prompt.empty() || prompt.size() > maximumPrompt || prompt.find('\0') != std::string_view::npos || (kind != "input" && kind != "confirm" && kind != "none")) {
        return refused();
    }

    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(5);
    client.set_read_timeout(120);
    client.set_write_timeout(5);
    client.set_follow_location(false);
    std::string answer;
    // clang-format off
    const auto receive = [&answer](const char* bytes, std::size_t length) {
        if (length > maximumAnswer - answer.size()) {
            return false;
        }

        answer.append(bytes, length);

        return true;
    };
    // clang-format on

    const httplib::Headers headers{{"Authorization", "Bearer " + std::string(token)}, {"X-Workpane-Prompt", std::string(kind)}};
    const auto response = client.Post("/credential", headers, std::string(prompt), "text/plain; charset=utf-8", receive);

    if (!response || response->status != 200 || answer.find_first_of(std::string("\0\r\n", 3)) != std::string::npos || (kind == "confirm" && answer != "yes" && answer != "no")) {
        return refused();
    }

    return Result<std::string>::success(std::move(answer));
}

Result<std::string> AskpassHelper::environment(const char* name) {
#if defined(_WIN32)
    std::array<char, maximumEnvironment + 1> value{};
    std::size_t length = 0;

    if (getenv_s(&length, value.data(), value.size(), name) != 0 || length > value.size()) {
        return refused();
    }

    return Result<std::string>::success(length == 0 ? std::string{} : std::string(value.data(), length - 1));
#else
    const char* value = std::getenv(name);
    const std::string_view text = value == nullptr ? std::string_view{} : std::string_view(value);

    if (text.size() > maximumEnvironment) {
        return refused();
    }

    return Result<std::string>::success(std::string(text));
#endif
}

int AskpassHelper::run(int argc, char** argv) {
    const auto portText = environment("WORKPANE_ASKPASS_PORT");
    const auto token = environment("WORKPANE_ASKPASS_TOKEN");
    const auto hint = environment("SSH_ASKPASS_PROMPT");

    if (argc != 2 || !portText.hasValue() || !token.hasValue() || !hint.hasValue() || portText.value().empty() || token.value().empty()) {
        return 1;
    }

    const std::string_view portString(portText.value());
    int port = 0;
    const auto parsed = std::from_chars(portString.data(), portString.data() + portString.size(), port);

    if (parsed.ec != std::errc{} || parsed.ptr != portString.data() + portString.size()) {
        return 1;
    }

    const auto answer = request(port, token.value(), argv[1], hint.value().empty() ? "input" : hint.value());

    if (!answer.hasValue()) {
        return 1;
    }

#if defined(_WIN32)
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
        return 1;
    }
#endif

    return std::fwrite(answer.value().data(), 1, answer.value().size(), stdout) == answer.value().size() && std::fputc('\n', stdout) != EOF && std::fflush(stdout) == 0 ? 0 : 1;
}

} // namespace workpane::platform
