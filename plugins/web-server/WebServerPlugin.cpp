#include "WebServerPlugin.h"

#include "WebServerInstance.h"
#include "WebServerTranslations.h"
#include "WebServerView.h"
#include "persistence/StoredValues.h"
#include "ui/Components.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaObject>
#include <QPromise>
#include <QSpinBox>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <algorithm>
#include <atomic>
#include <memory>
#include <utility>

namespace workpane::plugins::webserver {

constexpr int minimumSplitRatio = 150;
constexpr int maximumSplitRatio = 850;

constexpr auto pluginIdentifier = "web-server";

class WebServerRuntime final {
  public:
    WebServerRuntime() : m_server(new WebServerInstance()) {
        m_server->moveToThread(&m_workerThread);
        QObject::connect(&m_workerThread, &QThread::finished, m_server, &QObject::deleteLater);
        m_workerThread.setObjectName(QStringLiteral("workpaneWebServer"));
        m_workerThread.start();
    }

    WebServerRuntime(const WebServerRuntime&) = delete;
    WebServerRuntime& operator=(const WebServerRuntime&) = delete;

    ~WebServerRuntime() {
        // clang-format off
        const bool submitted = QMetaObject::invokeMethod(m_server, [this]() {
            m_server->stop();
            m_workerThread.quit();
        }, Qt::QueuedConnection);
        // clang-format on

        if (!submitted) {
            m_workerThread.quit();
        }

        m_workerThread.wait();
    }

    [[nodiscard]] QFuture<bool> start(const QString& root, const QString& host, quint16 port) {
        auto promise = std::make_shared<QPromise<bool>>();
        promise->start();
        const QFuture<bool> future = promise->future();
        // clang-format off
        const bool submitted = QMetaObject::invokeMethod(m_server, [this, root, host, port, promise]() {
            const bool started = m_server->start(root, host, port);
            m_running.store(started);
            m_port.store(m_server->port());
            promise->addResult(started);
            promise->finish();
        }, Qt::QueuedConnection);
        // clang-format on

        if (!submitted) {
            promise->addResult(false);
            promise->finish();
        }

        return future;
    }

    void stop() {
        if (!m_running.exchange(false)) {
            return;
        }

        m_port.store(0);
        QMetaObject::invokeMethod(m_server, &WebServerInstance::stop, Qt::QueuedConnection);
    }

    [[nodiscard]] bool running() const {
        return m_running.load();
    }

    [[nodiscard]] quint16 port() const {
        return m_port.load();
    }

    [[nodiscard]] const RequestLogModel& requestLog() const {
        return m_server->requestLog();
    }

    [[nodiscard]] QFuture<bool> clearRequestLog() {
        auto promise = std::make_shared<QPromise<bool>>();
        promise->start();
        const QFuture<bool> future = promise->future();
        // clang-format off
        const bool submitted = QMetaObject::invokeMethod(m_server, [this, promise]() {
            m_server->clearRequestLog();
            promise->addResult(true);
            promise->finish();
        }, Qt::QueuedConnection);
        // clang-format on

        if (!submitted) {
            promise->addResult(false);
            promise->finish();
        }

        return future;
    }

