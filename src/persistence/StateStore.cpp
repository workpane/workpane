#include "persistence/StateStore.h"

#include "domain/ApplicationLanguage.h"
#include "persistence/CoreDatabaseSchema.h"
#include "persistence/StoredValues.h"
#include "ui/Theme.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>

#include <utility>

namespace workpane::persistence {

class StateStoreHelper final {
  public:
    static const QRegularExpression& pluginIdPattern();
    static Error databaseError(const QString& code, const QString& message, const QSqlError& error);
    static Result<void> validatePluginStatementOwnership(const QString& pluginId, const QString& statement);
    static QString normalizedDefinition(const QString& statement);
    static Result<QSet<QString>> declaredObjects(const QString& pluginId, const QVector<DatabaseMigration>& migrations);
};

const QRegularExpression& StateStoreHelper::pluginIdPattern() {
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9]+(?:-[a-z0-9]+)*$"));
    return pattern;
}

Error StateStoreHelper::databaseError(const QString& code, const QString& message, const QSqlError& error) {
    return {code, message, error.text()};
}

// SQLite keeps the text an object was created with and quotes the identifiers of one it renamed, so both sides are compared in the same shape.
QString StateStoreHelper::normalizedDefinition(const QString& statement) {
    return QString(statement).remove(QLatin1Char('"')).simplified();
}

