#include "persistence/CoreDatabaseSchema.h"

#include "domain/ApplicationLanguage.h"
#include "persistence/StoredValues.h"

#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>

#include <limits>
#include <utility>

namespace workpane::persistence {

class CoreDatabaseSchemaHelper final {
  public:
    static const QRegularExpression& pluginIdPattern();
    static Error queryError(const QSqlQuery& query);
};

const QRegularExpression& CoreDatabaseSchemaHelper::pluginIdPattern() {
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9]+(?:-[a-z0-9]+)*$"));
    return pattern;
}

Error CoreDatabaseSchemaHelper::queryError(const QSqlQuery& query) {
    return {"database_query_failed", "The core database could not be validated", query.lastError().text()};
}

const QHash<QString, QString>& CoreDatabaseSchemas::coreDatabaseTableSchemas() {
    static const QHash<QString, QString> schemas{{QStringLiteral("plugin_settings"), QStringLiteral("CREATE TABLE plugin_settings(owner_id TEXT PRIMARY KEY NOT NULL, document TEXT NOT NULL) STRICT")}, {QStringLiteral("core_state"), QStringLiteral("CREATE TABLE core_state(singleton INTEGER PRIMARY KEY CHECK(singleton = 1), clean_shutdown INTEGER NOT NULL CHECK(clean_shutdown IN (0, 1))) STRICT")}, {QStringLiteral("plugin_schema_versions"), QStringLiteral("CREATE TABLE plugin_schema_versions(plugin_id TEXT PRIMARY KEY, version INTEGER NOT NULL CHECK(version >= 0)) STRICT")}};
    return schemas;
}

Result<QHash<QString, int>> CoreDatabaseSchemas::validateCoreDatabase(const QSqlDatabase& database) {
    QSqlQuery version(database);

    if (!version.exec(QStringLiteral("PRAGMA user_version")) || !version.next()) {
        return Result<QHash<QString, int>>::failure(CoreDatabaseSchemaHelper::queryError(version));
    }

    qint64 storedCoreVersion = -1;

    if (!StoredValues::readStoredInteger(version.value(0), storedCoreVersion) || storedCoreVersion != coreDatabaseSchemaVersion || version.next()) {
        return Result<QHash<QString, int>>::failure({"database_schema_unsupported", "The application database schema version is unsupported", QString::number(storedCoreVersion)});
    }

    QSqlQuery schema(database);

    if (!schema.exec(QStringLiteral("SELECT name, sql FROM sqlite_schema WHERE type = 'table' AND name IN ('plugin_settings', 'core_state', 'plugin_schema_versions')"))) {
        return Result<QHash<QString, int>>::failure(CoreDatabaseSchemaHelper::queryError(schema));
    }

    QHash<QString, QString> storedSchemas;

    while (schema.next()) {
        storedSchemas.insert(schema.value(0).toString(), schema.value(1).toString().remove(QLatin1Char('"')));
    }

    if (storedSchemas != CoreDatabaseSchemas::coreDatabaseTableSchemas()) {
        return Result<QHash<QString, int>>::failure({"database_schema_invalid", "The core database schema is incomplete", {}});
    }

    QSqlQuery state(database);

    if (!state.exec(QStringLiteral("SELECT singleton, typeof(clean_shutdown), clean_shutdown FROM core_state")) || !state.next()) {
        return Result<QHash<QString, int>>::failure(state.lastError().isValid() ? CoreDatabaseSchemaHelper::queryError(state) : Error{"database_state_invalid", "The application shutdown state is invalid", {}});
    }

    qint64 stateSingleton = 0;
    qint64 cleanShutdown = -1;

    if (!StoredValues::readStoredInteger(state.value(0), stateSingleton) || stateSingleton != 1 || state.value(1).toString() != QStringLiteral("integer") || !StoredValues::readStoredInteger(state.value(2), cleanShutdown) || (cleanShutdown != 0 && cleanShutdown != 1) || state.next()) {
        return Result<QHash<QString, int>>::failure({"database_state_invalid", "The application shutdown state is invalid", {}});
    }

    QSqlQuery settings(database);

    if (!settings.exec(QStringLiteral("SELECT owner_id, typeof(document) FROM plugin_settings"))) {
        return Result<QHash<QString, int>>::failure(CoreDatabaseSchemaHelper::queryError(settings));
    }

    while (settings.next()) {
        if (settings.value(0).toString().isEmpty() || settings.value(1).toString() != QStringLiteral("text")) {
            return Result<QHash<QString, int>>::failure({"database_settings_invalid", "A stored settings document is invalid", settings.value(0).toString()});
        }
    }

    QSqlQuery plugins(database);

    if (!plugins.exec(QStringLiteral("SELECT plugin_id, typeof(version), version FROM plugin_schema_versions"))) {
        return Result<QHash<QString, int>>::failure(CoreDatabaseSchemaHelper::queryError(plugins));
    }

    QHash<QString, int> pluginVersions;

    while (plugins.next()) {
        const QString pluginId = plugins.value(0).toString();
        qint64 pluginVersion = -1;
        if (!CoreDatabaseSchemaHelper::pluginIdPattern().match(pluginId).hasMatch() || pluginVersions.contains(pluginId) || plugins.value(1).toString() != QStringLiteral("integer") || !StoredValues::readStoredInteger(plugins.value(2), pluginVersion) || pluginVersion < 0 || pluginVersion > std::numeric_limits<int>::max()) {
            return Result<QHash<QString, int>>::failure({"database_plugin_schema_invalid", "A plugin database schema version is invalid", pluginId});
        }
        pluginVersions.insert(pluginId, static_cast<int>(pluginVersion));
    }

    return Result<QHash<QString, int>>::success(std::move(pluginVersions));
}

} // namespace workpane::persistence
