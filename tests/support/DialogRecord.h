#pragma once

#include "support/RootedPath.h"

#include <string>
#include <vector>

namespace workpane::tests {

struct DialogRecord final {
    std::vector<std::string> requests;
    std::vector<std::string> notifications;
    std::vector<std::string> paths{RootedPath::of("tmp/workpane-test/chosen.lua").generic_string()};
    std::string button{"ok"};
};

} // namespace workpane::tests
