#pragma once

#include "domain/Result.h"
#include "persistence/PluginDatabase.h"

#include <QFuture>
#include <QJsonObject>
#include <QObject>
#include <QThread>

#include <memory>

namespace workpane::domain {
struct ApplicationSettings;
}

namespace workpane::persistence {

class StateStore;
class DatabaseWorker;

class DatabaseExecutor final : public QObject {
    Q_OBJECT

  public:
    explicit DatabaseExecutor(QString filePath, QObject* parent = nullptr);
    ~DatabaseExecutor() override;

    DatabaseExecutor(const DatabaseExecutor&) = delete;
    DatabaseExecutor& operator=(const DatabaseExecutor&) = delete;

    [[nodiscard]] QFuture<Result<void>> saveSettings(const QString& ownerId, const QJsonObject& document);
    [[nodiscard]] QFuture<Result<void>> exportConfiguration(const QString& destinationPath);
    [[nodiscard]] QFuture<Result<void>> executePluginDatabase(const QString& pluginId, const QString& statement, const QVariantList& bindings);
    [[nodiscard]] QFuture<Result<void>> executePluginDatabaseTransaction(const QString& pluginId, const QVector<DatabaseStatement>& statements);
    [[nodiscard]] QFuture<Result<DatabaseRows>> queryPluginDatabase(const QString& pluginId, const QString& statement, const QVariantList& bindings);

  private:
    QThread m_workerThread;
    std::unique_ptr<DatabaseWorker> m_worker;
};

} // namespace workpane::persistence
