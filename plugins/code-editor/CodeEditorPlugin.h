#pragma once

#include "CodeColorScheme.h"
#include "CodeEditorRepository.h"
#include "LanguageRegistry.h"
#include "plugins/PluginInterface.h"
#include "ui/ApplicationShortcuts.h"

#include <QFuture>
#include <QTimer>

#include <memory>

namespace workpane::plugins::codeeditor {

class CodeEditorPlugin final : public QObject, public PluginInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID WorkpanePluginInterface_iid)
    Q_INTERFACES(workpane::plugins::PluginInterface)

  public:
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
    [[nodiscard]] QWidget* createAppearanceSection(QWidget* parent);
    [[nodiscard]] QWidget* createFilesSection(QWidget* parent);
    [[nodiscard]] QWidget* createLanguageServersSection(QWidget* parent);
    void handleRequest(const QString& senderPluginId, const QString& topic, const QJsonObject& payload, PluginReply reply) override;
    void shutdown() override;

    [[nodiscard]] const QVector<CodeWorkspaceState>& workspaces() const;
    [[nodiscard]] bool wordWrap() const;
    [[nodiscard]] TextCharset defaultCharset() const;
    [[nodiscard]] CodeEditorFont editorFont() const;
    [[nodiscard]] const CodeColorScheme& colorScheme() const;
    void setWordWrap(bool enabled);
    void setDefaultCharset(TextCharset charset);
    void setLanguageServersEnabled(bool enabled);
    void setEditorFontFamily(const QString& family);
    void setEditorFontSize(int pointSize);
    void stepEditorFontSize(ui::ContentFontStep step);
    void setColorScheme(const QString& schemeId);
    [[nodiscard]] const QVector<ResolvedLanguageServer>& languageServers() const;
    [[nodiscard]] QVector<ResolvedLanguageServer> activeLanguageServers() const;
    [[nodiscard]] bool languageServersEnabled() const;
    [[nodiscard]] PluginHost& host() const;
    [[nodiscard]] Result<QString> openWorkspace(const QString& rootPath);
    [[nodiscard]] Result<void> closeWorkspace(const QString& workspaceId);
    [[nodiscard]] Result<void> activateWorkspace(const QString& workspaceId);
    [[nodiscard]] Result<void> moveWorkspace(int from, int to);
    [[nodiscard]] Result<void> updateWorkspace(CodeWorkspaceState workspace);
    void refreshLanguageServers();

  signals:
    void workspacesChanged();
    void wordWrapChanged();
    void editorFontChanged();
    void colorSchemeChanged();
    void languageServersChanged();
    void languageServerDiscoveryStateChanged(bool running);

  private:
    void persistSettings();
    void schedulePersistence();
    void persistState();
    [[nodiscard]] CodeWorkspaceState* workspace(const QString& workspaceId);

    PluginHost* m_host{nullptr};
    std::unique_ptr<CodeEditorRepository> m_repository;
    QVector<CodeWorkspaceState> m_workspaces;
    QVector<CodeWorkspaceState> m_committedWorkspaces;
    CodeEditorSettings m_settings;
    CodeColorScheme m_colorScheme;
    CodeEditorSettings m_committedSettings;
    quint64 m_settingsRevision{0};
    quint64 m_committedSettingsRevision{0};
    QVector<ResolvedLanguageServer> m_languageServers;
    QFuture<QVector<ResolvedLanguageServer>> m_languageServerDiscoveryFuture;
    std::unique_ptr<QObject> m_asyncContext;
    QTimer m_persistenceTimer;
    quint64 m_stateRevision{0};
    quint64 m_persistedRevision{0};
    bool m_persistenceInFlight{false};
    bool m_languageServerDiscoveryInFlight{false};
};

} // namespace workpane::plugins::codeeditor
