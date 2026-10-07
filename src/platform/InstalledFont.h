#pragma once

#include <filesystem>
#include <string>

namespace workpane::platform {

// A font family installed on the machine, named as the reader knows it, with the file of its regular face.
struct InstalledFont final {
    std::string family;
    std::filesystem::path file;
};

} // namespace workpane::platform
