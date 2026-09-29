#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace workpane::app {

struct CommandLineOptions final {
    std::optional<std::filesystem::path> dataDirectory;
    bool help{false};
    bool version{false};

    [[nodiscard]] std::vector<std::string> arguments() const;
};

} // namespace workpane::app