  private:
    QThread m_workerThread;
    WebServerInstance* m_server;
    std::atomic_bool m_running{false};
    std::atomic<quint16> m_port{0};
};

WebServerPlugin::WebServerPlugin() = default;

WebServerPlugin::~WebServerPlugin() {
    shutdown();
}

QString WebServerPlugin::id() const {
    return QString::fromLatin1(pluginIdentifier);
}

QStringList WebServerPlugin::dependencies() const {
    return {};
}

int WebServerPlugin::databaseSchemaVersion() const {
    return 1;
}

QString WebServerPlugin::titleKey() const {
    return QStringLiteral("web-server.plugin.title");
}

TranslationCatalog WebServerPlugin::translations() const {
    return translations::WebServerCatalog::catalog();
}

QString WebServerPlugin::styleSheet(const ui::Theme&) const {
    return QStringLiteral(R"(
        QDialog#webServerDialog { background: @window; }
        QLabel#dialogTitle { color: @text; font-size: 15px; font-weight: 600; }
        QWidget#webServerDialogStatus { background: @panel; border: none; }
        QLabel#webServerError { color: @dangerText; background: @dangerBackground; border-left: 2px solid @danger; padding: 7px 9px; }
        QTableWidget#webServerTable { selection-background-color: @hover; selection-color: @text; }
        QToolButton#fieldActionButton, QToolButton#serverActionButton { background: transparent; border: none; border-radius: @controlRadiuspx; }
        QToolButton#fieldActionButton:hover, QToolButton#serverActionButton:hover { background: @hover; }
    )");
}

QVector<NavigationItem> WebServerPlugin::navigationItems(const ui::Theme& theme) const {
    return {{QStringLiteral("manager"), QStringLiteral("web-server.navigation.server"), ui::IconCatalog::icon(ui::IconName::WebServer, theme), NavigationPlacement::Primary, NavigationOrder::Server}};
}

QVector<SettingsGroup> WebServerPlugin::settingsGroups() const {
    const SettingsSection general{QStringLiteral("general"), QStringLiteral("web-server.settings.general"), {QStringLiteral("web-server.settings.splitter-label")}};
    return {{QStringLiteral("web-server"), QStringLiteral("web-server.plugin.title"), {general}}};
}

Result<void> WebServerPlugin::initialize(PluginHost& host) {
    if (m_host != nullptr) {
        return Result<void>::failure({"web_server_already_initialized", "The Web Server plugin is already initialized", {}});
    }

    m_host = &host;
    m_asyncContext = std::make_unique<QObject>();
    const auto migrationResult = host.migrateDatabase({{1, {QStringLiteral("CREATE TABLE web_server_configurations(id TEXT PRIMARY KEY, name TEXT NOT NULL, root TEXT NOT NULL, bind_host TEXT NOT NULL, port INTEGER NOT NULL CHECK(port BETWEEN 1 AND 65535), terminal_id TEXT) STRICT"), QStringLiteral("CREATE UNIQUE INDEX web_server_configurations_terminal_index ON web_server_configurations(terminal_id) WHERE terminal_id IS NOT NULL")}}});

    if (!migrationResult.hasValue()) {
        shutdown();
        return migrationResult;
    }

    const auto result = restoreState();

    if (!result.hasValue()) {
        shutdown();
        return result;
    }

    const auto capability = host.provideCapability({QString::fromLatin1(serveFolderCapability)});

    if (!capability.hasValue()) {
        shutdown();
        return capability;
    }

    QTimer::singleShot(0, this, &WebServerPlugin::synchronizeTerminals);
    return result;
}

QWidget* WebServerPlugin::createNavigationView(const QString& itemId, QWidget* parent) {
    if (itemId != QStringLiteral("manager") || m_host == nullptr) {
        return nullptr;
    }

    return new WebServerView(*this, parent);
}

QWidget* WebServerPlugin::createSettingsSection(const QString& groupId, const QString& sectionId, QWidget* parent) {
    if (groupId != QStringLiteral("web-server") || sectionId != QStringLiteral("general") || m_host == nullptr) {
        return nullptr;
    }

    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    auto* form = ui::Components::settingsForm();
    auto* ratio = new QSpinBox(page);
    ratio->setRange(150, 850);
    ratio->setSingleStep(10);
    ratio->setValue(m_splitRatio);
    ui::Components::addSettingsRow(form, m_host->translate(QStringLiteral("web-server.settings.splitter-label")), ui::Components::stepperRow(ratio, m_host->theme(), page));
    layout->addLayout(form);
    layout->addStretch(1);

    // clang-format off
    connect(ratio, &QSpinBox::valueChanged, this, [this](int value) { setSplitRatio(value); });
    // clang-format on
    return page;
}

void WebServerPlugin::handleRequest(const QString&, const QString& topic, const QJsonObject& payload, PluginReply reply) {
    if (topic == QString::fromLatin1(serveFolderCapability) && SettingsReaders::hasExactKeys(payload, {QStringLiteral("path")}) && payload.value(QStringLiteral("path")).isString()) {
        openWebServerForFolder(payload.value(QStringLiteral("path")).toString(), reply);
        return;
    }

    reply(SettingsReaders::unhandledTopic(topic));
}

// One folder is served by one configuration, so asking again for a folder already configured opens the form of that server instead of a second one for the same root.
void WebServerPlugin::openWebServerForFolder(const QString& path, const PluginReply& reply) {
    const QString root = QFileInfo(path).canonicalFilePath();

    if (root.isEmpty() || !QFileInfo(root).isDir() || !QFileInfo(root).isReadable()) {
        reply(Result<QJsonObject>::failure({"web_server_root_invalid", host().translate(QStringLiteral("web-server.error.root-invalid")), path}));
        return;
    }

    QString serverId;

    for (const auto& [configuredId, server] : m_webServers) {
        if (QFileInfo(server.root).canonicalFilePath() == root) {
            serverId = configuredId;
        }
    }

    if (serverId.isEmpty()) {
        serverId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    // The view is revealed before the form runs, because the form owns the event loop until the user answers it.
    host().showNavigation(QStringLiteral("manager"));
    reply(Result<QJsonObject>::success({}));
    emit folderRequested(serverId, root);
}

// A port already taken by another configuration would fail the moment it starts, so the next free one is offered instead.
int WebServerPlugin::availablePort() const {
    int port = 8080;
    // clang-format off
    const auto taken = [&](int candidate) { return std::any_of(m_webServers.cbegin(), m_webServers.cend(), [candidate](const auto& entry) { return entry.second.port == candidate; }); };
    // clang-format on

    while (port < 65535 && taken(port)) {
        ++port;
    }

    return port;
}

void WebServerPlugin::handleEvent(const QString& senderPluginId, const QString& topic, const QJsonObject& payload) {
    if (senderPluginId != QStringLiteral("terminal")) {
        return;
    }

    if (topic == QStringLiteral("terminal.workspace.changed")) {
        if (!payload.isEmpty()) {
            host().notify(host().translate(QStringLiteral("web-server.plugin.title")), host().translate(QStringLiteral("web-server.error.message-invalid")), plugins::AlertSeverity::Error);
            return;
        }
        synchronizeTerminals();
        return;
    }

    if (topic == QStringLiteral("terminal.closed")) {
        if (!SettingsReaders::hasExactKeys(payload, {QStringLiteral("terminalId")}) || !payload.value(QStringLiteral("terminalId")).isString()) {
            host().notify(host().translate(QStringLiteral("web-server.plugin.title")), host().translate(QStringLiteral("web-server.error.message-invalid")), plugins::AlertSeverity::Error);
            return;
        }
        const QString terminalId = payload.value(QStringLiteral("terminalId")).toString();
        if (terminalId.isEmpty()) {
            host().notify(host().translate(QStringLiteral("web-server.plugin.title")), host().translate(QStringLiteral("web-server.error.message-invalid")), plugins::AlertSeverity::Error);
            return;
        }
        unlinkTerminal(terminalId);
    }
}

void WebServerPlugin::unlinkTerminal(const QString& terminalId) {
    m_terminals.remove(terminalId);

    if (m_activeTerminalId == terminalId) {
        m_activeTerminalId.clear();
    }

    for (auto& [serverId, server] : m_webServers) {
        if (server.terminalId != terminalId) {
            continue;
        }

        server.terminalId.clear();
        auto future = persistConfiguration(serverId);
        // clang-format off
        future.then(this, [this](Result<void> result) {
            if (!result.hasValue() && m_host != nullptr) {
                m_host->notify(m_host->translate(QStringLiteral("web-server.plugin.title")), m_host->translate(QStringLiteral("web-server.error.save-message")), plugins::AlertSeverity::Error);
            }
        });
        // clang-format on
    }

    emit webServerChanged({});
}

void WebServerPlugin::shutdown() {
    m_asyncContext.reset();

    for (auto& [serverId, server] : m_webServers) {
        Q_UNUSED(serverId);
        if (server.runtime != nullptr) {
            scheduleRuntimeCleanup(std::move(server.runtime));
        }
    }

    m_webServers.clear();

    for (auto& validation : m_rootValidationFutures) {
        validation.waitForFinished();
    }

    m_rootValidationFutures.clear();

    for (auto& cleanup : m_runtimeCleanupFutures) {
        cleanup.waitForFinished();
    }

    m_runtimeCleanupFutures.clear();
    m_pendingServerOperations.clear();
    m_cancelledServerOperations.clear();
    m_terminals.clear();
    m_activeTerminalId.clear();
    m_host = nullptr;
}

QFuture<Result<void>> WebServerPlugin::configureAndStartWebServer(const QString& serverId, const QString& name, const QString& root, const QString& bindHost, int port, const QString& terminalId) {
    if (m_pendingServerOperations.contains(serverId)) {
        return QtFuture::makeReadyValueFuture(startFailure(host().translate(QStringLiteral("web-server.error.operation-pending"))));
    }
    if (webServerRunning(serverId)) {
        return QtFuture::makeReadyValueFuture(startFailure(host().translate(QStringLiteral("web-server.error.stop-before-change"))));
    }
    if (serverId.isEmpty() || name.trimmed().isEmpty()) {
        return QtFuture::makeReadyValueFuture(startFailure(host().translate(QStringLiteral("web-server.error.name-invalid"))));
    }
    if (!terminalId.isEmpty() && terminalData(terminalId).isEmpty()) {
        return QtFuture::makeReadyValueFuture(startFailure(host().translate(QStringLiteral("web-server.manager.no-terminal"))));
    }
    if (port < 1 || port > 65535) {
        return QtFuture::makeReadyValueFuture(startFailure(host().translate(QStringLiteral("web-server.error.port-range"))));
    }

    QHostAddress address;

    if (!address.setAddress(bindHost)) {
        return QtFuture::makeReadyValueFuture(startFailure(host().translate(QStringLiteral("web-server.error.host-invalid")), bindHost));
    }

    m_pendingServerOperations.insert(serverId);
    emit webServerChanged(serverId);
    // clang-format off
    auto validation = QtConcurrent::run([root]() {
        const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
        return !canonicalRoot.isEmpty() && QFileInfo(canonicalRoot).isDir() && QFileInfo(canonicalRoot).isReadable() ? Result<QString>::success(canonicalRoot) : Result<QString>::failure(Error{"web_server_root_invalid", "The Web Server document root is invalid", root});
    });
    m_rootValidationFutures.removeIf([](const auto& future) { return future.isFinished(); });
    m_rootValidationFutures.append(validation);
    auto operation = validation.then(m_asyncContext.get(), [this, serverId, name = name.trimmed(), terminalId, bindHost, port](Result<QString> rootResult) {
        if (m_cancelledServerOperations.contains(serverId)) {
            return QtFuture::makeReadyValueFuture(Result<void>::failure(Error{"web_server_start_cancelled", "The Web Server start was cancelled", serverId}));
        }
        if (!rootResult.hasValue()) {
            return QtFuture::makeReadyValueFuture(startFailure(host().translate(QStringLiteral("web-server.error.root-invalid")), rootResult.error().detail));
        }
        const auto linkedServerId = serverIdForTerminal(terminalId);
        if (!terminalId.isEmpty() && !linkedServerId.isEmpty() && linkedServerId != serverId) {
            return QtFuture::makeReadyValueFuture(startFailure(host().translate(QStringLiteral("web-server.error.configuration-unavailable")), terminalId));
        }
        const auto existing = m_webServers.find(serverId);
        const auto previous = existing == m_webServers.end() ? std::shared_ptr<WebServer>{} : std::make_shared<WebServer>(std::move(existing->second));
        m_webServers.insert_or_assign(serverId, WebServer{nullptr, name, rootResult.value(), bindHost, terminalId, static_cast<quint16>(port)});
        auto persistFuture = persistConfiguration(serverId);
        return persistFuture.then(m_asyncContext.get(), [this, serverId, previous](Result<void> persistResult) {
            if (!persistResult.hasValue()) {
                if (previous != nullptr) {
                    m_webServers.insert_or_assign(serverId, std::move(*previous));
                } else {
                    m_webServers.erase(serverId);
                }
                host().notify(host().translate(QStringLiteral("web-server.plugin.title")), host().translate(QStringLiteral("web-server.error.save-message")), plugins::AlertSeverity::Error);
                return QtFuture::makeReadyValueFuture(persistResult);
            }
            emit webServerChanged(serverId);
            if (m_cancelledServerOperations.contains(serverId)) {
                return QtFuture::makeReadyValueFuture(Result<void>::failure(Error{"web_server_start_cancelled", "The Web Server start was cancelled", serverId}));
            }
            return startRuntime(serverId);
        }).unwrap();
    }).unwrap();
    return operation.then(m_asyncContext.get(), [this, serverId](Result<void> result) {
        m_pendingServerOperations.remove(serverId);
        m_cancelledServerOperations.remove(serverId);
        emit webServerChanged(serverId);
        return result;
    });
    // clang-format on
}

QFuture<Result<void>> WebServerPlugin::startWebServer(const QString& serverId) {
    if (!webServerConfigured(serverId)) {
        return QtFuture::makeReadyValueFuture(startFailure(host().translate(QStringLiteral("web-server.error.configure-first"))));
    }
    if (webServerRunning(serverId)) {
        return QtFuture::makeReadyValueFuture(Result<void>::success());
    }

    return startConfiguredWebServer(serverId);
}

QFuture<Result<void>> WebServerPlugin::removeWebServer(const QString& serverId) {
    if (m_pendingServerOperations.contains(serverId)) {
        return QtFuture::makeReadyValueFuture(removeFailure("web_server_operation_pending", QStringLiteral("web-server.error.operation-pending"), serverId));
    }

    stopWebServer(serverId);
    m_pendingServerOperations.insert(serverId);
    emit webServerChanged(serverId);
    const auto server = m_webServers.find(serverId);

    if (server == m_webServers.end()) {
        m_pendingServerOperations.remove(serverId);
        return QtFuture::makeReadyValueFuture(removeFailure("web_server_configuration_missing", QStringLiteral("web-server.error.configuration-unavailable"), serverId));
    }

    auto removedServer = std::make_shared<WebServer>(std::move(server->second));
    m_webServers.erase(serverId);
    emit webServerChanged(serverId);
    auto future = host().executeDatabase(QStringLiteral("DELETE FROM web_server_configurations WHERE id = ?"), {serverId});
    // clang-format off
    return future.then(m_asyncContext.get(), [this, serverId, removedServer](Result<void> result) {
        if (!result.hasValue()) {
            m_webServers.insert_or_assign(serverId, std::move(*removedServer));
            emit webServerChanged(serverId);
            host().notify(host().translate(QStringLiteral("web-server.plugin.title")), host().translate(QStringLiteral("web-server.error.remove-message")), plugins::AlertSeverity::Error);
        }
        m_pendingServerOperations.remove(serverId);
        m_cancelledServerOperations.remove(serverId);
        emit webServerChanged(serverId);
        return result;
    });
    // clang-format on
}

void WebServerPlugin::stopWebServer(const QString& serverId) {
    if (m_pendingServerOperations.contains(serverId)) {
        m_cancelledServerOperations.insert(serverId);
    }

    const auto server = m_webServers.find(serverId);

    if (server == m_webServers.end() || server->second.runtime == nullptr) {
        return;
    }

    scheduleRuntimeCleanup(std::move(server->second.runtime));
    emit webServerChanged(serverId);
    host().notify(host().translate(QStringLiteral("web-server.notification.stopped")), host().translate(QStringLiteral("web-server.notification.stopped-detail")).arg(server->second.name), plugins::AlertSeverity::Success);
}

QUrl WebServerPlugin::webServerAddress(const QString& serverId) const {
    const auto server = m_webServers.find(serverId);

    if (server == m_webServers.end() || server->second.runtime == nullptr || !server->second.runtime->running()) {
        return {};
    }

    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(server->second.host);
    url.setPort(server->second.runtime->port());
    url.setPath(QStringLiteral("/"));
    return url;
}

bool WebServerPlugin::openWebServer(const QString& serverId) const {
    const QUrl url = webServerAddress(serverId);
    return url.isValid() && QDesktopServices::openUrl(url);
}

void WebServerPlugin::openWebServerInBrowser(const QString& serverId, QObject& callbackContext, PluginReply reply) {
    const QUrl url = webServerAddress(serverId);

    if (!url.isValid()) {
        reply(Result<QJsonObject>::failure({"web_server_not_running", "The web server is not running", serverId}));
        return;
    }

    m_host->invokeCapability(QString::fromLatin1(openPageCapability), {{QStringLiteral("url"), url.toString()}}, callbackContext, std::move(reply));
}

QVariantList WebServerPlugin::configuredWebServers() const {
    QVariantList servers;

    for (const auto& [serverId, server] : m_webServers) {
        servers.append(QVariantMap{{QStringLiteral("serverId"), serverId}, {QStringLiteral("name"), server.name}, {QStringLiteral("root"), server.root}, {QStringLiteral("host"), server.host}, {QStringLiteral("port"), webServerPort(serverId)}, {QStringLiteral("terminalId"), server.terminalId}, {QStringLiteral("running"), webServerRunning(serverId)}, {QStringLiteral("pending"), webServerOperationPending(serverId)}});
    }
    // clang-format off
    std::sort(servers.begin(), servers.end(), [](const QVariant& left, const QVariant& right) { return QString::localeAwareCompare(left.toMap().value(QStringLiteral("name")).toString(), right.toMap().value(QStringLiteral("name")).toString()) < 0; });
    // clang-format on
    return servers;
}

PluginRequestLogBatch WebServerPlugin::requestLogEntriesSince(const QString& serverId, quint64 cursor, int maximumEntries) const {
    PluginRequestLogBatch output{cursor, {}};
    const auto server = m_webServers.find(serverId);

    if (server == m_webServers.end() || server->second.runtime == nullptr || maximumEntries <= 0) {
        return output;
    }

    const auto batch = server->second.runtime->requestLog().entriesSince(cursor, maximumEntries);
    output.cursor = batch.cursor;

    for (const auto& entry : batch.entries) {
        output.entries.append(QVariantMap{{QStringLiteral("timestamp"), persistence::StoredValues::storedTimestamp(entry.timestamp)}, {QStringLiteral("method"), entry.method}, {QStringLiteral("path"), entry.path}, {QStringLiteral("status"), entry.status}, {QStringLiteral("durationMs"), entry.durationMilliseconds}, {QStringLiteral("responseBytes"), entry.responseBytes}, {QStringLiteral("remoteAddress"), entry.remoteAddress}});
    }

    return output;
}

QFuture<bool> WebServerPlugin::clearRequestLog(const QString& serverId) {
    const auto server = m_webServers.find(serverId);

    if (server == m_webServers.end() || server->second.runtime == nullptr) {
        return QtFuture::makeReadyValueFuture(false);
    }

    return server->second.runtime->clearRequestLog();
}

bool WebServerPlugin::webServerConfigured(const QString& serverId) const {
    return m_webServers.contains(serverId);
}

bool WebServerPlugin::webServerRunning(const QString& serverId) const {
    const auto server = m_webServers.find(serverId);
    return server != m_webServers.end() && server->second.runtime != nullptr && server->second.runtime->running();
}

bool WebServerPlugin::webServerOperationPending(const QString& serverId) const {
    return m_pendingServerOperations.contains(serverId);
}

quint16 WebServerPlugin::webServerPort(const QString& serverId) const {
    const auto server = m_webServers.find(serverId);

    if (server == m_webServers.end()) {
        return 0;
    }

    return server->second.runtime == nullptr ? server->second.port : server->second.runtime->port();
}

QString WebServerPlugin::webServerName(const QString& serverId) const {
    const auto server = m_webServers.find(serverId);
    return server == m_webServers.end() ? QString{} : server->second.name;
}

QString WebServerPlugin::webServerHost(const QString& serverId) const {
    const auto server = m_webServers.find(serverId);
    return server == m_webServers.end() ? QString{} : server->second.host;
}

QString WebServerPlugin::webServerRoot(const QString& serverId) const {
    const auto server = m_webServers.find(serverId);
    return server == m_webServers.end() ? QString{} : server->second.root;
}

QString WebServerPlugin::webServerTerminalId(const QString& serverId) const {
    const auto server = m_webServers.find(serverId);
    return server == m_webServers.end() ? QString{} : server->second.terminalId;
}

QString WebServerPlugin::serverIdForTerminal(const QString& terminalId) const {
    if (terminalId.isEmpty()) {
        return {};
    }

    for (const auto& [serverId, server] : m_webServers) {
        if (server.terminalId == terminalId) {
            return serverId;
        }
    }

    return {};
}

QString WebServerPlugin::activeTerminalId() const {
    return m_activeTerminalId;
}

QVariantMap WebServerPlugin::terminalData(const QString& terminalId) const {
    return m_terminals.value(terminalId);
}

int WebServerPlugin::splitRatio() const {
    return m_splitRatio;
}

void WebServerPlugin::setSplitRatio(int ratio) {
    if (ratio < minimumSplitRatio || ratio > maximumSplitRatio || ratio == m_splitRatio) {
        return;
    }

    m_splitRatio = ratio;
    const quint64 revision = ++m_splitRatioRevision;
    auto future = host().saveSettings({{QStringLiteral("splitRatio"), m_splitRatio}});
    // clang-format off
    future.then(m_asyncContext.get(), [this, ratio, revision](Result<void> result) {
        if (result.hasValue()) {
            if (revision > m_committedSplitRatioRevision) {
                m_committedSplitRatio = ratio;
                m_committedSplitRatioRevision = revision;
            }
            return;
        }
        if (revision == m_splitRatioRevision) {
            m_splitRatio = m_committedSplitRatio;
        }
        host().notify(host().translate(QStringLiteral("web-server.plugin.title")), host().translate(QStringLiteral("web-server.error.save-message")), plugins::AlertSeverity::Error);
    });
    // clang-format on
}

PluginHost& WebServerPlugin::host() const {
    return *m_host;
}

Result<void> WebServerPlugin::restoreState() {
    plugins::SettingsReader reader(host().settings());
    int ratio = m_splitRatio;
    reader.readInteger(QStringLiteral("splitRatio"), ratio);

    if (ratio >= minimumSplitRatio && ratio <= maximumSplitRatio) {
        m_splitRatio = ratio;
    }

    m_committedSplitRatio = m_splitRatio;

    const auto configurationRows = host().queryBootstrapDatabase(QStringLiteral("SELECT id, name, root, bind_host, port, terminal_id FROM web_server_configurations ORDER BY name, id"));

    if (!configurationRows.hasValue()) {
        return Result<void>::failure(configurationRows.error());
    }

    for (const auto& row : configurationRows.value()) {
        const QString serverId = row.value(QStringLiteral("id")).toString();
        const QString name = row.value(QStringLiteral("name")).toString();
        const QString root = row.value(QStringLiteral("root")).toString();
        const QString bindHost = row.value(QStringLiteral("bind_host")).toString();
        const QString terminalId = row.value(QStringLiteral("terminal_id")).toString();
        qint64 port = 0;
        QHostAddress address;
        if (serverId.isEmpty() || name.trimmed().isEmpty() || name != name.trimmed() || root.isEmpty() || !QDir::isAbsolutePath(root) || !address.setAddress(bindHost) || !persistence::StoredValues::readStoredInteger(row.value(QStringLiteral("port")), port) || port < 1 || port > 65535 || m_webServers.contains(serverId) || (!terminalId.isEmpty() && !serverIdForTerminal(terminalId).isEmpty())) {
            return Result<void>::failure({"web_server_state_invalid", "A Web Server configuration is invalid", serverId});
        }
        m_webServers.insert_or_assign(serverId, WebServer{nullptr, name, root, bindHost, terminalId, static_cast<quint16>(port)});
    }

    return Result<void>::success();
}

void WebServerPlugin::synchronizeTerminals() {
    if (m_host == nullptr || !m_host->capabilityAvailable(QString::fromLatin1(terminalSnapshotCapability))) {
        return;
    }

    // clang-format off
    const auto applySnapshot = [this](Result<QJsonObject> result) {
        if (!result.hasValue()) {
            host().notify(host().translate(QStringLiteral("web-server.plugin.title")), host().translate(QStringLiteral("web-server.error.terminal-message")), plugins::AlertSeverity::Error);
            return;
        }

        const QJsonObject snapshot = result.value();
        if (!SettingsReaders::hasExactKeys(snapshot, {QStringLiteral("activeTerminalId"), QStringLiteral("terminals")}) || !snapshot.value(QStringLiteral("activeTerminalId")).isString() || !snapshot.value(QStringLiteral("terminals")).isArray()) {
            host().notify(host().translate(QStringLiteral("web-server.plugin.title")), host().translate(QStringLiteral("web-server.error.message-invalid")), plugins::AlertSeverity::Error);
            return;
        }

        QHash<QString, QVariantMap> terminals;
        const QJsonArray values = snapshot.value(QStringLiteral("terminals")).toArray();
        for (const auto& value : values) {
            if (!value.isObject()) {
                host().notify(host().translate(QStringLiteral("web-server.plugin.title")), host().translate(QStringLiteral("web-server.error.message-invalid")), plugins::AlertSeverity::Error);
                return;
            }
            const QJsonObject terminal = value.toObject();
            if (!SettingsReaders::hasExactKeys(terminal, {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("cwd")}) || !terminal.value(QStringLiteral("id")).isString() || !terminal.value(QStringLiteral("name")).isString() || !terminal.value(QStringLiteral("cwd")).isString()) {
                host().notify(host().translate(QStringLiteral("web-server.plugin.title")), host().translate(QStringLiteral("web-server.error.message-invalid")), plugins::AlertSeverity::Error);
                return;
            }
            const QString terminalId = terminal.value(QStringLiteral("id")).toString();
            if (terminalId.isEmpty() || terminals.contains(terminalId)) {
                host().notify(host().translate(QStringLiteral("web-server.plugin.title")), host().translate(QStringLiteral("web-server.error.message-invalid")), plugins::AlertSeverity::Error);
                return;
            }
            terminals.insert(terminalId, {{QStringLiteral("id"), terminalId}, {QStringLiteral("name"), terminal.value(QStringLiteral("name")).toString()}, {QStringLiteral("cwd"), terminal.value(QStringLiteral("cwd")).toString()}});
        }
        const QString activeTerminalId = snapshot.value(QStringLiteral("activeTerminalId")).toString();
        if (!activeTerminalId.isEmpty() && !terminals.contains(activeTerminalId)) {
            host().notify(host().translate(QStringLiteral("web-server.plugin.title")), host().translate(QStringLiteral("web-server.error.message-invalid")), plugins::AlertSeverity::Error);
            return;
        }

        m_terminals = std::move(terminals);
        m_activeTerminalId = activeTerminalId;

        QStringList unavailableTerminalIds;
        for (const auto& [serverId, server] : m_webServers) {
            Q_UNUSED(serverId);
            if (!server.terminalId.isEmpty() && !m_terminals.contains(server.terminalId)) {
                unavailableTerminalIds.append(server.terminalId);
            }
        }
        for (const auto& terminalId : unavailableTerminalIds) {
            unlinkTerminal(terminalId);
        }
        emit webServerChanged({});
    };
    // clang-format on
    host().invokeCapability(QString::fromLatin1(terminalSnapshotCapability), {}, *this, applySnapshot);
}

QFuture<Result<void>> WebServerPlugin::startConfiguredWebServer(const QString& serverId) {
    if (m_pendingServerOperations.contains(serverId)) {
        return QtFuture::makeReadyValueFuture(startFailure(host().translate(QStringLiteral("web-server.error.operation-pending"))));
    }

    m_pendingServerOperations.insert(serverId);
    emit webServerChanged(serverId);
    auto future = startRuntime(serverId);
    // clang-format off
    return future.then(m_asyncContext.get(), [this, serverId](Result<void> result) {
        m_pendingServerOperations.remove(serverId);
        m_cancelledServerOperations.remove(serverId);
        emit webServerChanged(serverId);
        return result;
    });
    // clang-format on
}

QFuture<Result<void>> WebServerPlugin::startRuntime(const QString& serverId) {
    const auto server = m_webServers.find(serverId);

    if (server == m_webServers.end()) {
        return QtFuture::makeReadyValueFuture(startFailure(host().translate(QStringLiteral("web-server.error.configuration-unavailable"))));
    }

    server->second.runtime = std::make_unique<WebServerRuntime>();
    WebServerRuntime* runtime = server->second.runtime.get();
    const QString root = server->second.root;
    const QString bindHost = server->second.host;
    const quint16 port = server->second.port;
    auto future = runtime->start(root, bindHost, port);
    // clang-format off
    return future.then(m_asyncContext.get(), [this, serverId, runtime, root, bindHost, port](bool started) {
        const auto current = m_webServers.find(serverId);
        if (m_cancelledServerOperations.contains(serverId) || current == m_webServers.end() || current->second.runtime.get() != runtime) {
            return Result<void>::failure({"web_server_start_cancelled", "The Web Server start was cancelled", serverId});
        }
        if (!started) {
            scheduleRuntimeCleanup(std::move(current->second.runtime));
            return startFailure(host().translate(QStringLiteral("web-server.error.bind")).arg(bindHost).arg(port));
        }
        emit webServerChanged(serverId);
        host().notify(host().translate(QStringLiteral("web-server.notification.started")), host().translate(QStringLiteral("web-server.notification.serving")).arg(root, bindHost).arg(current->second.runtime->port()), plugins::AlertSeverity::Success);
        return Result<void>::success();
    });
    // clang-format on
}

QFuture<Result<void>> WebServerPlugin::persistConfiguration(const QString& serverId) {
    const auto server = m_webServers.find(serverId);

    if (server == m_webServers.end()) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"web_server_configuration_missing", "The Web Server configuration is unavailable", serverId}));
    }

    const QVariant terminalId = server->second.terminalId.isEmpty() ? QVariant{} : QVariant(server->second.terminalId);
    return host().executeDatabase(QStringLiteral("INSERT INTO web_server_configurations(id, name, root, bind_host, port, terminal_id) VALUES(?, ?, ?, ?, ?, ?) ON CONFLICT(id) DO UPDATE SET name = excluded.name, root = excluded.root, bind_host = excluded.bind_host, port = excluded.port, terminal_id = excluded.terminal_id"), {serverId, server->second.name, server->second.root, server->second.host, server->second.port, terminalId});
}

// A reader who confirmed a removal is told why it did not happen, because an action that does nothing and explains nothing reads as a product that is broken.
Result<void> WebServerPlugin::removeFailure(const char* code, const QString& messageKey, const QString& detail) {
    const QString message = host().translate(messageKey);
    host().notify(host().translate(QStringLiteral("web-server.plugin.title")), message, plugins::AlertSeverity::Error);
    return Result<void>::failure({code, message, detail});
}

Result<void> WebServerPlugin::startFailure(const QString& message, const QString& detail) {
    host().notify(host().translate(QStringLiteral("web-server.error.start-title")), message, plugins::AlertSeverity::Error);
    return Result<void>::failure({"web_server_start_failed", message, detail});
}

void WebServerPlugin::scheduleRuntimeCleanup(std::unique_ptr<WebServerRuntime> runtime) {
    if (runtime == nullptr) {
        return;
    }
    // clang-format off
    m_runtimeCleanupFutures.removeIf([](const auto& future) { return future.isFinished(); });
    m_runtimeCleanupFutures.append(QtConcurrent::run([runtime = std::move(runtime)]() mutable { runtime.reset(); }));
    // clang-format on
}

} // namespace workpane::plugins::webserver
