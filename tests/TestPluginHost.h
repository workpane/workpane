#pragma once

#include "filesystem/FileSystemService.h"
#include "persistence/StateStore.h"
#include "plugins/PluginInterface.h"
#include "ui/Theme.h"

#include <QPointer>
#include <QSet>

#include <functional>
#include <optional>
#include <utility>

namespace workpane::test {

struct PluginEvent final {
    QString topic;
    QJsonObject payload;
};

struct Notification final {
    QString title;
    QString message;
    plugins::AlertSeverity severity{plugins::AlertSeverity::Information};
};

struct LogMessage final {
    plugins::LogLevel level;
    QString category;
    QString message;
    QJsonObject details;
};

struct ProvidedCapability final {
    QString name;
    int version{1};
};

struct CapabilityInvocation final {
    QString name;
    QJsonObject payload;
    QPointer<QObject> callbackContext;
};

class TestPluginHost final : public plugins::PluginHost {
  public:
    [[nodiscard]] QString translate(const QString& key) const override {
        return translations.value(key, key);
    }

    [[nodiscard]] const ui::Theme& theme() const override {
        return ui::ThemeManager::instance().theme();
    }

    [[nodiscard]] const QString& applicationDataPath() const override {
        return dataPath;
    }

    [[nodiscard]] QJsonObject settings() const override {
        return settingsDocument;
    }

    [[nodiscard]] QFuture<Result<void>> saveSettings(const QJsonObject& document) override {
        savedSettings.append(document);
        return settingsFutureHandler ? settingsFutureHandler(document) : QtFuture::makeReadyValueFuture(Result<void>::success());
    }

    [[nodiscard]] Result<void> migrateDatabase(const QVector<persistence::DatabaseMigration>& migrations) override {
        appliedMigrations += migrations;

        if (migrationHandler) {
            return migrationHandler(migrations);
        }
        if (migrationError.has_value()) {
            return Result<void>::failure(migrationError.value());
        }

        if (!migrations.isEmpty()) {
            schemaVersion = migrations.last().version;
        }

        return Result<void>::success();
    }

    [[nodiscard]] Result<void> executeBootstrapDatabaseTransaction(const QVector<persistence::DatabaseStatement>& statements) override {
        databaseTransactions.append(statements);

        if (transactionHandler) {
            return transactionHandler(statements);
        }
        if (transactionError.has_value()) {
            return Result<void>::failure(transactionError.value());
        }

        return Result<void>::success();
    }

    [[nodiscard]] Result<persistence::DatabaseRows> queryBootstrapDatabase(const QString& statement, const QVariantList& bindings) const override {
        return queryResult(statement, bindings);
    }

    [[nodiscard]] QFuture<Result<void>> executeDatabase(const QString& statement, const QVariantList& bindings) override {
        databaseExecutions.append({{QStringLiteral("statement"), statement}, {QStringLiteral("bindings"), bindings}});

        if (executeHandler) {
            return QtFuture::makeReadyValueFuture(executeHandler(statement, bindings));
        }
        if (executeFutureHandler) {
            return executeFutureHandler(statement, bindings);
        }
        if (executeError.has_value()) {
            return QtFuture::makeReadyValueFuture(Result<void>::failure(executeError.value()));
        }

        return QtFuture::makeReadyValueFuture(Result<void>::success());
    }

    [[nodiscard]] QFuture<Result<void>> executeDatabaseTransaction(const QVector<persistence::DatabaseStatement>& statements) override {
        databaseTransactions.append(statements);

        if (transactionFutureHandler) {
            return transactionFutureHandler(statements);
        }
        if (transactionError.has_value()) {
            return QtFuture::makeReadyValueFuture(Result<void>::failure(transactionError.value()));
        }

        return QtFuture::makeReadyValueFuture(Result<void>::success());
    }

    [[nodiscard]] QFuture<Result<persistence::DatabaseRows>> queryDatabase(const QString& statement, const QVariantList& bindings) override {
        if (queryFutureHandler) {
            return queryFutureHandler(statement, bindings);
        }

        return QtFuture::makeReadyValueFuture(queryResult(statement, bindings));
    }

