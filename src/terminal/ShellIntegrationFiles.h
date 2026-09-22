#pragma once

#include "domain/Result.h"

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>

namespace workpane::terminalcore {

// Every platform integration keeps its startup files beside the history of the terminal, so both of them report the same conditions by the same names.
class ShellIntegrationFiles final {
  public:
    [[nodiscard]] static Result<QString> write(const QString& directory, const QString& name, const QList<QPair<QString, QByteArray>>& files);
};

} // namespace workpane::terminalcore
