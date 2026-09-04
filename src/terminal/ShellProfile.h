#pragma once

#include <QString>
#include <QStringList>

namespace workpane::terminalcore {

struct ShellProfile final {
    QString id;
    QString name;
    QString executable;
    QStringList arguments;
};

class ShellProfileResolver final {
  public:
    [[nodiscard]] static ShellProfile systemDefault();
    [[nodiscard]] static QList<ShellProfile> availableProfiles();
};

class ShellPaths final {
  public:
    [[nodiscard]] static QString formatLocalPathsForShell(const ShellProfile& profile, const QStringList& paths);
};

} // namespace workpane::terminalcore
