#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace workpane::ui {

// What a program told the terminal since it was last asked, apart from what it drew.
struct TerminalSignals final {
    bool bell{false};
    std::vector<std::pair<std::string, std::string>> notifications;
    std::optional<std::string> title;
    std::optional<std::string> directory;
    std::optional<std::string> clipboard;
};

} // namespace workpane::ui
