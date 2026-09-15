#include "SystemInformationPlugin.h"

#include "SystemInformationTranslations.h"
#include "SystemInformationView.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QJsonObject>

#include <memory>
#include <optional>
#include <utility>

namespace workpane::plugins::systeminformation {

SystemInformationPlugin::SystemInformationPlugin() : SystemInformationPlugin(std::make_shared<HwinfoSystemInformationProvider>()) {}

SystemInformationPlugin::SystemInformationPlugin(std::shared_ptr<SystemInformationProvider> provider) : m_provider(std::move(provider)) {}

SystemInformationPlugin::~SystemInformationPlugin() {
    shutdown();
}

QString SystemInformationPlugin::id() const {
    return QStringLiteral("system-information");
}

QString SystemInformationPlugin::titleKey() const {
    return QStringLiteral("system-information.plugin.title");
}

QStringList SystemInformationPlugin::dependencies() const {
    return {};
}

int SystemInformationPlugin::databaseSchemaVersion() const {
    return 0;
}

TranslationCatalog SystemInformationPlugin::translations() const {
    return translations::SystemInformationCatalog::catalog();
}

QString SystemInformationPlugin::styleSheet(const ui::Theme&) const {
    return QStringLiteral("QFrame#systemInformationCard { background: @panel; border: 1px solid @border; border-radius: 6px; } QLabel#systemInformationSectionTitle { color: @text; font-weight: 600; } QLabel#systemInformationDeviceTitle { color: @text; font-weight: 600; } QLabel#systemInformationField, QLabel#systemInformationUpdated { color: @textMuted; } QProgressBar#systemInformationProgress { background: @raised; border: none; border-radius: 3px; color: @text; text-align: center; } QProgressBar#systemInformationProgress::chunk { background: @accent; border-radius: 3px; }");
}

QVector<NavigationItem> SystemInformationPlugin::navigationItems(const ui::Theme& theme) const {
    return {{QStringLiteral("overview"), QStringLiteral("system-information.navigation.overview"), ui::IconCatalog::icon(ui::IconName::System, theme), NavigationPlacement::Secondary, NavigationOrder::System}};
}

QVector<SettingsGroup> SystemInformationPlugin::settingsGroups() const {
    return {};
}

Result<void> SystemInformationPlugin::initialize(PluginHost& host) {
    if (m_host != nullptr) {
        return Result<void>::failure({"systeminformation_already_initialized", "The System Information plugin is already initialized", {}});
    }
    if (m_provider == nullptr) {
        return Result<void>::failure({"systeminformation_provider_unavailable", "The System Information provider is unavailable", {}});
    }

    m_host = &host;
    m_asyncContext = std::make_unique<QObject>();
    return Result<void>::success();
}

QWidget* SystemInformationPlugin::createNavigationView(const QString& itemId, QWidget* parent) {
    return itemId == QStringLiteral("overview") && m_host != nullptr ? new SystemInformationView(*this, *m_host, parent) : nullptr;
}

QWidget* SystemInformationPlugin::createSettingsSection(const QString&, const QString&, QWidget*) {
    return nullptr;
}

void SystemInformationPlugin::handleRequest(const QString&, const QString& topic, const QJsonObject&, PluginReply reply) {
    reply(SettingsReaders::unhandledTopic(topic));
}

void SystemInformationPlugin::shutdown() {
    m_asyncContext.reset();
    ++m_collectionGeneration;

    if (m_collection.has_value()) {
        m_collection->cancelled->store(true, std::memory_order_relaxed);
        m_collection->future.waitForFinished();
        m_collection.reset();
    }

    m_snapshot.reset();
    m_refreshing = false;
    m_host = nullptr;
}

const std::optional<SystemSnapshot>& SystemInformationPlugin::snapshot() const {
    return m_snapshot;
}

bool SystemInformationPlugin::isRefreshing() const {
    return m_refreshing;
}

Result<void> SystemInformationPlugin::refresh() {
    if (m_host == nullptr || m_asyncContext == nullptr) {
        return Result<void>::failure({"systeminformation_not_initialized", "The System Information plugin is not initialized", {}});
    }
    if (m_refreshing) {
        return Result<void>::failure({"systeminformation_refresh_in_progress", "System information is already being collected", {}});
    }

    m_refreshing = true;
    emit refreshStateChanged(true);
    const quint64 generation = ++m_collectionGeneration;
    m_collection.emplace(SystemCollection::collectSystemInformation(m_provider));
    auto future = m_collection->future;
    // clang-format off
    future.then(m_asyncContext.get(), [this, generation](Result<SystemSnapshot> result) { completeRefresh(generation, std::move(result)); });
    // clang-format on
    return Result<void>::success();
}

void SystemInformationPlugin::completeRefresh(quint64 generation, Result<SystemSnapshot> result) {
    if (generation != m_collectionGeneration || m_host == nullptr) {
        return;
    }

    m_collection.reset();
    m_refreshing = false;
    emit refreshStateChanged(false);

    if (!result.hasValue()) {
        m_host->log(LogLevel::Error, QStringLiteral("collection"), result.error().message, {{QStringLiteral("code"), result.error().code}, {QStringLiteral("detail"), result.error().detail}});
        m_host->notify(m_host->translate(QStringLiteral("system-information.error.title")), m_host->translate(QStringLiteral("system-information.error.message")), AlertSeverity::Error);
        return;
    }

    // The window system owns what it knows about the screens, so they are read here rather than on the worker.
    result.value().displays = SystemCollection::connectedDisplays();
    m_snapshot = std::move(result.value());
    emit snapshotChanged();
}

} // namespace workpane::plugins::systeminformation
