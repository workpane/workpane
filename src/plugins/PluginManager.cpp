#include "plugins/PluginManager.h"

#include "domain/ApplicationLanguage.h"
#include "filesystem/FileSystemService.h"
#include "persistence/DatabaseExecutor.h"
#include "persistence/StateStore.h"
#include "persistence/StoredValues.h"
#include "plugins/CoreTranslations.h"
#include "ui/ConfirmationDialog.h"
#include "ui/Theme.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QLocale>
#include <QPluginLoader>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

namespace workpane::plugins {

struct PendingPluginRequest final {
    QPointer<QObject> callbackContext;
    PluginReply reply;
    std::optional<Result<QJsonObject>> result;
    QPointer<QTimer> timeout;
    QMetaObject::Connection contextGuard;
};

class ScopedPluginHost final : public PluginHost {
  public:
    ScopedPluginHost(PluginManager& manager, QString pluginId) : m_manager(manager), m_pluginId(std::move(pluginId)) {}

    [[nodiscard]] QString translate(const QString& key) const override {
        return m_manager.translate(key);
    }

    [[nodiscard]] const ui::Theme& theme() const override {
        return m_manager.theme();
    }

    [[nodiscard]] Result<void> provideCapability(const CapabilityDescriptor& descriptor) override {
        return m_manager.provideCapability(m_pluginId, descriptor);
    }

    [[nodiscard]] bool capabilityAvailable(const QString& name) const override {
        return m_manager.capabilityAvailable(name);
    }

    [[nodiscard]] QStringList capabilities() const override {
        return m_manager.capabilities();
    }

    void invokeCapability(const QString& name, const QJsonObject& payload, QObject& callbackContext, PluginReply reply) override {
        m_manager.invokeCapability(m_pluginId, name, payload, callbackContext, std::move(reply));
    }

    [[nodiscard]] const QString& applicationDataPath() const override {
        return m_manager.applicationDataPath();
    }

    [[nodiscard]] QJsonObject settings() const override {
        return m_manager.settings(m_pluginId);
    }

    [[nodiscard]] QFuture<Result<void>> saveSettings(const QJsonObject& document) override {
        return m_manager.saveSettings(m_pluginId, document);
    }

    [[nodiscard]] Result<void> migrateDatabase(const QVector<persistence::DatabaseMigration>& migrations) override {
        return m_manager.migrateDatabase(m_pluginId, migrations);
    }

    [[nodiscard]] Result<void> executeBootstrapDatabaseTransaction(const QVector<persistence::DatabaseStatement>& statements) override {
        return m_manager.executeBootstrapDatabaseTransaction(m_pluginId, statements);
    }

    [[nodiscard]] Result<persistence::DatabaseRows> queryBootstrapDatabase(const QString& statement, const QVariantList& bindings) const override {
        return m_manager.queryBootstrapDatabase(m_pluginId, statement, bindings);
    }

    [[nodiscard]] QFuture<Result<void>> executeDatabase(const QString& statement, const QVariantList& bindings) override {
        return m_manager.executeDatabase(m_pluginId, statement, bindings);
    }

    [[nodiscard]] QFuture<Result<void>> executeDatabaseTransaction(const QVector<persistence::DatabaseStatement>& statements) override {
        return m_manager.executeDatabaseTransaction(m_pluginId, statements);
    }

    [[nodiscard]] QFuture<Result<persistence::DatabaseRows>> queryDatabase(const QString& statement, const QVariantList& bindings) override {
        return m_manager.queryDatabase(m_pluginId, statement, bindings);
    }

    [[nodiscard]] QFuture<Result<QByteArray>> readFile(const QString& path, qint64 maximumBytes) override {
        return m_manager.readFile(path, maximumBytes);
    }

    [[nodiscard]] QFuture<Result<QVector<filesystem::DirectoryEntry>>> listDirectory(const QString& path, int maximumEntries) override {
        return m_manager.listDirectory(path, maximumEntries);
    }

    [[nodiscard]] QFuture<Result<void>> writeFile(const QString& path, const QByteArray& content) override {
        return m_manager.writeFile(path, content);
    }

    [[nodiscard]] QFuture<Result<void>> createFile(const QString& path) override {
        return m_manager.createFile(path);
    }

    [[nodiscard]] QFuture<Result<void>> createDirectory(const QString& path) override {
        return m_manager.createDirectory(path);
    }

    [[nodiscard]] QFuture<Result<void>> movePath(const QString& sourcePath, const QString& destinationPath) override {
        return m_manager.movePath(sourcePath, destinationPath);
    }

    [[nodiscard]] QFuture<Result<void>> copyFile(const QString& sourcePath, const QString& destinationPath) override {
        return m_manager.copyFile(sourcePath, destinationPath);
    }

