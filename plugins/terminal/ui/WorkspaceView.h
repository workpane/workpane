#pragma once

#include "TerminalSettingsStore.h"
#include "plugins/PluginInterface.h"
#include "workspace/WorkspaceManager.h"

#include <QVariantList>
#include <QWidget>

class QHBoxLayout;
class QScrollArea;
class QVBoxLayout;

namespace workpane::plugins::terminalplugin {

class TerminalPane;

class WorkspaceView final : public QWidget {
    Q_OBJECT

  public:
    WorkspaceView(plugins::terminalplugin::workspace::WorkspaceManager& manager, plugins::terminalplugin::TerminalSettingsStore& settings, plugins::PluginHost& host, QWidget* parent = nullptr);
    void focusTerminal(const QString& sessionId);
    void focusCurrentTerminal();

  signals:
    void closeTerminalRequested(const QString& sessionId, const QString& name);
    void interactionError(const Error& error);

  private slots:
    void synchronize();
    void updateSelection(const QString& sessionId = {});
    void createTerminalInSlot();
    void selectShelfSession(const QString& sessionId);
    void closeShelfSession(const QString& sessionId);
    void selectSession(const QString& sessionId);
    void requestFocusMode(const QString& sessionId);
    void assignSessionToSlot(const QString& sessionId, int slotIndex);
    void moveSessionToShelf(const QString& sessionId);
    void applyTerminalFont();
    void applyPasteConfirmation(bool enabled);

  private:
    void rebuildWorkspace();
    void rebuildShelf();
    void retireWorkspaceHosts();
    [[nodiscard]] QWidget* createEmptySlot(int slotIndex);
    [[nodiscard]] TerminalPane* createTerminalPane(const QString& sessionId);

    plugins::terminalplugin::workspace::WorkspaceManager& m_manager;
    plugins::terminalplugin::TerminalSettingsStore& m_settings;
    plugins::PluginHost& m_host;
    QVBoxLayout* m_rootLayout{nullptr};
    QWidget* m_gridHost{nullptr};
    QWidget* m_focusHost{nullptr};
    QWidget* m_shelf{nullptr};
    QScrollArea* m_shelfScrollArea{nullptr};
    QWidget* m_shelfContents{nullptr};
    QHBoxLayout* m_shelfLayout{nullptr};
    QList<TerminalPane*> m_panes;
    QVariantList m_renderedSlots;
    QVariantList m_renderedShelf;
    QString m_renderedTabId;
    QString m_renderedPresetId;
    QString m_focusModeSessionId;
};

} // namespace workpane::plugins::terminalplugin
