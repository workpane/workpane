#pragma once

#include "plugins/PluginInterface.h"

#include <QObject>
#include <QSet>
#include <QUrl>

#include <map>
#include <memory>

namespace workpane::plugins::webserver {

struct PluginRequestLogBatch final {
    quint64 cursor{};
    QVariantList entries;
};

class WebServerRuntime;

class WebServerPlugin final : public QObject, public PluginInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID WorkpanePluginInterface_iid)
    Q_INTERFACES(workpane::plugins::PluginInterface)

  public:
    WebServerPlugin();
    ~WebServerPlugin() override;

    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString titleKey() const override;
    [[nodiscard]] QStringList dependencies() const override;
    [[nodiscard]] int databaseSchemaVersion() const override;
    [[nodiscard]] TranslationCatalog translations() const override;
    [[nodiscard]] QString styleSheet(const ui::Theme& theme) const override;
    [[nodiscard]] QVector<NavigationItem> navigationItems(const ui::Theme& theme) const override;
    [[nodiscard]] QVector<SettingsGroup> settingsGroups() const override;
    [[nodiscard]] Result<void> initialize(PluginHost& host) override;
    [[nodiscard]] QWidget* createNavigationView(const QString& itemId, QWidget* parent) override;
    [[nodiscard]] QWidget* createSettingsSection(const QString& groupId, const QString& sectionId, QWidget* parent) override;
    void handleRequest(const QString& senderPluginId, const QString& topic, const QJsonObject& payload, PluginReply reply) override;
    void handleEvent(const QString& senderPluginId, const QString& topic, const QJsonObject& payload) override;
    void shutdown() override;

    [[nodiscard]] QFuture<Result<void>> configureAndStartWebServer(const QString& serverId, const QString& name, const QString& root, const QString& host, int port, const QString& terminalId = {});
    [[nodiscard]] QFuture<Result<void>> startWebServer(const QString& serverId);
    [[nodiscard]] QFuture<Result<void>> removeWebServer(const QString& serverId);
    void stopWebServer(const QString& serverId);
    [[nodiscard]] QUrl webServerAddress(const QString& serverId) const;
    [[nodiscard]] bool openWebServer(const QString& serverId) const;
    void openWebServerInBrowser(const QString& serverId, QObject& callbackContext, PluginReply reply);
    [[nodiscard]] QVariantList configuredWebServers() const;
    [[nodiscard]] PluginRequestLogBatch requestLogEntriesSince(const QString& serverId, quint64 cursor, int maximumEntries) const;
    [[nodiscard]] QFuture<bool> clearRequestLog(const QString& serverId);
    [[nodiscard]] bool webServerConfigured(const QString& serverId) const;
    [[nodiscard]] bool webServerRunning(const QString& serverId) const;
    [[nodiscard]] bool webServerOperationPending(const QString& serverId) const;
    [[nodiscard]] quint16 webServerPort(const QString& serverId) const;
    [[nodiscard]] QString webServerName(const QString& serverId) const;
    [[nodiscard]] QString webServerHost(const QString& serverId) const;
    [[nodiscard]] QString webServerRoot(const QString& serverId) const;
    [[nodiscard]] QString webServerTerminalId(const QString& serverId) const;
    [[nodiscard]] QString serverIdForTerminal(const QString& terminalId) const;
    [[nodiscard]] QString activeTerminalId() const;
    [[nodiscard]] QVariantMap terminalData(const QString& terminalId) const;
    // A port already taken by another configuration would fail the moment it starts, so the form suggests the next free one.
    [[nodiscard]] int availablePort() const;
    [[nodiscard]] int splitRatio() const;
    void setSplitRatio(int ratio);
    [[nodiscard]] PluginHost& host() const;

  signals:
    void webServerChanged(const QString& serverId);
    // The form that configures a server belongs to the view, so the plugin asks for it instead of building one.
    void folderRequested(const QString& serverId, const QString& root);

  private:
    struct WebServer final {
        std::unique_ptr<WebServerRuntime> runtime;
        QString name;
        QString root;
        QString host;
        QString terminalId;
        quint16 port{};
    };

    [[nodiscard]] Result<void> restoreState();
    void synchronizeTerminals();
    void unlinkTerminal(const QString& terminalId);
    void openWebServerForFolder(const QString& path, const PluginReply& reply);
    [[nodiscard]] QFuture<Result<void>> startConfiguredWebServer(const QString& serverId);
    [[nodiscard]] QFuture<Result<void>> startRuntime(const QString& serverId);
    [[nodiscard]] QFuture<Result<void>> persistConfiguration(const QString& serverId);
    [[nodiscard]] Result<void> removeFailure(const char* code, const QString& messageKey, const QString& detail);
    [[nodiscard]] Result<void> startFailure(const QString& message, const QString& detail = {});
    void scheduleRuntimeCleanup(std::unique_ptr<WebServerRuntime> runtime);

    PluginHost* m_host{nullptr};
    std::unique_ptr<QObject> m_asyncContext;
    std::map<QString, WebServer> m_webServers;
    QSet<QString> m_pendingServerOperations;
    QSet<QString> m_cancelledServerOperations;
    QHash<QString, QVariantMap> m_terminals;
    QString m_activeTerminalId;
    QVector<QFuture<void>> m_runtimeCleanupFutures;
    QVector<QFuture<Result<QString>>> m_rootValidationFutures;
    int m_splitRatio{420};
    int m_committedSplitRatio{420};
    quint64 m_splitRatioRevision{0};
    quint64 m_committedSplitRatioRevision{0};
};

} // namespace workpane::plugins::webserver
