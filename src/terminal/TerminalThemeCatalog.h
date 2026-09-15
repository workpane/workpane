#pragma once

#include "domain/TerminalTheme.h"

#include <QString>
#include <QVector>

namespace workpane::terminalcore {

// A theme is resolved from an identifier the caller already validated, and an unknown one is answered rather than thrown.
class TerminalThemes final {
  public:
    [[nodiscard]] static const QVector<domain::TerminalTheme>& terminalThemes();
    [[nodiscard]] static const domain::TerminalTheme* terminalTheme(const QString& id);
    [[nodiscard]] static bool terminalThemeExists(const QString& id);
};

} // namespace workpane::terminalcore
