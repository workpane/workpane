#pragma once

#include "domain/Result.h"
#include "terminal/ShellProfile.h"

#include <QString>

namespace workpane::terminalcore {

class WindowsShellIntegration final {
  public:
    [[nodiscard]] static Result<void> configure(ShellProfile& profile, const QString& historyFile);
};

} // namespace workpane::terminalcore