    [[nodiscard]] QFuture<Result<void>> removeFile(const QString& path) override {
        return m_manager.removeFile(path);
    }

    [[nodiscard]] QFuture<Result<void>> removeDirectory(const QString& path) override {
        return m_manager.removeDirectory(path);
    }

    [[nodiscard]] bool confirm(QWidget* parent, const QString& title, const QString& message, const QString& detail, const QString& action, bool destructive) const override {
        return m_manager.confirm(parent, title, message, detail, action, destructive);
    }

    void publish(const QString& topic, const QJsonObject& payload) override {
        m_manager.publish(m_pluginId, topic, payload);
    }

    void log(LogLevel level, const QString& category, const QString& message, const QJsonObject& details) override {
        m_manager.log(m_pluginId, level, category, message, details);
    }

    void notify(const QString& title, const QString& message, AlertSeverity severity) override {
        m_manager.notifyPlugin(m_pluginId, title, message, severity);
    }

    void showNavigation(const QString& navigationId) override {
        m_manager.showNavigation(m_pluginId, navigationId);
    }

  private:
    PluginManager& m_manager;
    QString m_pluginId;
};

constexpr auto coreSourceId = "workpane";
constexpr auto pluginRequestTimeout = std::chrono::seconds(30);

class PluginManagerHelper final {
  public:
    static QString pluginDirectoryPath();
    static QString pluginFilePattern();
    static const QRegularExpression& contributionIdPattern();
    static bool ownsTranslationKey(const QString& pluginId, const QString& key);
    static QStringList pluginPaths();
    static QString logLevelName(LogLevel level);
};

QString PluginManagerHelper::pluginDirectoryPath() {
#ifdef Q_OS_MACOS
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../PlugIns"));
#elif defined(Q_OS_WIN)
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("plugins"));
#else
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("plugins"));
#endif
}

QString PluginManagerHelper::pluginFilePattern() {
#ifdef Q_OS_MACOS
    return QStringLiteral("*.dylib");
#elif defined(Q_OS_WIN)
    return QStringLiteral("*.dll");
#else
    return QStringLiteral("*.so");
#endif
}

const QRegularExpression& PluginManagerHelper::contributionIdPattern() {
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9]+(?:-[a-z0-9]+)*$"));
    return pattern;
}

bool PluginManagerHelper::ownsTranslationKey(const QString& pluginId, const QString& key) {
    return key.startsWith(pluginId + QLatin1Char('.'));
}

QStringList PluginManagerHelper::pluginPaths() {
    const QDir directory(pluginDirectoryPath());
    const QFileInfoList entries = directory.entryInfoList({pluginFilePattern()}, QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);

    QStringList paths;
    paths.reserve(entries.size());

    for (const auto& entry : entries) {
        paths.append(entry.absoluteFilePath());
    }

    return paths;
}

QString PluginManagerHelper::logLevelName(LogLevel level) {
    switch (level) {
    case LogLevel::Debug:
        return QStringLiteral("debug");
    case LogLevel::Info:
        return QStringLiteral("info");
    case LogLevel::Warning:
        return QStringLiteral("warning");
    case LogLevel::Error:
        return QStringLiteral("error");
    }

    return QStringLiteral("info");
}

PluginManager::PluginManager(QObject* parent) : QObject(parent), m_localization(domain::ApplicationLanguages::resolveApplicationLanguage(QLocale::system().name(QLocale::TagSeparator::Dash))), m_fileSystem(std::make_unique<filesystem::FileSystemService>()), m_theme(&ui::ThemeManager::instance().theme()) {
    const auto result = m_localization.registerCatalog(QStringLiteral("workpane"), coretranslations::CoreCatalog::catalog());

    if (!result.hasValue()) {
        m_coreCatalogError = result.error();
    }
}

PluginManager::~PluginManager() {
    unloadPlugins();
}

Result<void> PluginManager::loadPlugins() {
    if (m_coreCatalogError.has_value()) {
        return Result<void>::failure(m_coreCatalogError.value());
    }
    if (!m_loaders.empty()) {
        return Result<void>::failure({"plugin_already_loaded", "The bundled plugins are already loaded", {}});
    }

    const QStringList paths = PluginManagerHelper::pluginPaths();

    if (paths.isEmpty()) {
        return Result<void>::failure({"plugin_directory_not_found", "No application plugins were found", PluginManagerHelper::pluginDirectoryPath()});
    }

    for (const auto& path : paths) {
        auto loader = std::make_unique<QPluginLoader>(QDir::cleanPath(path));
        QObject* instance = loader->instance();
        if (instance == nullptr) {
            const auto error = Error{"plugin_load_failed", "A bundled plugin could not be loaded", loader->errorString()};
            unloadPlugins();
            return Result<void>::failure(error);
        }

        auto* interface = qobject_cast<PluginInterface*>(instance);
        if (interface == nullptr) {
            const auto error = Error{"plugin_interface_invalid", "A bundled plugin has an invalid interface", loader->fileName()};
            loader->unload();
            unloadPlugins();
            return Result<void>::failure(error);
        }

        const auto result = registerPlugin(*interface);
        if (!result.hasValue()) {
            loader->unload();
            unloadPlugins();
            return result;
        }
        m_loaders.push_back(std::move(loader));
    }

    return Result<void>::success();
}

