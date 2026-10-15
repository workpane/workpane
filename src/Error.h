#pragma once

#include <string>

namespace workpane {

struct Error final {
    std::string code;
    std::string message;
    std::string detail;
};

} // namespace workpane
