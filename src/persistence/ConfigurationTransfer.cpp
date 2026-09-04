#include "persistence/ConfigurationTransfer.h"

#include "persistence/CoreDatabaseSchema.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryFile>
#include <QUuid>
#include <QtConcurrentRun>

namespace workpane::persistence {

class ConfigurationTransferHelper final {
  public:
    static Result<void> copyAtomically(const QString& sourcePath, const QString& destinationPath);
    static Result<void> replaceDatabase(const QString& sourcePath, const QString& destinationPath);
    static QString fileIdentity(const QString& filePath);
    static Result<void> validateDatabase(const QString& filePath, const QHash<QString, int>& pluginSchemaVersions);
    static Result<void> createSnapshot(const QString& databasePath, const QString& destinationPath);
};

Result<void> ConfigurationTransferHelper::copyAtomically(const QString& sourcePath, const QString& destinationPath) {
    QFile source(sourcePath);

    if (!source.open(QIODevice::ReadOnly)) {
        return Result<void>::failure({"configuration_source_open_failed", "The configuration source could not be opened", source.errorString()});
    }

    QSaveFile destination(destinationPath);

    if (!destination.open(QIODevice::WriteOnly)) {
        return Result<void>::failure({"configuration_destination_open_failed", "The configuration destination could not be opened", destination.errorString()});
    }

    constexpr qint64 chunkSize = 1024LL * 1024;

    while (!source.atEnd()) {
        const QByteArray chunk = source.read(chunkSize);
        if (chunk.isEmpty() && source.error() != QFileDevice::NoError) {
            destination.cancelWriting();
            return Result<void>::failure({"configuration_source_read_failed", "The configuration source could not be read", source.errorString()});
        }
        if (destination.write(chunk) != chunk.size()) {
            destination.cancelWriting();
            return Result<void>::failure({"configuration_destination_write_failed", "The configuration destination could not be written", destination.errorString()});
        }
    }

    return destination.commit() ? Result<void>::success() : Result<void>::failure(Error{"configuration_commit_failed", "The configuration file could not be committed", destination.errorString()});
}

// A database is replaced together with the log that belongs to it, because a log left beside its replacement is replayed over what arrived.
Result<void> ConfigurationTransferHelper::replaceDatabase(const QString& sourcePath, const QString& destinationPath) {
    const auto copied = copyAtomically(sourcePath, destinationPath);

    if (!copied.hasValue()) {
        return copied;
    }

    for (const auto& suffix : {QStringLiteral("-wal"), QStringLiteral("-shm")}) {
        const QString sidecar = destinationPath + suffix;
        if (QFileInfo::exists(sidecar) && !QFile::remove(sidecar)) {
            return Result<void>::failure({"configuration_log_remove_failed", "The log of the replaced configuration database could not be removed", sidecar});
        }
    }

    return Result<void>::success();
}

QString ConfigurationTransferHelper::fileIdentity(const QString& filePath) {
    const QFileInfo file(filePath);
    return file.exists() ? file.canonicalFilePath() : file.absoluteFilePath();
}

Result<void> ConfigurationTransferHelper::validateDatabase(const QString& filePath, const QHash<QString, int>& pluginSchemaVersions) {
    const QFileInfo file(filePath);

    if (!file.isAbsolute() || !file.isFile() || !file.isReadable()) {
        return Result<void>::failure({"configuration_file_invalid", "The configuration file is invalid", filePath});
    }

    const QString connectionName = QStringLiteral("workpane-configuration-validation-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    database.setDatabaseName(filePath);

    if (!database.open()) {
        const QString detail = database.lastError().text();
        database = {};
        QSqlDatabase::removeDatabase(connectionName);
        return Result<void>::failure({"configuration_database_open_failed", "The configuration database could not be opened", detail});
    }

    bool integrityValid = false;
    Result<QHash<QString, int>> coreValidation = Result<QHash<QString, int>>::failure({"configuration_database_invalid", "The configuration database is invalid or incompatible", filePath});
    {
        QSqlQuery integrity(database);
        integrityValid = integrity.exec(QStringLiteral("PRAGMA quick_check")) && integrity.next() && integrity.value(0).toString() == QStringLiteral("ok") && !integrity.next();
        coreValidation = CoreDatabaseSchemas::validateCoreDatabase(database);
    }
    database.close();
    database = {};
    QSqlDatabase::removeDatabase(connectionName);

    if (!integrityValid || !coreValidation.hasValue() || coreValidation.value() != pluginSchemaVersions) {
        return Result<void>::failure({"configuration_database_invalid", "The configuration database is invalid or incompatible", filePath});
    }

    return Result<void>::success();
}

Result<void> ConfigurationTransferHelper::createSnapshot(const QString& databasePath, const QString& destinationPath) {
    const QFileInfo destination(destinationPath);

    if (!destination.isAbsolute() || !destination.dir().exists()) {
        return Result<void>::failure({"configuration_destination_invalid", "The configuration destination is invalid", destinationPath});
    }

    const QString sourceIdentity = fileIdentity(databasePath);
    const QString destinationIdentity = fileIdentity(destinationPath);

    if (!sourceIdentity.isEmpty() && sourceIdentity == destinationIdentity) {
        return Result<void>::failure({"configuration_destination_conflict", "The active configuration database cannot be used as the export destination", destinationPath});
    }

    QTemporaryFile snapshot(destination.dir().filePath(QStringLiteral(".workpane-export-XXXXXX.sqlite3")));

    if (!snapshot.open()) {
        return Result<void>::failure({"configuration_snapshot_create_failed", "The configuration snapshot could not be created", snapshot.errorString()});
    }

    const QString snapshotPath = snapshot.fileName();
    snapshot.close();
    QFile::remove(snapshotPath);

    const QString connectionName = QStringLiteral("workpane-configuration-export-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    database.setDatabaseName(databasePath);

    if (!database.open()) {
        const QString detail = database.lastError().text();
        database = {};
        QSqlDatabase::removeDatabase(connectionName);
        return Result<void>::failure({"configuration_database_open_failed", "The application configuration could not be opened", detail});
    }

    bool exported = false;
    QString exportError;
    {
        QSqlQuery query(database);
        query.prepare(QStringLiteral("VACUUM INTO ?"));
        query.addBindValue(snapshotPath);
        exported = query.exec();
        exportError = query.lastError().text();
    }
    database.close();
    database = {};
    QSqlDatabase::removeDatabase(connectionName);

    if (!exported) {
        return Result<void>::failure({"configuration_export_failed", "The application configuration could not be exported", exportError});
    }

    const QString snapshotConnectionName = QStringLiteral("workpane-configuration-snapshot-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSqlDatabase snapshotDatabase = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), snapshotConnectionName);
    snapshotDatabase.setDatabaseName(snapshotPath);
    bool normalized = snapshotDatabase.open();
    QString normalizationError = snapshotDatabase.lastError().text();

    if (normalized) {
        QSqlQuery query(snapshotDatabase);
        normalized = query.exec(QStringLiteral("UPDATE core_state SET clean_shutdown = 1 WHERE singleton = 1")) && query.numRowsAffected() == 1;
        normalizationError = query.lastError().text();
    }

    snapshotDatabase.close();
    snapshotDatabase = {};
    QSqlDatabase::removeDatabase(snapshotConnectionName);

    if (!normalized) {
        return Result<void>::failure({"configuration_snapshot_invalid", "The configuration snapshot could not be finalized", normalizationError});
    }

    return copyAtomically(snapshotPath, destinationPath);
}

QFuture<Result<void>> ConfigurationTransfer::exportDatabase(const QString& databasePath, const QString& destinationPath) {
    // clang-format off
    return QtConcurrent::run([databasePath, destinationPath]() { return exportDatabaseNow(databasePath, destinationPath); });
    // clang-format on
}

Result<void> ConfigurationTransfer::exportDatabaseNow(const QString& databasePath, const QString& destinationPath) {
    return ConfigurationTransferHelper::createSnapshot(databasePath, destinationPath);
}

QFuture<Result<void>> ConfigurationTransfer::stageImport(const QString& sourcePath, const QString& pendingPath, const QHash<QString, int>& pluginSchemaVersions) {
    // clang-format off
    return QtConcurrent::run([sourcePath, pendingPath, pluginSchemaVersions]() {
        if (ConfigurationTransferHelper::fileIdentity(sourcePath) == ConfigurationTransferHelper::fileIdentity(pendingPath)) {
            return Result<void>::failure({"configuration_import_conflict", "The configuration import source conflicts with the staging file", sourcePath});
        }
        const auto validation = ConfigurationTransferHelper::validateDatabase(sourcePath, pluginSchemaVersions);
        if (!validation.hasValue()) {
            return validation;
        }
        return ConfigurationTransferHelper::createSnapshot(sourcePath, pendingPath);
    });
    // clang-format on
}

Result<bool> ConfigurationTransfer::beginPendingImport(const QString& databasePath, const QString& pendingPath, const QString& backupPath, const QHash<QString, int>& pluginSchemaVersions) {
    if (QFileInfo::exists(backupPath)) {
        const auto backupValidation = ConfigurationTransferHelper::validateDatabase(backupPath, pluginSchemaVersions);
        if (!backupValidation.hasValue()) {
            return Result<bool>::failure(backupValidation.error());
        }
        const auto recovery = ConfigurationTransferHelper::replaceDatabase(backupPath, databasePath);
        if (!recovery.hasValue()) {
            return Result<bool>::failure(recovery.error());
        }
        if (QFileInfo::exists(pendingPath) && !QFile::remove(pendingPath)) {
            return Result<bool>::failure({"configuration_pending_remove_failed", "The interrupted configuration staging file could not be removed", pendingPath});
        }
        if (!QFile::remove(backupPath)) {
            return Result<bool>::failure({"configuration_backup_remove_failed", "The interrupted configuration backup could not be removed", backupPath});
        }
        return Result<bool>::success(false);
    }

    if (!QFileInfo::exists(pendingPath)) {
        return Result<bool>::success(false);
    }
    if (ConfigurationTransferHelper::fileIdentity(databasePath) == ConfigurationTransferHelper::fileIdentity(pendingPath) || ConfigurationTransferHelper::fileIdentity(databasePath) == ConfigurationTransferHelper::fileIdentity(backupPath) || ConfigurationTransferHelper::fileIdentity(pendingPath) == ConfigurationTransferHelper::fileIdentity(backupPath)) {
        return Result<bool>::failure({"configuration_import_conflict", "The configuration import paths conflict", pendingPath});
    }

    const auto validation = ConfigurationTransferHelper::validateDatabase(pendingPath, pluginSchemaVersions);

    if (!validation.hasValue()) {
        return Result<bool>::failure(validation.error());
    }

    const auto backup = ConfigurationTransferHelper::createSnapshot(databasePath, backupPath);

    if (!backup.hasValue()) {
        return Result<bool>::failure(backup.error());
    }

    // A replacement can fail after the database it wrote is already in place, so the backup stays where the next start recovers from it.
    const auto result = ConfigurationTransferHelper::replaceDatabase(pendingPath, databasePath);

    if (!result.hasValue()) {
        return Result<bool>::failure(result.error());
    }

    return Result<bool>::success(true);
}

Result<void> ConfigurationTransfer::finalizePendingImport(const QString& pendingPath, const QString& backupPath) {
    if (QFileInfo::exists(pendingPath) && !QFile::remove(pendingPath)) {
        return Result<void>::failure({"configuration_pending_remove_failed", "The imported configuration staging file could not be removed", pendingPath});
    }
    if (!QFileInfo::exists(backupPath) || !QFile::remove(backupPath)) {
        return Result<void>::failure({"configuration_backup_remove_failed", "The configuration backup could not be removed", backupPath});
    }

    return Result<void>::success();
}

Result<void> ConfigurationTransfer::rollbackPendingImport(const QString& databasePath, const QString& pendingPath, const QString& backupPath) {
    if (!QFileInfo::exists(backupPath)) {
        return Result<void>::failure({"configuration_backup_missing", "The configuration backup is unavailable", backupPath});
    }

    const auto restore = ConfigurationTransferHelper::replaceDatabase(backupPath, databasePath);

    if (!restore.hasValue()) {
        return restore;
    }
    if (QFileInfo::exists(pendingPath) && !QFile::remove(pendingPath)) {
        return Result<void>::failure({"configuration_pending_remove_failed", "The rejected configuration staging file could not be removed", pendingPath});
    }
    if (!QFile::remove(backupPath)) {
        return Result<void>::failure({"configuration_backup_remove_failed", "The configuration backup could not be removed", backupPath});
    }

    return Result<void>::success();
}

} // namespace workpane::persistence
