#pragma once

#include "ui/TerminalLaunch.h"

#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace workpane::tests {

// One shell a test started, whose output the test writes and whose input, sizes and end the test reads.
struct TerminalProcessRecord final {
    ui::TerminalLaunch launch;
    std::mutex mutex;
    std::string output;
    std::string input;
    std::vector<std::pair<int, int>> sizes;
    std::optional<int> exit;
    std::string directory;
    bool alive{true};
    bool refusesInput{false};
};

} // namespace workpane::tests
