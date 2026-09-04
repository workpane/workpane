#pragma once

#include "domain/Result.h"
#include "terminal/ShellProfile.h"

#include <QProcessEnvironment>
#include <QString>

namespace workpane::terminalcore {

class PosixShellIntegration final {
  public:
    [[nodiscard]] static Result<void> configure(const ShellProfile& profile, const QString& historyFile, QProcessEnvironment& environment);
};

} // namespace workpane::terminalcore
