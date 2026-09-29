#pragma once

#include "support/TerminalProcessRecord.h"

#include <memory>
#include <string>
#include <vector>

namespace workpane::tests {

struct TerminalRecord final {
    std::vector<std::shared_ptr<TerminalProcessRecord>> processes;
    std::string refusal;
};

} // namespace workpane::tests
