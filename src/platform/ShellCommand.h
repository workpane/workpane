#pragma once

#include <string>
#include <vector>

namespace workpane::platform {

struct ShellCommand final {
    std::string program;
    std::vector<std::string> arguments;
    std::string name;
};

} // namespace workpane::platform
