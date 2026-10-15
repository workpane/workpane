#include "ui/components/terminal/TerminalStream.h"

#include <algorithm>
#include <utility>

namespace workpane::ui {

Result<void> TerminalStream::append(std::string_view bytes, std::string directory) {
    if (m_code.has_value()) {
        return Result<void>::failure({"terminal_stream_ended", "The terminal stream has ended", ""});
    }

    if (bytes.size() > maximumPending - m_output.size()) {
        return Result<void>::failure({"terminal_stream_full", "The terminal stream is waiting for its output to be drawn", ""});
    }

    m_output.append(bytes);

    if (!directory.empty()) {
        m_directory = std::move(directory);
    }

    return Result<void>::success();
}

Result<void> TerminalStream::finish(int code) {
    if (m_code.has_value()) {
        return Result<void>::failure({"terminal_stream_ended", "The terminal stream has ended", ""});
    }

    m_code = code;

    return Result<void>::success();
}

Result<void> TerminalStream::acknowledge(std::size_t bytes) {
    if (bytes > m_input) {
        return Result<void>::failure({"terminal_stream_ack_invalid", "The terminal input acknowledgement exceeds its pending bytes", ""});
    }

    m_input -= bytes;

    return Result<void>::success();
}

std::string TerminalStream::takeOutput(std::size_t limit) {
    const auto count = std::min(limit, m_output.size());
    std::string taken = m_output.substr(0, count);
    m_output.erase(0, count);

    return taken;
}

std::optional<int> TerminalStream::exitCode() const {
    return m_output.empty() ? m_code : std::nullopt;
}

bool TerminalStream::write(std::string_view bytes) {
    if (m_code.has_value() || bytes.size() > maximumPending - m_input) {
        return false;
    }

    m_input += bytes.size();

    return true;
}

void TerminalStream::resize(int, int) {}

std::string TerminalStream::directory() const {
    return m_directory;
}

} // namespace workpane::ui
