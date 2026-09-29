#pragma once

#include <string>
#include <vector>

namespace workpane::platform {

struct FileFilter final {
    std::string name;
    std::vector<std::string> patterns;
};

} // namespace workpane::platform
