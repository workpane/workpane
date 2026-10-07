#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace workpane::scripting {

// What the product tells Lua about itself when it starts, with the folders its plugins are loaded from, the bundled one first.
struct ApplicationInfo final {
    std::string version;
    bool debug{false};
    std::string platform;
    std::string architecture;
    std::filesystem::path resources;
    std::vector<std::filesystem::path> plugins;
    std::filesystem::path data;
};

} // namespace workpane::scripting
