#pragma once

#include "TerminalWorkspaceRepository.h"
#include "domain/TerminalTheme.h"
#include "domain/Workspace.h"
#include "plugins/PluginInterface.h"
#include "terminal/TerminalSession.h"

#include <QAbstractListModel>
#include <QString>

#include <map>
#include <memory>

namespace workpane::plugins::terminalplugin::workspace {

class WorkspaceManager final : public QAbstractListModel {
    Q_OBJECT

  public:
    enum class ShutdownMode { Persist, Discard };

    enum Role { NameRole = Qt::UserRole + 1 };

    explicit WorkspaceManager(plugins::terminalplugin::TerminalWorkspaceRepository& repository, plugins::PluginHost& host, QString historyPath, domain::TerminalTheme terminalTheme, terminalcore::PtyBackendFactory backendFactory, QObject* parent = nullptr);
    ~WorkspaceManager() override;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;

    [[nodiscard]] int currentTabIndex() const;
    void setCurrentTabIndex(int index);
    [[nodiscard]] QString currentTabId() const;
    [[nodiscard]] QVariantList currentSlots() const;
    [[nodiscard]] QVariantList currentShelf() const;
    [[nodiscard]] QString currentPresetId() const;
    [[nodiscard]] QString currentFocusedSessionId() const;
    [[nodiscard]] QString sessionName(const QString& sessionId) const;
    [[nodiscard]] int currentLayoutColumns() const;
    [[nodiscard]] QString currentCwd() const;
    [[nodiscard]] int terminalCount() const;
    [[nodiscard]] QVariantList allSessions() const;

    [[nodiscard]] Result<void> initialize();
    void shutdown(ShutdownMode mode = ShutdownMode::Persist);
    void setTerminalTheme(const QString& themeId);
    void setClipboardWriteAllowed(bool allowed);

    [[nodiscard]] QObject* sessionObject(const QString& sessionId) const;
    [[nodiscard]] QVariantMap sessionData(const QString& sessionId) const;
    [[nodiscard]] QVariantList layoutPresets() const;
    void createTab();
    void moveTab(int from, int to);
    void renameTab(int index, const QString& name);
    void closeTab(int index);
    QString createTerminal(int slotIndex = -1);
    void closeTerminal(QString sessionId);
    void changeLayout(const QString& presetId);
    void assignToSlot(const QString& sessionId, int slotIndex);
    void activateShelvedSession(const QString& sessionId);
    void moveToShelf(const QString& sessionId, int shelfIndex = -1);
    void focusSession(const QString& sessionId);

  signals:
    void currentTabChanged();
    void sessionsChanged();
    void sessionNameChanged(const QString& sessionId);
    void layoutChanged();
    void focusedSessionChanged(const QString& sessionId);
    void terminalClosing(const QString& sessionId);
    void notificationRequested(const QString& title, const QString& message, bool error);

  private slots:
    void persistRuntimeSession();
    void notifySessionNameChanged();
    void reportSessionError(const Error& error);

  private:
    [[nodiscard]] domain::MainTab* currentTab();
    [[nodiscard]] const domain::MainTab* currentTab() const;
    [[nodiscard]] domain::TerminalSessionState* sessionState(const QString& sessionId);
    [[nodiscard]] terminalcore::TerminalSession* runtimeSession(const QString& sessionId) const;
    [[nodiscard]] domain::Workspace createDefaultWorkspace() const;
    [[nodiscard]] domain::TerminalSessionState createSessionState() const;
    [[nodiscard]] Result<void> startRuntimeSessions();
    [[nodiscard]] int tabIndexForSession(const QString& sessionId) const;
    void normalizeFocusedSession(domain::MainTab& tab);
    void removeSessionFromAllTabs(const QString& sessionId);
    void persist();
    void notifyTabChanged(int index);

    plugins::terminalplugin::TerminalWorkspaceRepository& m_repository;
    plugins::PluginHost& m_host;
    QString m_historyPath;
    domain::TerminalTheme m_terminalTheme;
    bool m_clipboardWriteAllowed{false};
    terminalcore::PtyBackendFactory m_backendFactory;
    domain::Workspace m_workspace;
    std::map<QString, std::unique_ptr<terminalcore::TerminalSession>> m_runtimeSessions;
    int m_currentTabIndex{0};
    bool m_shuttingDown{false};
};

} // namespace workpane::plugins::terminalplugin::workspace
