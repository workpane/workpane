#pragma once

#include "domain/Result.h"
#include "plugins/CapabilityRegistry.h"
#include "plugins/LocalizationService.h"
#include "plugins/PluginInterface.h"

#include <QObject>

#include <memory>
#include <optional>
#include <vector>

class QPluginLoader;

namespace workpane::persistence {
class DatabaseExecutor;
class StateStore;
} // namespace workpane::persistence

namespace workpane::filesystem {
class FileSystemService;
} // namespace workpane::filesystem

namespace workpane::ui {
class Theme;
}

namespace workpane::plugins {

class ScopedPluginHost;
struct PendingPluginRequest;

struct PluginNavigationItem final {
    QString pluginId;
    NavigationItem item;
};

struct PluginSettingsContribution final {
    QString pluginId;
    SettingsGroup group;
};

class PluginManager final : public QObject {
    Q_OBJECT

  public:
    explicit PluginManager(QObject* parent = nullptr);
    ~PluginManager() override;

    [[nodiscard]] Result<void> loadPlugins();
    [[nodiscard]] Result<void> initialize(QString applicationDataPath, persistence::StateStore& stateStore, persistence::DatabaseExecutor& databaseExecutor);
    void shutdown();
    void unloadPlugins();

    [[nodiscard]] QVector<PluginNavigationItem> navigationItems() const;
    [[nodiscard]] QVector<PluginSettingsContribution> settings() const;
    [[nodiscard]] QHash<QString, int> databaseSchemaVersions() const;
    [[nodiscard]] QWidget* createNavigationView(const QString& pluginId, const QString& itemId, QWidget* parent) const;
    [[nodiscard]] QWidget* createSettingsSection(const QString& pluginId, const QString& groupId, const QString& sectionId, QWidget* parent) const;
    [[nodiscard]] QString pluginTitle(const QString& pluginId) const;
    [[nodiscard]] QString styleSheet() const;
    void setTheme(const ui::Theme& theme);
    [[nodiscard]] const ui::Theme& theme() const;

    [[nodiscard]] Result<void> setLocale(const QString& localeName);
    [[nodiscard]] const QString& localeName() const;
    [[nodiscard]] QString translate(const QString& key) const;
    void notify(const QString& title, const QString& message, AlertSeverity severity);

  signals:
    void notificationRequested(const QString& title, const QString& message, AlertSeverity severity);
    void navigationRequested(const QString& pluginId, const QString& navigationId);

  private:
    friend class ScopedPluginHost;

    [[nodiscard]] Result<void> registerPlugin(PluginInterface& plugin);
    [[nodiscard]] PluginInterface* plugin(const QString& pluginId) const;
    [[nodiscard]] PluginHost* hostFor(const QString& pluginId) const;
    void showNavigation(const QString& pluginId, const QString& navigationId);
    [[nodiscard]] const QString& applicationDataPath() const;
    [[nodiscard]] QJsonObject settings(const QString& ownerId) const;
    [[nodiscard]] QFuture<Result<void>> saveSettings(const QString& ownerId, const QJsonObject& document);
    [[nodiscard]] Result<void> migrateDatabase(const QString& pluginId, const QVector<persistence::DatabaseMigration>& migrations);
    [[nodiscard]] Result<void> executeBootstrapDatabaseTransaction(const QString& pluginId, const QVector<persistence::DatabaseStatement>& statements);
    [[nodiscard]] Result<persistence::DatabaseRows> queryBootstrapDatabase(const QString& pluginId, const QString& statement, const QVariantList& bindings) const;
    [[nodiscard]] QFuture<Result<void>> executeDatabase(const QString& pluginId, const QString& statement, const QVariantList& bindings);
    [[nodiscard]] QFuture<Result<void>> executeDatabaseTransaction(const QString& pluginId, const QVector<persistence::DatabaseStatement>& statements);
    [[nodiscard]] QFuture<Result<persistence::DatabaseRows>> queryDatabase(const QString& pluginId, const QString& statement, const QVariantList& bindings);
    [[nodiscard]] QFuture<Result<QByteArray>> readFile(const QString& path, qint64 maximumBytes);
    [[nodiscard]] QFuture<Result<QVector<filesystem::DirectoryEntry>>> listDirectory(const QString& path, int maximumEntries);
    [[nodiscard]] QFuture<Result<void>> writeFile(const QString& path, const QByteArray& content);
    [[nodiscard]] QFuture<Result<void>> createFile(const QString& path);
    [[nodiscard]] QFuture<Result<void>> createDirectory(const QString& path);
    [[nodiscard]] QFuture<Result<void>> movePath(const QString& sourcePath, const QString& destinationPath);
    [[nodiscard]] QFuture<Result<void>> copyFile(const QString& sourcePath, const QString& destinationPath);
    [[nodiscard]] QFuture<Result<void>> removeFile(const QString& path);
    [[nodiscard]] QFuture<Result<void>> removeDirectory(const QString& path);
    [[nodiscard]] bool confirm(QWidget* parent, const QString& title, const QString& message, const QString& detail, const QString& action, bool destructive) const;
    [[nodiscard]] Result<void> provideCapability(const QString& pluginId, const CapabilityDescriptor& descriptor);
    [[nodiscard]] bool capabilityAvailable(const QString& name) const;
    [[nodiscard]] QStringList capabilities() const;
    void invokeCapability(const QString& senderPluginId, const QString& name, const QJsonObject& payload, QObject& callbackContext, PluginReply reply);
    void request(const QString& senderPluginId, const QString& targetPluginId, const QString& topic, const QJsonObject& payload, QObject& callbackContext, PluginReply reply);
    void completeRequest(quint64 requestId, Result<QJsonObject> result);
    void removeRequest(quint64 requestId);
    void cancelRequests();
    void publish(const QString& senderPluginId, const QString& topic, const QJsonObject& payload);
    void log(const QString& senderPluginId, LogLevel level, const QString& category, const QString& message, const QJsonObject& details);
    void notifyPlugin(const QString& senderPluginId, const QString& title, const QString& message, AlertSeverity severity);
    LocalizationService m_localization;
    CapabilityRegistry m_capabilities;
    QString m_applicationDataPath;
    std::vector<std::unique_ptr<QPluginLoader>> m_loaders;
    std::vector<std::unique_ptr<PluginHost>> m_pluginHosts;
    QVector<PluginInterface*> m_plugins;
    QVector<PluginInterface*> m_initializedPlugins;
    QVector<PluginNavigationItem> m_navigationItems;
    QHash<quint64, std::shared_ptr<PendingPluginRequest>> m_pendingRequests;
    persistence::StateStore* m_stateStore{nullptr};
    persistence::DatabaseExecutor* m_databaseExecutor{nullptr};
    std::unique_ptr<filesystem::FileSystemService> m_fileSystem;
    std::optional<Error> m_coreCatalogError;
    const ui::Theme* m_theme{nullptr};
    quint64 m_nextRequestId{0};
    bool m_initialized{false};
};

} // namespace workpane::plugins