    // A suite that exercises files binds the real service, because a double answering more than the service answers proves nothing.
    // A statement the real store refuses must not pass here, and a query it answers by real SQL must not be answered by a double that only pretends to.
    void useDatabase(persistence::StateStore& store, const QString& pluginId) {
        // clang-format off
        migrationHandler = [&store, pluginId](const QVector<persistence::DatabaseMigration>& migrations) { return store.migratePluginDatabase(pluginId, migrations); };
        transactionHandler = [&store, pluginId](const QVector<persistence::DatabaseStatement>& statements) { return store.executePluginDatabaseTransaction(pluginId, statements); };
        queryHandler = [&store, pluginId](const QString& statement, const QVariantList& bindings) { return store.queryPluginDatabase(pluginId, statement, bindings); };
        executeHandler = [&store, pluginId](const QString& statement, const QVariantList& bindings) { return store.executePluginDatabase(pluginId, statement, bindings); };
        transactionFutureHandler = [&store, pluginId](const QVector<persistence::DatabaseStatement>& statements) { return QtFuture::makeReadyValueFuture(store.executePluginDatabaseTransaction(pluginId, statements)); };
        executeFutureHandler = [&store, pluginId](const QString& statement, const QVariantList& bindings) { return QtFuture::makeReadyValueFuture(store.executePluginDatabase(pluginId, statement, bindings)); };
        // clang-format on
    }

    void useFileSystem(filesystem::FileSystemService& service) {
        // clang-format off
        readFileHandler = [&service](const QString& path, qint64 maximumBytes) { return service.readFile(path, maximumBytes); };
        listDirectoryHandler = [&service](const QString& path, int maximumEntries) { return service.listDirectory(path, maximumEntries); };
        writeFileHandler = [&service](const QString& path, const QByteArray& content) { return service.writeFile(path, content); };
        pathOperationHandler = [&service](const QString& operation, const QString& source, const QString& destination) {
            if (operation == QStringLiteral("create-file")) {
                return service.createFile(source);
            }
            if (operation == QStringLiteral("create-directory")) {
                return service.createDirectory(source);
            }
            if (operation == QStringLiteral("move")) {
                return service.movePath(source, destination);
            }
            if (operation == QStringLiteral("copy")) {
                return service.copyFile(source, destination);
            }
            if (operation == QStringLiteral("remove-file")) {
                return service.removeFile(source);
            }
            return service.removeDirectory(source);
        };
        // clang-format on
    }

    [[nodiscard]] QFuture<Result<QByteArray>> readFile(const QString& path, qint64 maximumBytes) override {
        return readFileHandler ? readFileHandler(path, maximumBytes) : QtFuture::makeReadyValueFuture(Result<QByteArray>::failure({"test_filesystem_unavailable", "The test filesystem is unavailable", path}));
    }

    [[nodiscard]] QFuture<Result<QVector<filesystem::DirectoryEntry>>> listDirectory(const QString& path, int maximumEntries) override {
        return listDirectoryHandler ? listDirectoryHandler(path, maximumEntries) : QtFuture::makeReadyValueFuture(Result<QVector<filesystem::DirectoryEntry>>::failure({"test_filesystem_unavailable", "The test filesystem is unavailable", path}));
    }

    [[nodiscard]] QFuture<Result<void>> writeFile(const QString& path, const QByteArray& content) override {
        return writeFileHandler ? writeFileHandler(path, content) : QtFuture::makeReadyValueFuture(Result<void>::failure({"test_filesystem_unavailable", "The test filesystem is unavailable", path}));
    }

    [[nodiscard]] QFuture<Result<void>> createFile(const QString& path) override {
        return pathOperationHandler ? pathOperationHandler(QStringLiteral("create-file"), path, {}) : QtFuture::makeReadyValueFuture(Result<void>::failure({"test_filesystem_unavailable", "The test filesystem is unavailable", path}));
    }

    [[nodiscard]] QFuture<Result<void>> createDirectory(const QString& path) override {
        return pathOperationHandler ? pathOperationHandler(QStringLiteral("create-directory"), path, {}) : QtFuture::makeReadyValueFuture(Result<void>::failure({"test_filesystem_unavailable", "The test filesystem is unavailable", path}));
    }

    [[nodiscard]] QFuture<Result<void>> movePath(const QString& sourcePath, const QString& destinationPath) override {
        return pathOperationHandler ? pathOperationHandler(QStringLiteral("move"), sourcePath, destinationPath) : QtFuture::makeReadyValueFuture(Result<void>::failure({"test_filesystem_unavailable", "The test filesystem is unavailable", sourcePath}));
    }

    [[nodiscard]] QFuture<Result<void>> copyFile(const QString& sourcePath, const QString& destinationPath) override {
        return pathOperationHandler ? pathOperationHandler(QStringLiteral("copy"), sourcePath, destinationPath) : QtFuture::makeReadyValueFuture(Result<void>::failure({"test_filesystem_unavailable", "The test filesystem is unavailable", sourcePath}));
    }

    [[nodiscard]] QFuture<Result<void>> removeFile(const QString& path) override {
        return pathOperationHandler ? pathOperationHandler(QStringLiteral("remove-file"), path, {}) : QtFuture::makeReadyValueFuture(Result<void>::failure({"test_filesystem_unavailable", "The test filesystem is unavailable", path}));
    }