Result<void> PluginManager::initialize(QString applicationDataPath, persistence::StateStore& stateStore, persistence::DatabaseExecutor& databaseExecutor) {
    if (m_initialized) {
        return Result<void>::failure({"plugin_already_initialized", "The plugins are already initialized", {}});
    }

    if (applicationDataPath.isEmpty()) {
        return Result<void>::failure({"plugin_data_path_invalid", "The plugin data path is invalid", {}});
    }

    m_applicationDataPath = std::move(applicationDataPath);
    m_stateStore = &stateStore;
    m_databaseExecutor = &databaseExecutor;

    QVector<PluginInterface*> pending = m_plugins;
    QSet<QString> ready;

    while (!pending.isEmpty()) {
        bool progressed = false;
        for (qsizetype index = pending.size() - 1; index >= 0; --index) {
            auto* loadedPlugin = pending.at(index);
            bool dependenciesReady = true;
            for (const auto& dependencyId : loadedPlugin->dependencies()) {
                if (plugin(dependencyId) == nullptr) {
                    shutdown();
                    return Result<void>::failure({"plugin_dependency_missing", "A plugin dependency is missing", QStringLiteral("%1 -> %2").arg(loadedPlugin->id(), dependencyId)});
                }
                if (!ready.contains(dependencyId)) {
                    dependenciesReady = false;
                }
            }
            if (!dependenciesReady) {
                continue;
            }

            auto* pluginHost = hostFor(loadedPlugin->id());
            if (pluginHost == nullptr) {
                shutdown();
                return Result<void>::failure({"plugin_host_unavailable", "The plugin host is unavailable", loadedPlugin->id()});
            }

            const auto result = loadedPlugin->initialize(*pluginHost);
            if (!result.hasValue()) {
                loadedPlugin->shutdown();
                shutdown();
                return result;
            }
            if (loadedPlugin->databaseSchemaVersion() > 0) {
                const auto schemaVersion = m_stateStore->pluginSchemaVersion(loadedPlugin->id());
                if (!schemaVersion.hasValue() || schemaVersion.value() != loadedPlugin->databaseSchemaVersion()) {
                    loadedPlugin->shutdown();
                    shutdown();
                    return Result<void>::failure(schemaVersion.hasValue() ? Error{"plugin_database_version_mismatch", "The initialized plugin database schema does not match its declaration", loadedPlugin->id()} : schemaVersion.error());
                }
            }
            m_initializedPlugins.append(loadedPlugin);
            ready.insert(loadedPlugin->id());
            pending.removeAt(index);
            progressed = true;
        }
        if (!progressed) {
            shutdown();
            return Result<void>::failure({"plugin_dependency_cycle", "The plugin dependency graph contains a cycle", {}});
        }
    }

    m_initialized = true;
    return Result<void>::success();
}

void PluginManager::shutdown() {
    cancelRequests();
    // Nothing answers once teardown begins, so a capability asked for while the plugins are shutting down is refused rather than delivered to one that already stopped.
    m_capabilities.clear();

    for (auto loadedPlugin = m_initializedPlugins.crbegin(); loadedPlugin != m_initializedPlugins.crend(); ++loadedPlugin) {
        (*loadedPlugin)->shutdown();
    }

    m_initializedPlugins.clear();
    m_initialized = false;
    m_stateStore = nullptr;
    m_databaseExecutor = nullptr;
    m_applicationDataPath.clear();
}

QVector<PluginNavigationItem> PluginManager::navigationItems() const {
    QVector<PluginNavigationItem> registered = m_navigationItems;
    // clang-format off
    std::stable_sort(registered.begin(), registered.end(), [](const PluginNavigationItem& left, const PluginNavigationItem& right) {
        if (left.item.placement != right.item.placement) {
            return left.item.placement == NavigationPlacement::Primary;
        }
        return left.item.order < right.item.order;
    });
    // clang-format on
    return registered;
}

