#pragma once

#include <QProcessEnvironment>

namespace workpane::terminalcore {

// Both pseudo-terminal backends start their shell from this environment, so neither platform can describe the terminal differently.
class TerminalEnvironment final {
  public:
    [[nodiscard]] static QProcessEnvironment sessionEnvironment();
};

} // namespace workpane::terminalcore
