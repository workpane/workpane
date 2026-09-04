#pragma once

#include "domain/Result.h"

#include <QFuture>
#include <QHash>
#include <QObject>
#include <QString>

namespace workpane::persistence {
class DatabaseExecutor;
}

namespace workpane::app {

class ConfigurationManager final : public QObject {
    Q_OBJECT

  public:
    ConfigurationManager(persistence::DatabaseExecutor& databaseExecutor, QString pendingImportPath, QHash<QString, int> pluginSchemaVersions, QObject* parent = nullptr);

    [[nodiscard]] QFuture<Result<void>> exportConfiguration(const QString& destinationPath);
    [[nodiscard]] QFuture<Result<void>> importConfiguration(const QString& sourcePath);
    void requestRestart();

  signals:
    void restartRequested();
    void transferStateChanged(bool active);

  private:
    persistence::DatabaseExecutor& m_databaseExecutor;
    QString m_pendingImportPath;
    QHash<QString, int> m_pluginSchemaVersions;
    bool m_transferActive{false};
};

} // namespace workpane::app