QVector<PluginSettingsContribution> PluginManager::settings() const {
    QVector<PluginSettingsContribution> registered;

    for (auto* loadedPlugin : m_plugins) {
        for (const auto& group : loadedPlugin->settingsGroups()) {
            registered.append({loadedPlugin->id(), group});
        }
    }

    return registered;
}

QHash<QString, int> PluginManager::databaseSchemaVersions() const {
    QHash<QString, int> versions;

    for (auto* loadedPlugin : m_plugins) {
        if (loadedPlugin->databaseSchemaVersion() > 0) {
            versions.insert(loadedPlugin->id(), loadedPlugin->databaseSchemaVersion());
        }
    }

    return versions;
}

QWidget* PluginManager::createNavigationView(const QString& pluginId, const QString& itemId, QWidget* parent) const {
    auto* loadedPlugin = plugin(pluginId);
    return loadedPlugin == nullptr ? nullptr : loadedPlugin->createNavigationView(itemId, parent);
}

QWidget* PluginManager::createSettingsSection(const QString& pluginId, const QString& groupId, const QString& sectionId, QWidget* parent) const {
    auto* loadedPlugin = plugin(pluginId);
    return loadedPlugin == nullptr ? nullptr : loadedPlugin->createSettingsSection(groupId, sectionId, parent);
}

QString PluginManager::pluginTitle(const QString& pluginId) const {
    auto* loadedPlugin = plugin(pluginId);
    return loadedPlugin == nullptr ? pluginId : translate(loadedPlugin->titleKey());
}

QString PluginManager::styleSheet() const {
    QString styles;

    for (auto* loadedPlugin : m_plugins) {
        styles.append(loadedPlugin->styleSheet(*m_theme));
    }

    return ui::ThemeTokens::substituted(std::move(styles), *m_theme);
}

void PluginManager::setTheme(const ui::Theme& theme) {
    m_theme = &theme;
    m_navigationItems.clear();

    for (auto* loadedPlugin : m_plugins) {
        for (const auto& item : loadedPlugin->navigationItems(theme)) {
            m_navigationItems.append({loadedPlugin->id(), item});
        }
    }
}

const ui::Theme& PluginManager::theme() const {
    return *m_theme;
}

Result<void> PluginManager::setLocale(const QString& localeName) {
    if (!domain::ApplicationLanguages::isSupportedApplicationLanguage(localeName)) {
        return Result<void>::failure({"application_language_invalid", "The application language is unsupported", localeName});
    }

    return m_localization.setLocale(localeName);
}

const QString& PluginManager::localeName() const {
    return m_localization.localeName();
}

QString PluginManager::translate(const QString& key) const {
    return m_localization.translate(key);
}

const QString& PluginManager::applicationDataPath() const {
    return m_applicationDataPath;
}

// Settings are one document per owner, so a value the owner adds later never changes the schema that stores it.
QJsonObject PluginManager::settings(const QString& ownerId) const {
    return m_stateStore == nullptr ? QJsonObject{} : m_stateStore->settings(ownerId);
}

QFuture<Result<void>> PluginManager::saveSettings(const QString& ownerId, const QJsonObject& document) {
    if (m_stateStore == nullptr || m_databaseExecutor == nullptr) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"plugin_settings_unavailable", "The settings store is unavailable", ownerId}));
    }

    return m_databaseExecutor->saveSettings(ownerId, document);
}

Result<void> PluginManager::migrateDatabase(const QString& pluginId, const QVector<persistence::DatabaseMigration>& migrations) {
    auto* loadedPlugin = plugin(pluginId);

    if (m_stateStore == nullptr || loadedPlugin == nullptr) {
        return Result<void>::failure({"plugin_database_unavailable", "The plugin database is unavailable", pluginId});
    }
    if (migrations.isEmpty() || loadedPlugin->databaseSchemaVersion() <= 0 || migrations.last().version != loadedPlugin->databaseSchemaVersion()) {
        return Result<void>::failure({"plugin_database_version_mismatch", "The plugin database migration plan does not match its declared schema version", pluginId});
    }

    return m_stateStore->migratePluginDatabase(pluginId, migrations);
}

Result<void> PluginManager::executeBootstrapDatabaseTransaction(const QString& pluginId, const QVector<persistence::DatabaseStatement>& statements) {
    if (m_stateStore == nullptr || plugin(pluginId) == nullptr) {
        return Result<void>::failure({"plugin_database_unavailable", "The plugin database is unavailable", pluginId});
    }

    return m_stateStore->executePluginDatabaseTransaction(pluginId, statements);
}

Result<persistence::DatabaseRows> PluginManager::queryBootstrapDatabase(const QString& pluginId, const QString& statement, const QVariantList& bindings) const {
    if (m_stateStore == nullptr || plugin(pluginId) == nullptr) {
        return Result<persistence::DatabaseRows>::failure({"plugin_database_unavailable", "The plugin database is unavailable", pluginId});
    }

    return m_stateStore->queryPluginDatabase(pluginId, statement, bindings);
}