    [[nodiscard]] QFuture<Result<void>> removeDirectory(const QString& path) override {
        return pathOperationHandler ? pathOperationHandler(QStringLiteral("remove-directory"), path, {}) : QtFuture::makeReadyValueFuture(Result<void>::failure({"test_filesystem_unavailable", "The test filesystem is unavailable", path}));
    }

    [[nodiscard]] Result<persistence::DatabaseRows> queryResult(const QString& statement, const QVariantList& bindings) const {
        if (queryError.has_value()) {
            return Result<persistence::DatabaseRows>::failure(queryError.value());
        }
        if (queryHandler) {
            return queryHandler(statement, bindings);
        }

        return Result<persistence::DatabaseRows>::success(databaseRows);
    }

    [[nodiscard]] bool confirm(QWidget*, const QString&, const QString&, const QString&, const QString&, bool) const override {
        return confirmation;
    }

    [[nodiscard]] Result<void> provideCapability(const plugins::CapabilityDescriptor& descriptor) override {
        providedCapabilities.append({descriptor.name, descriptor.version});
        availableCapabilities.insert(descriptor.name);
        return Result<void>::success();
    }

    [[nodiscard]] bool capabilityAvailable(const QString& name) const override {
        return availableCapabilities.contains(name);
    }

    [[nodiscard]] QStringList capabilities() const override {
        QStringList names(availableCapabilities.constBegin(), availableCapabilities.constEnd());
        names.sort();
        return names;
    }

    void invokeCapability(const QString& name, const QJsonObject& payload, QObject& callbackContext, plugins::PluginReply reply) override {
        capabilityInvocations.append({name, payload, &callbackContext});

        if (capabilityHandler) {
            capabilityHandler(name, payload, &callbackContext, std::move(reply));
        }
    }

    void publish(const QString& topic, const QJsonObject& payload) override {
        events.append({topic, payload});
    }

    void log(plugins::LogLevel level, const QString& category, const QString& message, const QJsonObject& details) override {
        logs.append({level, category, message, details});
    }

    void notify(const QString& title, const QString& message, plugins::AlertSeverity severity) override {
        notifications.append({title, message, severity});
    }

    void showNavigation(const QString& navigationId) override {
        revealedNavigation.append(navigationId);
    }

    QString dataPath;
    QSet<QString> availableCapabilities;
    QVector<ProvidedCapability> providedCapabilities;
    QVector<CapabilityInvocation> capabilityInvocations;
    QHash<QString, QString> translations;
    QVector<PluginEvent> events;
    QVector<Notification> notifications;
    QStringList revealedNavigation;
    QVector<LogMessage> logs;
    QJsonObject settingsDocument;
    QVector<QJsonObject> savedSettings;
    QVector<persistence::DatabaseMigration> appliedMigrations;
    QVector<QVariantMap> databaseExecutions;
    QVector<QVector<persistence::DatabaseStatement>> databaseTransactions;
    std::function<Result<void>(const QVector<persistence::DatabaseMigration>&)> migrationHandler;
    std::function<Result<void>(const QVector<persistence::DatabaseStatement>&)> transactionHandler;
    std::function<Result<void>(const QString&, const QVariantList&)> executeHandler;
    persistence::DatabaseRows databaseRows;
    std::optional<Error> migrationError;
    std::optional<Error> executeError;
    std::optional<Error> transactionError;
    std::optional<Error> queryError;
    bool confirmation{true};
    int schemaVersion{0};
    std::function<void(const QString&, const QJsonObject&, QObject*, plugins::PluginReply)> capabilityHandler;
    std::function<Result<persistence::DatabaseRows>(const QString&, const QVariantList&)> queryHandler;
    std::function<QFuture<Result<void>>(const QString&, const QVariantList&)> executeFutureHandler;
    std::function<QFuture<Result<void>>(const QVector<persistence::DatabaseStatement>&)> transactionFutureHandler;
    std::function<QFuture<Result<persistence::DatabaseRows>>(const QString&, const QVariantList&)> queryFutureHandler;
    std::function<QFuture<Result<void>>(const QJsonObject&)> settingsFutureHandler;
    std::function<QFuture<Result<QByteArray>>(const QString&, qint64)> readFileHandler;
    std::function<QFuture<Result<QVector<filesystem::DirectoryEntry>>>(const QString&, int)> listDirectoryHandler;
    std::function<QFuture<Result<void>>(const QString&, const QByteArray&)> writeFileHandler;
    std::function<QFuture<Result<void>>(const QString&, const QString&, const QString&)> pathOperationHandler;
};

} // namespace workpane::test
