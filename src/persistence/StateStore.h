#pragma once

#include "domain/Result.h"
#include "persistence/PluginDatabase.h"

#include <QJsonObject>
#include <QMutex>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVector>

namespace workpane::persistence {

class StateStore final {
  public:
    explicit StateStore(QString filePath);
    ~StateStore();

    [[nodiscard]] Result<void> initialize();
    [[nodiscard]] const QString& filePath() const;
    // Nothing stored may keep the application from opening, so what could not be used is recorded rather than refused.
    [[nodiscard]] const QStringList& rebuiltSchemas() const;
    [[nodiscard]] const QString& replacedDatabasePath() const;
    [[nodiscard]] Result<bool> wasCleanShutdown() const;
    [[nodiscard]] QJsonObject settings(const QString& ownerId) const;
    [[nodiscard]] Result<void> saveSettings(const QString& ownerId, const QJsonObject& document);

    [[nodiscard]] Result<void> markShutdown(bool clean);
    [[nodiscard]] Result<int> pluginSchemaVersion(const QString& pluginId) const;
    [[nodiscard]] Result<void> migratePluginDatabase(const QString& pluginId, const QVector<DatabaseMigration>& migrations);
    [[nodiscard]] Result<void> executePluginDatabase(const QString& pluginId, const QString& statement, const QVariantList& bindings);
    [[nodiscard]] Result<void> executePluginDatabaseTransaction(const QString& pluginId, const QVector<DatabaseStatement>& statements);
    [[nodiscard]] Result<DatabaseRows> queryPluginDatabase(const QString& pluginId, const QString& statement, const QVariantList& bindings) const;

  private:
    [[nodiscard]] Result<void> validatePluginSchema(const QString& pluginId, const QVector<DatabaseMigration>& migrations);
    [[nodiscard]] Result<void> applyPluginMigrations(const QString& pluginId, const QVector<DatabaseMigration>& migrations);
    [[nodiscard]] Result<void> dropPluginSchema(const QString& pluginId);
    [[nodiscard]] Result<void> initializeCoreSchema();
    [[nodiscard]] Result<void> validateCoreSchema() const;
    [[nodiscard]] Result<void> execute(const QString& statement, const QVariantList& bindings = {}) const;
    [[nodiscard]] Result<void> executeSingleRowMutation(const QString& statement, const QVariantList& bindings) const;
    [[nodiscard]] Result<DatabaseRows> query(const QString& statement, const QVariantList& bindings = {}) const;
    [[nodiscard]] Result<void> beginTransaction();
    [[nodiscard]] Result<void> commitTransaction();
    void rollbackTransaction();

    [[nodiscard]] Result<void> openDatabase();
    [[nodiscard]] Result<void> replaceUnusableDatabase();

    QString m_filePath;
    QString m_connectionName;
    QStringList m_rebuiltSchemas;
    QString m_replacedDatabasePath;
    mutable QMutex m_mutex;
    QSqlDatabase m_database;
};

} // namespace workpane::persistence