QFuture<Result<void>> PluginManager::executeDatabase(const QString& pluginId, const QString& statement, const QVariantList& bindings) {
    if (m_databaseExecutor == nullptr || plugin(pluginId) == nullptr) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"plugin_database_unavailable", "The plugin database is unavailable", pluginId}));
    }

    return m_databaseExecutor->executePluginDatabase(pluginId, statement, bindings);
}

QFuture<Result<void>> PluginManager::executeDatabaseTransaction(const QString& pluginId, const QVector<persistence::DatabaseStatement>& statements) {
    if (m_databaseExecutor == nullptr || plugin(pluginId) == nullptr) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"plugin_database_unavailable", "The plugin database is unavailable", pluginId}));
    }

    return m_databaseExecutor->executePluginDatabaseTransaction(pluginId, statements);
}

QFuture<Result<persistence::DatabaseRows>> PluginManager::queryDatabase(const QString& pluginId, const QString& statement, const QVariantList& bindings) {
    if (m_databaseExecutor == nullptr || plugin(pluginId) == nullptr) {
        return QtFuture::makeReadyValueFuture(Result<persistence::DatabaseRows>::failure({"plugin_database_unavailable", "The plugin database is unavailable", pluginId}));
    }

    return m_databaseExecutor->queryPluginDatabase(pluginId, statement, bindings);
}

QFuture<Result<QByteArray>> PluginManager::readFile(const QString& path, qint64 maximumBytes) {
    return m_fileSystem->readFile(path, maximumBytes);
}

QFuture<Result<QVector<filesystem::DirectoryEntry>>> PluginManager::listDirectory(const QString& path, int maximumEntries) {
    return m_fileSystem->listDirectory(path, maximumEntries);
}

QFuture<Result<void>> PluginManager::writeFile(const QString& path, const QByteArray& content) {
    return m_fileSystem->writeFile(path, content);
}

QFuture<Result<void>> PluginManager::createFile(const QString& path) {
    return m_fileSystem->createFile(path);
}

QFuture<Result<void>> PluginManager::createDirectory(const QString& path) {
    return m_fileSystem->createDirectory(path);
}

QFuture<Result<void>> PluginManager::movePath(const QString& sourcePath, const QString& destinationPath) {
    return m_fileSystem->movePath(sourcePath, destinationPath);
}

QFuture<Result<void>> PluginManager::copyFile(const QString& sourcePath, const QString& destinationPath) {
    return m_fileSystem->copyFile(sourcePath, destinationPath);
}

QFuture<Result<void>> PluginManager::removeFile(const QString& path) {
    return m_fileSystem->removeFile(path);
}

QFuture<Result<void>> PluginManager::removeDirectory(const QString& path) {
    return m_fileSystem->removeDirectory(path);
}

bool PluginManager::confirm(QWidget* parent, const QString& title, const QString& message, const QString& detail, const QString& action, bool destructive) const {
    return ui::ConfirmationDialog::confirm(parent, title, message, detail, translate(QStringLiteral("workpane.actions.cancel")), action, destructive);
}

Result<void> PluginManager::provideCapability(const QString& pluginId, const CapabilityDescriptor& descriptor) {
    return m_capabilities.provide(pluginId, descriptor);
}

bool PluginManager::capabilityAvailable(const QString& name) const {
    return m_capabilities.contains(name);
}

QStringList PluginManager::capabilities() const {
    return m_capabilities.names();
}

void PluginManager::invokeCapability(const QString& senderPluginId, const QString& name, const QJsonObject& payload, QObject& callbackContext, PluginReply reply) {
    if (!reply) {
        return;
    }

    const QString provider = m_capabilities.provider(name);

    if (provider.isEmpty()) {
        reply(Result<QJsonObject>::failure({"capability_unknown", "No plugin provides this capability", name}));
        return;
    }

    request(senderPluginId, provider, name, payload, callbackContext, std::move(reply));
}

