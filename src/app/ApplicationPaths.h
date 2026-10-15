#pragma once

#include <filesystem>

namespace workpane::app {

struct ApplicationPaths final {
    std::filesystem::path executable;
    std::filesystem::path resources;
    std::filesystem::path data;

    [[nodiscard]] std::filesystem::path lua() const;
    [[nodiscard]] std::filesystem::path plugins() const;
    [[nodiscard]] std::filesystem::path fonts() const;
};

} // namespace workpane::app
