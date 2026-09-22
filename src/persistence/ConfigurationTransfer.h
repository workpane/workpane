#pragma once

#include "domain/Result.h"

#include <QFuture>
#include <QHash>
#include <QString>

namespace workpane::persistence {

class ConfigurationTransfer final {
  public:
    [[nodiscard]] static Result<void> exportDatabaseNow(const QString& databasePath, const QString& destinationPath);
    [[nodiscard]] static QFuture<Result<void>> exportDatabase(const QString& databasePath, const QString& destinationPath);
    [[nodiscard]] static QFuture<Result<void>> stageImport(const QString& sourcePath, const QString& pendingPath, const QHash<QString, int>& pluginSchemaVersions);
    [[nodiscard]] static Result<bool> beginPendingImport(const QString& databasePath, const QString& pendingPath, const QString& backupPath, const QHash<QString, int>& pluginSchemaVersions);
    [[nodiscard]] static Result<void> finalizePendingImport(const QString& pendingPath, const QString& backupPath);
    [[nodiscard]] static Result<void> rollbackPendingImport(const QString& databasePath, const QString& pendingPath, const QString& backupPath);
};

} // namespace workpane::persistence