void PluginManager::request(const QString& senderPluginId, const QString& targetPluginId, const QString& topic, const QJsonObject& payload, QObject& callbackContext, PluginReply reply) {
    if (!reply) {
        return;
    }

    const quint64 requestId = ++m_nextRequestId;
    auto pending = std::make_shared<PendingPluginRequest>();
    pending->callbackContext = &callbackContext;
    pending->reply = std::move(reply);
    auto* timeout = new QTimer(this);
    timeout->setSingleShot(true);
    timeout->setInterval(pluginRequestTimeout);
    pending->timeout = timeout;
    m_pendingRequests.insert(requestId, pending);

    const QPointer<PluginManager> manager(this);
    // clang-format off
    const auto respond = [manager, requestId](Result<QJsonObject> result) {
        if (manager == nullptr) {
            return;
        }
        QMetaObject::invokeMethod(manager, [manager, requestId, result = std::move(result)]() mutable { if (manager != nullptr) { manager->completeRequest(requestId, std::move(result)); } }, Qt::QueuedConnection);
    };
    // clang-format on
    // clang-format off
    connect(timeout, &QTimer::timeout, this, [this, requestId, targetPluginId, topic]() { completeRequest(requestId, Result<QJsonObject>::failure({"plugin_message_timeout", "The plugin request timed out", QStringLiteral("%1: %2").arg(targetPluginId, topic)})); });
    pending->contextGuard = connect(&callbackContext, &QObject::destroyed, this, [this, requestId]() { removeRequest(requestId); });
    // clang-format on
    timeout->start();

    auto* sender = plugin(senderPluginId);
    auto* target = plugin(targetPluginId);

    if (sender == nullptr || target == nullptr || !m_initializedPlugins.contains(target) || topic.isEmpty()) {
        respond(Result<QJsonObject>::failure({"plugin_message_invalid", "The plugin message is invalid", QStringLiteral("%1 -> %2: %3").arg(senderPluginId, targetPluginId, topic)}));
        return;
    }

    auto* targetObject = dynamic_cast<QObject*>(target);

    if (targetObject == nullptr) {
        respond(Result<QJsonObject>::failure({"plugin_message_target_invalid", "The target plugin cannot receive messages", targetPluginId}));
        return;
    }

    const QPointer<QObject> guardedTarget(targetObject);
    // clang-format off
    QTimer::singleShot(0, this, [this, guardedTarget, targetPluginId, senderPluginId, topic, payload, respond]() mutable {
        auto* currentTarget = plugin(targetPluginId);
        if (!m_initialized || guardedTarget == nullptr || currentTarget == nullptr || dynamic_cast<QObject*>(currentTarget) != guardedTarget || !m_initializedPlugins.contains(currentTarget)) {
            respond(Result<QJsonObject>::failure({"plugin_message_target_unavailable", "The target plugin is unavailable", targetPluginId}));
            return;
        }
        currentTarget->handleRequest(senderPluginId, topic, payload, std::move(respond));
    });
    // clang-format on
}

void PluginManager::completeRequest(quint64 requestId, Result<QJsonObject> result) {
    const auto iterator = m_pendingRequests.find(requestId);

    if (iterator == m_pendingRequests.end() || iterator.value()->result.has_value()) {
        return;
    }

    const auto pending = iterator.value();
    pending->result = std::move(result);

    if (pending->timeout != nullptr) {
        pending->timeout->stop();
        pending->timeout->deleteLater();
        pending->timeout = nullptr;
    }

    if (pending->callbackContext == nullptr) {
        removeRequest(requestId);
        return;
    }

    const QPointer<PluginManager> manager(this);
    // clang-format off
    const bool submitted = QMetaObject::invokeMethod(pending->callbackContext, [manager, requestId, pending]() mutable {
        if (pending->reply && pending->result.has_value()) {
            auto reply = std::move(pending->reply);
            pending->reply = {};
            reply(std::move(pending->result.value()));
        }
        if (manager != nullptr) {
            QMetaObject::invokeMethod(manager, [manager, requestId]() { if (manager != nullptr) { manager->removeRequest(requestId); } }, Qt::QueuedConnection);
        }
    }, Qt::QueuedConnection);
    // clang-format on

    if (!submitted) {
        removeRequest(requestId);
    }
}

void PluginManager::removeRequest(quint64 requestId) {
    const auto iterator = m_pendingRequests.find(requestId);

    if (iterator == m_pendingRequests.end()) {
        return;
    }

    if (iterator.value()->timeout != nullptr) {
        iterator.value()->timeout->stop();
        iterator.value()->timeout->deleteLater();
    }
    // A request that is gone stops watching the context it was given, which outlives it and would otherwise collect one guard per request.
    QObject::disconnect(iterator.value()->contextGuard);
    iterator.value()->reply = {};
    m_pendingRequests.erase(iterator);
}

void PluginManager::cancelRequests() {
    const auto requestIds = m_pendingRequests.keys();

    for (const quint64 requestId : requestIds) {
        removeRequest(requestId);
    }
}