// The schema a plugin declares is what its migrations produce rather than the text they are written as, so a table a later version altered is compared against what that alteration really made.
Result<QSet<QString>> StateStoreHelper::declaredObjects(const QString& pluginId, const QVector<DatabaseMigration>& migrations) {
    const QString connectionName = QStringLiteral("workpane-schema-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSet<QString> declared;
    Result<QSet<QString>> outcome = Result<QSet<QString>>::failure({"plugin_database_schema_unreadable", "The declared plugin database schema could not be built", pluginId});
    {
        QSqlDatabase scratch = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        scratch.setDatabaseName(QStringLiteral(":memory:"));

        if (scratch.open()) {
            bool applied = true;

            for (const auto& migration : migrations) {
                for (const auto& statement : migration.statements) {
                    QSqlQuery command(scratch);
                    applied = applied && command.exec(statement);
                }
            }

            QSqlQuery reading(scratch);
            const QString prefix = QString(pluginId).replace(QLatin1Char('-'), QLatin1Char('_')) + QLatin1Char('_');
            reading.prepare(QStringLiteral("SELECT sql FROM sqlite_schema WHERE type IN ('table', 'index') AND sql IS NOT NULL AND substr(name, 1, ?) = ?"));
            reading.addBindValue(prefix.size());
            reading.addBindValue(prefix);

            if (applied && reading.exec()) {
                while (reading.next()) {
                    declared.insert(normalizedDefinition(reading.value(0).toString()));
                }
                outcome = Result<QSet<QString>>::success(declared);
            }

            scratch.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return outcome;
}

Result<void> StateStoreHelper::validatePluginStatementOwnership(const QString& pluginId, const QString& statement) {
    const QString prefix = QString(pluginId).replace(QLatin1Char('-'), QLatin1Char('_')) + QLatin1Char('_');
    const QString command = statement.trimmed().section(QLatin1Char(' '), 0, 0).toUpper();
    const QSet<QString> supportedCommands{QStringLiteral("ALTER"), QStringLiteral("CREATE"), QStringLiteral("DELETE"), QStringLiteral("DROP"), QStringLiteral("INSERT"), QStringLiteral("SELECT"), QStringLiteral("UPDATE")};

    if (!supportedCommands.contains(command)) {
        return Result<void>::failure({"plugin_database_statement_unsupported", "The plugin database statement type is unsupported", command});
    }

    static const QRegularExpression supportedSyntax(QStringLiteral("^(?:ALTER\\s+TABLE\\b|CREATE\\s+(?:TABLE|(?:UNIQUE\\s+)?INDEX)\\b|DELETE\\s+FROM\\b|DROP\\s+(?:TABLE|INDEX)\\b|INSERT\\s+INTO\\b|SELECT\\b|UPDATE\\s+[a-z_])"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression commaSource(QStringLiteral("\\bFROM\\s+[a-z_][a-z0-9_]*(?:\\s+(?:AS\\s+)?(?!INDEXED\\b|NOT\\b|JOIN\\b|LEFT\\b|INNER\\b|CROSS\\b|WHERE\\b|ORDER\\b|GROUP\\b|LIMIT\\b)[a-z_][a-z0-9_]*)?(?:\\s+(?:INDEXED\\s+BY\\s+[a-z_][a-z0-9_]*|NOT\\s+INDEXED))?\\s*,"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression joinedCommaSource(QStringLiteral("\\bJOIN\\b(?:(?!\\b(?:WHERE|ORDER|GROUP|LIMIT|UNION|RETURNING)\\b)[\\s\\S])*?,\\s*[a-z_]"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression parenthesizedSource(QStringLiteral("\\b(?:FROM|JOIN)\\s*\\("), QRegularExpression::CaseInsensitiveOption);

    if (!supportedSyntax.match(statement.trimmed()).hasMatch() || statement.contains(QLatin1Char(';')) || statement.contains(QStringLiteral("--")) || statement.contains(QStringLiteral("/*")) || statement.contains(QStringLiteral("*/")) || statement.contains(QLatin1Char('"')) || statement.contains(QLatin1Char('`')) || statement.contains(QLatin1Char('[')) || statement.contains(QLatin1Char(']')) || commaSource.match(statement).hasMatch() || joinedCommaSource.match(statement).hasMatch() || parenthesizedSource.match(statement).hasMatch()) {
        return Result<void>::failure({"plugin_database_statement_unsupported", "The plugin database statement syntax is unsupported", statement});
    }

    static const QRegularExpression tablePattern(QStringLiteral("\\b(?:TABLE(?:\\s+IF\\s+(?:NOT\\s+)?EXISTS)?|INTO|UPDATE(?!\\s+SET\\b)|FROM|JOIN|REFERENCES)\\s+([a-z_][a-z0-9_]*)"), QRegularExpression::CaseInsensitiveOption);
    bool foundOwnedObject = false;
    auto matches = tablePattern.globalMatch(statement);

    while (matches.hasNext()) {
        const QString table = matches.next().captured(1);
        if (!table.startsWith(prefix)) {
            return Result<void>::failure({"plugin_database_ownership_invalid", "A plugin database statement accesses an object owned by another component", table});
        }
        foundOwnedObject = true;
    }

    static const QRegularExpression indexPattern(QStringLiteral("\\bCREATE\\s+(?:UNIQUE\\s+)?INDEX(?:\\s+IF\\s+NOT\\s+EXISTS)?\\s+([a-z_][a-z0-9_]*)\\s+ON\\s+([a-z_][a-z0-9_]*)"), QRegularExpression::CaseInsensitiveOption);
    const auto indexMatch = indexPattern.match(statement);

    if (indexMatch.hasMatch()) {
        if (!indexMatch.captured(1).startsWith(prefix) || !indexMatch.captured(2).startsWith(prefix)) {
            return Result<void>::failure({"plugin_database_ownership_invalid", "A plugin database index accesses an object owned by another component", indexMatch.captured(1)});
        }
        foundOwnedObject = true;
    }

    static const QRegularExpression referencedIndexPattern(QStringLiteral("\\b(?:DROP\\s+INDEX(?:\\s+IF\\s+EXISTS)?|INDEXED\\s+BY)\\s+([a-z_][a-z0-9_]*)"), QRegularExpression::CaseInsensitiveOption);
    auto referencedIndexes = referencedIndexPattern.globalMatch(statement);

    while (referencedIndexes.hasNext()) {
        const QString index = referencedIndexes.next().captured(1);
        if (!index.startsWith(prefix)) {
            return Result<void>::failure({"plugin_database_ownership_invalid", "A plugin database statement accesses an index owned by another component", index});
        }
        foundOwnedObject = true;
    }

    return foundOwnedObject ? Result<void>::success() : Result<void>::failure(Error{"plugin_database_statement_unsupported", "The plugin database statement does not identify an owned table", statement});
}

StateStore::StateStore(QString filePath) : m_filePath(std::move(filePath)), m_connectionName(QStringLiteral("workpane-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces))) {}

StateStore::~StateStore() {
    const QMutexLocker locker(&m_mutex);

    if (m_database.isValid()) {
        m_database.close();
        m_database = {};
    }

    QSqlDatabase::removeDatabase(m_connectionName);
}

// A database this version cannot use is set aside and a new one takes its place, because nothing stored may keep the application from opening.
Result<void> StateStore::initialize() {
    const QMutexLocker locker(&m_mutex);

    if (m_database.isValid()) {
        return Result<void>::failure({"database_already_initialized", "The application database is already initialized", m_filePath});
    }

    const QFileInfo databaseFile(m_filePath);

    if (m_filePath.isEmpty() || !databaseFile.isAbsolute() || !databaseFile.dir().exists() || (databaseFile.exists() && !databaseFile.isFile())) {
        return Result<void>::failure({"database_path_invalid", "The application database path is invalid", m_filePath});
    }

    if (const auto opened = openDatabase(); opened.hasValue()) {
        if (const auto schema = initializeCoreSchema(); schema.hasValue()) {
            return schema;
        }
    }

    return replaceUnusableDatabase();
}

Result<void> StateStore::openDatabase() {
    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(m_filePath);

    if (!m_database.open()) {
        return Result<void>::failure(StateStoreHelper::databaseError(QStringLiteral("database_open_failed"), QStringLiteral("The application database could not be opened"), m_database.lastError()));
    }

    for (const auto& statement : {QStringLiteral("PRAGMA foreign_keys = ON"), QStringLiteral("PRAGMA busy_timeout = 5000"), QStringLiteral("PRAGMA journal_mode = WAL"), QStringLiteral("PRAGMA synchronous = NORMAL")}) {
        const auto result = execute(statement);
        if (!result.hasValue()) {
            return result;
        }
    }

    return Result<void>::success();
}

// The database that could not be used is kept beside the new one, so nothing it held is destroyed by the recovery.
Result<void> StateStore::replaceUnusableDatabase() {
    if (m_database.isValid()) {
        m_database.close();
        m_database = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
    }

    const QString backup = m_filePath + QStringLiteral(".unusable-") + QString::number(QDateTime::currentSecsSinceEpoch());
    const bool moved = QFileInfo::exists(m_filePath) && QFile::rename(m_filePath, backup);

    if (QFileInfo::exists(m_filePath)) {
        return Result<void>::failure({"database_replace_failed", "The unusable application database could not be set aside", m_filePath});
    }

    // The write-ahead log holds what was committed last, so it goes with the database it belongs to and never stays beside the new one.
    for (const auto& suffix : {QStringLiteral("-wal"), QStringLiteral("-shm")}) {
        const QString sidecar = m_filePath + suffix;
        if (!QFileInfo::exists(sidecar)) {
            continue;
        }
        if (!moved || !QFile::rename(sidecar, backup + suffix)) {
            QFile::remove(sidecar);
        }
    }

    m_replacedDatabasePath = QFileInfo::exists(backup) ? backup : QString{};

    const auto opened = openDatabase();

    if (!opened.hasValue()) {
        return opened;
    }

    return initializeCoreSchema();
}

const QStringList& StateStore::rebuiltSchemas() const {
    return m_rebuiltSchemas;
}

const QString& StateStore::replacedDatabasePath() const {
    return m_replacedDatabasePath;
}

const QString& StateStore::filePath() const {
    return m_filePath;
}

Result<bool> StateStore::wasCleanShutdown() const {
    const QMutexLocker locker(&m_mutex);
    const auto rows = query(QStringLiteral("SELECT clean_shutdown FROM core_state WHERE singleton = 1"));

    if (!rows.hasValue() || rows.value().size() != 1) {
        return Result<bool>::failure(rows.hasValue() ? Error{"database_state_invalid", "The application shutdown state is invalid", {}} : rows.error());
    }

    qint64 clean = -1;

    if (!StoredValues::readStoredInteger(rows.value().first().value(QStringLiteral("clean_shutdown")), clean) || (clean != 0 && clean != 1)) {
        return Result<bool>::failure({"database_state_invalid", "The application shutdown state is invalid", {}});
    }

    return Result<bool>::success(clean == 1);
}

Result<void> StateStore::markShutdown(bool clean) {
    const QMutexLocker locker(&m_mutex);
    return executeSingleRowMutation(QStringLiteral("UPDATE core_state SET clean_shutdown = ? WHERE singleton = 1"), {clean});
}

QJsonObject StateStore::settings(const QString& ownerId) const {
    const QMutexLocker locker(&m_mutex);
    const auto rows = query(QStringLiteral("SELECT document FROM plugin_settings WHERE owner_id = ?"), {ownerId});

    if (!rows.hasValue() || rows.value().size() != 1) {
        return {};
    }

    return QJsonDocument::fromJson(rows.value().first().value(QStringLiteral("document")).toString().toUtf8()).object();
}

Result<void> StateStore::saveSettings(const QString& ownerId, const QJsonObject& document) {
    if (ownerId.isEmpty()) {
        return Result<void>::failure({"database_settings_invalid", "The settings owner is invalid", ownerId});
    }

    const QMutexLocker locker(&m_mutex);
    const QString serialized = QString::fromUtf8(QJsonDocument(document).toJson(QJsonDocument::Compact));
    return execute(QStringLiteral("INSERT INTO plugin_settings(owner_id, document) VALUES(?, ?) ON CONFLICT(owner_id) DO UPDATE SET document = excluded.document"), {ownerId, serialized});
}

Result<int> StateStore::pluginSchemaVersion(const QString& pluginId) const {
    if (!StateStoreHelper::pluginIdPattern().match(pluginId).hasMatch()) {
        return Result<int>::failure({"plugin_database_owner_invalid", "The plugin database owner is invalid", pluginId});
    }

    const QMutexLocker locker(&m_mutex);
    const auto rows = query(QStringLiteral("SELECT version FROM plugin_schema_versions WHERE plugin_id = ?"), {pluginId});

    if (!rows.hasValue()) {
        return Result<int>::failure(rows.error());
    }

    return Result<int>::success(rows.value().isEmpty() ? 0 : rows.value().first().value(QStringLiteral("version")).toInt());
}

// Nothing stored may keep a feature from opening, so a schema this plugin can no longer use is dropped and created again from its own migrations.
Result<void> StateStore::migratePluginDatabase(const QString& pluginId, const QVector<DatabaseMigration>& migrations) {
    if (!StateStoreHelper::pluginIdPattern().match(pluginId).hasMatch() || migrations.isEmpty()) {
        return Result<void>::failure({"plugin_database_migration_invalid", "The plugin database migration plan is invalid", pluginId});
    }

    for (qsizetype index = 0; index < migrations.size(); ++index) {
        if (migrations.at(index).version != index + 1 || migrations.at(index).statements.isEmpty()) {
            return Result<void>::failure({"plugin_database_migration_invalid", "Plugin database migrations must contain the complete consecutive schema history", pluginId});
        }
    }

    const QMutexLocker locker(&m_mutex);

    const auto applied = applyPluginMigrations(pluginId, migrations);

    if (applied.hasValue()) {
        return applied;
    }
    // Only a stored schema this plugin can no longer use is worth rebuilding, because everything else it can fail with costs the reader what they recorded.
    if (applied.error().code != QStringLiteral("plugin_database_schema_invalid")) {
        return applied;
    }

    const auto dropped = dropPluginSchema(pluginId);

    if (!dropped.hasValue()) {
        return dropped;
    }

    const auto rebuilt = applyPluginMigrations(pluginId, migrations);

    if (rebuilt.hasValue() && !m_rebuiltSchemas.contains(pluginId)) {
        m_rebuiltSchemas.append(pluginId);
    }

    return rebuilt;
}

// Only the objects that carry the identifier of the plugin are dropped, so a rebuild never reaches what belongs to somebody else.
Result<void> StateStore::dropPluginSchema(const QString& pluginId) {
    const QString prefix = QString(pluginId).replace(QLatin1Char('-'), QLatin1Char('_')) + QLatin1Char('_');
    const auto rows = query(QStringLiteral("SELECT type, name FROM sqlite_schema WHERE type IN ('table', 'index') AND substr(name, 1, ?) = ?"), {prefix.size(), prefix});

    if (!rows.hasValue()) {
        return Result<void>::failure(rows.error());
    }

    const auto transaction = beginTransaction();

    if (!transaction.hasValue()) {
        return transaction;
    }

    for (const auto& row : rows.value()) {
        const QString name = row.value(QStringLiteral("name")).toString();
        const QString type = row.value(QStringLiteral("type")).toString();
        if (!name.startsWith(prefix) || (type != QStringLiteral("table") && type != QStringLiteral("index"))) {
            rollbackTransaction();
            return Result<void>::failure({"plugin_database_schema_invalid", "A stored plugin database object could not be identified", pluginId});
        }
        const auto dropped = execute(QStringLiteral("DROP %1 IF EXISTS %2").arg(type == QStringLiteral("table") ? QStringLiteral("TABLE") : QStringLiteral("INDEX"), name));
        if (!dropped.hasValue()) {
            rollbackTransaction();
            return dropped;
        }
    }

    const auto forgotten = execute(QStringLiteral("DELETE FROM plugin_schema_versions WHERE plugin_id = ?"), {pluginId});

    if (!forgotten.hasValue()) {
        rollbackTransaction();
        return forgotten;
    }

    return commitTransaction();
}

Result<void> StateStore::applyPluginMigrations(const QString& pluginId, const QVector<DatabaseMigration>& migrations) {
    const auto versionRows = query(QStringLiteral("SELECT version FROM plugin_schema_versions WHERE plugin_id = ?"), {pluginId});

    if (!versionRows.hasValue()) {
        return Result<void>::failure(versionRows.error());
    }

    int currentVersion = versionRows.value().isEmpty() ? 0 : versionRows.value().first().value(QStringLiteral("version")).toInt();

    if (migrations.last().version < currentVersion) {
        return Result<void>::failure({"plugin_database_version_newer", "The plugin database schema is newer than the plugin", pluginId});
    }

    const auto transaction = beginTransaction();

    if (!transaction.hasValue()) {
        return transaction;
    }

    for (const auto& migration : migrations) {
        if (migration.version <= currentVersion) {
            continue;
        }
        if (migration.version != currentVersion + 1 || migration.statements.isEmpty()) {
            rollbackTransaction();
            return Result<void>::failure({"plugin_database_migration_invalid", "Plugin database migrations must be consecutive and non-empty", pluginId});
        }
        for (const auto& statement : migration.statements) {
            const auto ownershipResult = StateStoreHelper::validatePluginStatementOwnership(pluginId, statement);
            if (!ownershipResult.hasValue()) {
                rollbackTransaction();
                return ownershipResult;
            }
            const auto result = execute(statement);
            if (!result.hasValue()) {
                rollbackTransaction();
                return result;
            }
        }
        const auto versionResult = execute(QStringLiteral("INSERT INTO plugin_schema_versions(plugin_id, version) VALUES(?, ?) ON CONFLICT(plugin_id) DO UPDATE SET version = excluded.version"), {pluginId, migration.version});
        if (!versionResult.hasValue()) {
            rollbackTransaction();
            return versionResult;
        }
        currentVersion = migration.version;
    }

    const auto schema = validatePluginSchema(pluginId, migrations);

    if (!schema.hasValue()) {
        rollbackTransaction();
        return schema;
    }

    return commitTransaction();
}

// A schema that changed while its version stayed the same would only be discovered by the first statement that failed, so it is compared here instead.
Result<void> StateStore::validatePluginSchema(const QString& pluginId, const QVector<DatabaseMigration>& migrations) {
    const QString prefix = QString(pluginId).replace(QLatin1Char('-'), QLatin1Char('_')) + QLatin1Char('_');
    const auto rows = query(QStringLiteral("SELECT sql FROM sqlite_schema WHERE type IN ('table', 'index') AND sql IS NOT NULL AND substr(name, 1, ?) = ?"), {prefix.size(), prefix});

    if (!rows.hasValue()) {
        return Result<void>::failure(rows.error());
    }

    QSet<QString> stored;

    for (const auto& row : rows.value()) {
        stored.insert(StateStoreHelper::normalizedDefinition(row.value(QStringLiteral("sql")).toString()));
    }

    const auto declared = StateStoreHelper::declaredObjects(pluginId, migrations);

    if (!declared.hasValue()) {
        return Result<void>::failure(declared.error());
    }
    if (stored != declared.value()) {
        return Result<void>::failure({"plugin_database_schema_invalid", "The stored plugin database schema does not match the one the plugin declares", pluginId});
    }

    return Result<void>::success();
}

Result<void> StateStore::executePluginDatabase(const QString& pluginId, const QString& statement, const QVariantList& bindings) {
    if (!StateStoreHelper::pluginIdPattern().match(pluginId).hasMatch() || statement.trimmed().isEmpty()) {
        return Result<void>::failure({"plugin_database_statement_invalid", "The plugin database statement is invalid", pluginId});
    }

    const auto ownershipResult = StateStoreHelper::validatePluginStatementOwnership(pluginId, statement);

    if (!ownershipResult.hasValue()) {
        return ownershipResult;
    }

    const QMutexLocker locker(&m_mutex);
    return execute(statement, bindings);
}

Result<void> StateStore::executePluginDatabaseTransaction(const QString& pluginId, const QVector<DatabaseStatement>& statements) {
    if (!StateStoreHelper::pluginIdPattern().match(pluginId).hasMatch() || statements.isEmpty()) {
        return Result<void>::failure({"plugin_database_transaction_invalid", "The plugin database transaction is invalid", pluginId});
    }

    for (const auto& statement : statements) {
        if (statement.statement.trimmed().isEmpty()) {
            return Result<void>::failure({"plugin_database_statement_invalid", "A plugin database transaction statement is invalid", pluginId});
        }
        const auto ownershipResult = StateStoreHelper::validatePluginStatementOwnership(pluginId, statement.statement);
        if (!ownershipResult.hasValue()) {
            return ownershipResult;
        }
    }

    const QMutexLocker locker(&m_mutex);
    const auto beginResult = beginTransaction();

    if (!beginResult.hasValue()) {
        return beginResult;
    }

    for (const auto& statement : statements) {
        const auto result = execute(statement.statement, statement.bindings);
        if (!result.hasValue()) {
            rollbackTransaction();
            return result;
        }
    }

    return commitTransaction();
}

Result<DatabaseRows> StateStore::queryPluginDatabase(const QString& pluginId, const QString& statement, const QVariantList& bindings) const {
    if (!StateStoreHelper::pluginIdPattern().match(pluginId).hasMatch() || statement.trimmed().isEmpty()) {
        return Result<DatabaseRows>::failure({"plugin_database_query_invalid", "The plugin database query is invalid", pluginId});
    }

    const auto ownershipResult = StateStoreHelper::validatePluginStatementOwnership(pluginId, statement);

    if (!ownershipResult.hasValue()) {
        return Result<DatabaseRows>::failure(ownershipResult.error());
    }

    const QMutexLocker locker(&m_mutex);
    return query(statement, bindings);
}

Result<void> StateStore::initializeCoreSchema() {
    const auto versionRows = query(QStringLiteral("PRAGMA user_version"));

    if (!versionRows.hasValue() || versionRows.value().size() != 1) {
        return Result<void>::failure(versionRows.hasValue() ? Error{"database_schema_invalid", "The application database schema version is unavailable", {}} : versionRows.error());
    }

    qint64 storedVersion = 0;

    if (!StoredValues::readStoredInteger(versionRows.value().first().value(QStringLiteral("user_version")), storedVersion)) {
        return Result<void>::failure({"database_schema_invalid", "The application database schema version is invalid", {}});
    }

    const int version = static_cast<int>(storedVersion);

    if (version == coreDatabaseSchemaVersion) {
        return validateCoreSchema();
    }
    if (version != 0) {
        return Result<void>::failure({"database_schema_unsupported", "The application database was created by a different schema version and must be removed before starting again", QStringLiteral("version %1 at %2").arg(QString::number(version), m_filePath)});
    }

    const auto transaction = beginTransaction();

    if (!transaction.hasValue()) {
        return transaction;
    }

    const QStringList statements = {CoreDatabaseSchemas::coreDatabaseTableSchemas().value(QStringLiteral("core_state")), CoreDatabaseSchemas::coreDatabaseTableSchemas().value(QStringLiteral("plugin_settings")), CoreDatabaseSchemas::coreDatabaseTableSchemas().value(QStringLiteral("plugin_schema_versions")), QStringLiteral("INSERT INTO core_state(singleton, clean_shutdown) VALUES(1, 1)")};

    for (const auto& statement : statements) {
        const auto result = execute(statement);
        if (!result.hasValue()) {
            rollbackTransaction();
            return result;
        }
    }

    const auto versionResult = execute(QStringLiteral("PRAGMA user_version = %1").arg(coreDatabaseSchemaVersion));

    if (!versionResult.hasValue()) {
        rollbackTransaction();
        return versionResult;
    }

    return commitTransaction();
}

Result<void> StateStore::validateCoreSchema() const {
    const auto result = CoreDatabaseSchemas::validateCoreDatabase(m_database);
    return result.hasValue() ? Result<void>::success() : Result<void>::failure(result.error());
}

Result<void> StateStore::execute(const QString& statement, const QVariantList& bindings) const {
    QSqlQuery sql(m_database);

    if (!sql.prepare(statement)) {
        return Result<void>::failure(StateStoreHelper::databaseError(QStringLiteral("database_prepare_failed"), QStringLiteral("A database statement could not be prepared"), sql.lastError()));
    }

    for (const auto& binding : bindings) {
        sql.addBindValue(binding);
    }

    if (!sql.exec()) {
        return Result<void>::failure(StateStoreHelper::databaseError(QStringLiteral("database_execute_failed"), QStringLiteral("A database statement could not be executed"), sql.lastError()));
    }

    return Result<void>::success();
}

Result<void> StateStore::executeSingleRowMutation(const QString& statement, const QVariantList& bindings) const {
    QSqlQuery sql(m_database);

    if (!sql.prepare(statement)) {
        return Result<void>::failure(StateStoreHelper::databaseError(QStringLiteral("database_prepare_failed"), QStringLiteral("A database statement could not be prepared"), sql.lastError()));
    }

    for (const auto& binding : bindings) {
        sql.addBindValue(binding);
    }

    if (!sql.exec()) {
        return Result<void>::failure(StateStoreHelper::databaseError(QStringLiteral("database_execute_failed"), QStringLiteral("A database statement could not be executed"), sql.lastError()));
    }
    if (sql.numRowsAffected() != 1) {
        return Result<void>::failure({"database_state_invalid", "A core database row is unavailable", statement});
    }

    return Result<void>::success();
}

Result<DatabaseRows> StateStore::query(const QString& statement, const QVariantList& bindings) const {
    QSqlQuery sql(m_database);

    if (!sql.prepare(statement)) {
        return Result<DatabaseRows>::failure(StateStoreHelper::databaseError(QStringLiteral("database_prepare_failed"), QStringLiteral("A database query could not be prepared"), sql.lastError()));
    }

    for (const auto& binding : bindings) {
        sql.addBindValue(binding);
    }

    if (!sql.exec()) {
        return Result<DatabaseRows>::failure(StateStoreHelper::databaseError(QStringLiteral("database_query_failed"), QStringLiteral("A database query could not be executed"), sql.lastError()));
    }

    DatabaseRows rows;
    const QSqlRecord record = sql.record();

    while (sql.next()) {
        QVariantMap row;
        for (int index = 0; index < record.count(); ++index) {
            row.insert(record.fieldName(index), sql.value(index));
        }
        rows.append(std::move(row));
    }

    return Result<DatabaseRows>::success(std::move(rows));
}

Result<void> StateStore::beginTransaction() {
    return m_database.transaction() ? Result<void>::success() : Result<void>::failure(StateStoreHelper::databaseError(QStringLiteral("database_transaction_failed"), QStringLiteral("A database transaction could not be started"), m_database.lastError()));
}

Result<void> StateStore::commitTransaction() {
    if (m_database.commit()) {
        return Result<void>::success();
    }

    const auto error = StateStoreHelper::databaseError(QStringLiteral("database_commit_failed"), QStringLiteral("A database transaction could not be committed"), m_database.lastError());
    m_database.rollback();
    return Result<void>::failure(error);
}

void StateStore::rollbackTransaction() {
    m_database.rollback();
}

} // namespace workpane::persistence