void PluginManager::publish(const QString& senderPluginId, const QString& topic, const QJsonObject& payload) {
    if ((senderPluginId != QString::fromLatin1(coreSourceId) && plugin(senderPluginId) == nullptr) || topic.isEmpty()) {
        return;
    }

    for (auto* target : m_initializedPlugins) {
        if (target->id() == senderPluginId) {
            continue;
        }
        auto* targetObject = dynamic_cast<QObject*>(target);
        if (targetObject == nullptr) {
            continue;
        }

        const QString targetPluginId = target->id();
        const QPointer<QObject> guardedTarget(targetObject);
        // clang-format off
        QTimer::singleShot(0, this, [this, guardedTarget, targetPluginId, senderPluginId, topic, payload]() {
            auto* currentTarget = plugin(targetPluginId);
            if (!m_initialized || guardedTarget == nullptr || currentTarget == nullptr || dynamic_cast<QObject*>(currentTarget) != guardedTarget || !m_initializedPlugins.contains(currentTarget)) {
                return;
            }
            currentTarget->handleEvent(senderPluginId, topic, payload);
        });
        // clang-format on
    }
}

void PluginManager::log(const QString& senderPluginId, LogLevel level, const QString& category, const QString& message, const QJsonObject& details) {
    if (category.trimmed().isEmpty() || message.trimmed().isEmpty()) {
        return;
    }

    publish(senderPluginId, QStringLiteral("workpane.log.entry"), {{QStringLiteral("timestampUtc"), persistence::StoredValues::storedTimestamp(QDateTime::currentDateTimeUtc())}, {QStringLiteral("level"), PluginManagerHelper::logLevelName(level)}, {QStringLiteral("category"), category}, {QStringLiteral("message"), message}, {QStringLiteral("details"), details}});
}

// A plugin asks for a destination of its own, so a request answered by one plugin never moves the shell to another one.
void PluginManager::showNavigation(const QString& pluginId, const QString& navigationId) {
    // clang-format off
    const bool declared = std::any_of(m_navigationItems.cbegin(), m_navigationItems.cend(), [&pluginId, &navigationId](const PluginNavigationItem& contribution) { return contribution.pluginId == pluginId && contribution.item.id == navigationId; });
    // clang-format on

    if (!declared) {
        log(QString::fromLatin1(coreSourceId), LogLevel::Error, QStringLiteral("workpane.navigation"), QStringLiteral("A plugin asked for a navigation destination it does not declare"), {{QStringLiteral("pluginId"), pluginId}, {QStringLiteral("navigationId"), navigationId}});
        return;
    }

    emit navigationRequested(pluginId, navigationId);
}

void PluginManager::notify(const QString& title, const QString& message, AlertSeverity severity) {
    notifyPlugin(QString::fromLatin1(coreSourceId), title, message, severity);
}

void PluginManager::notifyPlugin(const QString& senderPluginId, const QString& title, const QString& message, AlertSeverity severity) {
    LogLevel level = LogLevel::Info;

    if (severity == AlertSeverity::Warning) {
        level = LogLevel::Warning;
    }

    if (severity == AlertSeverity::Error) {
        level = LogLevel::Error;
    }

    log(senderPluginId, level, QStringLiteral("notification"), message, {{QStringLiteral("title"), title}});
    emit notificationRequested(title, message, severity);
}

Result<void> PluginManager::registerPlugin(PluginInterface& loadedPlugin) {
    if (!PluginManagerHelper::contributionIdPattern().match(loadedPlugin.id()).hasMatch() || loadedPlugin.titleKey().isEmpty() || plugin(loadedPlugin.id()) != nullptr || loadedPlugin.databaseSchemaVersion() < 0) {
        return Result<void>::failure({"plugin_metadata_invalid", "A bundled plugin has invalid metadata", loadedPlugin.id()});
    }

    const TranslationCatalog catalog = loadedPlugin.translations();

    for (const auto& language : domain::ApplicationLanguages::supportedApplicationLanguages()) {
        if (!catalog.contains(language)) {
            return Result<void>::failure({"plugin_translation_language_missing", "A plugin does not support an application language", QStringLiteral("%1: %2").arg(loadedPlugin.id(), language)});
        }
    }

    const auto& englishTranslations = catalog.value(QStringLiteral("en"));

    if (!PluginManagerHelper::ownsTranslationKey(loadedPlugin.id(), loadedPlugin.titleKey()) || !englishTranslations.contains(loadedPlugin.titleKey())) {
        return Result<void>::failure({"plugin_title_translation_missing", "A plugin title translation is missing", loadedPlugin.titleKey()});
    }

    QSet<QString> dependencyIds;

    for (const auto& dependencyId : loadedPlugin.dependencies()) {
        if (!PluginManagerHelper::contributionIdPattern().match(dependencyId).hasMatch() || dependencyId == loadedPlugin.id() || dependencyIds.contains(dependencyId)) {
            return Result<void>::failure({"plugin_dependency_invalid", "A plugin dependency is invalid", dependencyId});
        }
        dependencyIds.insert(dependencyId);
    }

    const QVector<NavigationItem> navigationItems = loadedPlugin.navigationItems(*m_theme);
    QSet<QString> navigationIds;
    QSet<int> takenOrders;

    for (const auto& registeredItem : m_navigationItems) {
        takenOrders.insert(static_cast<int>(registeredItem.item.order));
    }

    for (const auto& item : navigationItems) {
        if (!PluginManagerHelper::contributionIdPattern().match(item.id).hasMatch() || item.titleKey.isEmpty() || !PluginManagerHelper::ownsTranslationKey(loadedPlugin.id(), item.titleKey) || item.icon.isNull() || navigationIds.contains(item.id) || !englishTranslations.contains(item.titleKey)) {
            return Result<void>::failure({"plugin_navigation_invalid", "A plugin navigation item is invalid", item.id});
        }
        // Two destinations claiming one position leave the bar to the order the filesystem happened to list the libraries in.
        if (takenOrders.contains(static_cast<int>(item.order))) {
            return Result<void>::failure({"plugin_navigation_order_taken", "The navigation position is already declared by another destination", item.id});
        }
        takenOrders.insert(static_cast<int>(item.order));
        navigationIds.insert(item.id);
    }

    QSet<QString> groupIds;

    for (const auto& group : loadedPlugin.settingsGroups()) {
        if (!PluginManagerHelper::contributionIdPattern().match(group.id).hasMatch() || group.titleKey.isEmpty() || !PluginManagerHelper::ownsTranslationKey(loadedPlugin.id(), group.titleKey) || groupIds.contains(group.id) || group.sections.isEmpty() || !englishTranslations.contains(group.titleKey)) {
            return Result<void>::failure({"plugin_settings_group_invalid", "A plugin settings group is invalid", group.id});
        }
        if (group.sections.size() == 1 && group.sections.first().id != QStringLiteral("general")) {
            return Result<void>::failure({"plugin_settings_general_required", "A single settings section must be General", group.id});
        }

        QSet<QString> sectionIds;
        for (const auto& section : group.sections) {
            if (!PluginManagerHelper::contributionIdPattern().match(section.id).hasMatch() || section.titleKey.isEmpty() || !PluginManagerHelper::ownsTranslationKey(loadedPlugin.id(), section.titleKey) || sectionIds.contains(section.id) || section.searchKeys.isEmpty() || !englishTranslations.contains(section.titleKey)) {
                return Result<void>::failure({"plugin_settings_section_invalid", "A plugin settings section is invalid", QStringLiteral("%1/%2").arg(group.id, section.id)});
            }
            QSet<QString> searchKeys;
            for (const auto& searchKey : section.searchKeys) {
                if (!PluginManagerHelper::ownsTranslationKey(loadedPlugin.id(), searchKey) || searchKeys.contains(searchKey) || !englishTranslations.contains(searchKey)) {
                    return Result<void>::failure({"plugin_settings_search_translation_missing", "A plugin settings search translation is missing", searchKey});
                }
                searchKeys.insert(searchKey);
            }
            sectionIds.insert(section.id);
        }
        groupIds.insert(group.id);
    }

    const auto localizationResult = m_localization.registerCatalog(loadedPlugin.id(), catalog);

    if (!localizationResult.hasValue()) {
        return localizationResult;
    }

    m_plugins.append(&loadedPlugin);

    for (const auto& item : navigationItems) {
        m_navigationItems.append({loadedPlugin.id(), item});
    }

    m_pluginHosts.push_back(std::make_unique<ScopedPluginHost>(*this, loadedPlugin.id()));
    return Result<void>::success();
}

PluginInterface* PluginManager::plugin(const QString& pluginId) const {
    for (auto* loadedPlugin : m_plugins) {
        if (loadedPlugin->id() == pluginId) {
            return loadedPlugin;
        }
    }

    return nullptr;
}

PluginHost* PluginManager::hostFor(const QString& pluginId) const {
    for (qsizetype index = 0; index < m_plugins.size(); ++index) {
        if (m_plugins.at(index)->id() == pluginId) {
            return m_pluginHosts.at(static_cast<std::size_t>(index)).get();
        }
    }

    return nullptr;
}

void PluginManager::unloadPlugins() {
    shutdown();
    m_fileSystem->drain();

    for (auto* loadedPlugin : m_plugins) {
        m_localization.unregisterCatalog(loadedPlugin->id());
    }

    m_plugins.clear();
    m_navigationItems.clear();

    for (auto& loader : m_loaders) {
        loader->unload();
    }

    m_loaders.clear();
    m_pluginHosts.clear();
}

} // namespace workpane::plugins
