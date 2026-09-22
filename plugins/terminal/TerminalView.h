#pragma once

#include "TerminalFailure.h"
#include "TerminalSettingsStore.h"
#include "plugins/PluginInterface.h"
#include "ui/TabBar.h"
#include "workspace/WorkspaceManager.h"

#include <QWidget>

class QAction;
class QButtonGroup;
class QLabel;
class QMenu;
class QTabBar;
class QToolButton;

namespace workpane::plugins::terminalplugin {
class WorkspaceView;
}

namespace workpane::plugins::terminalplugin {

class TerminalView final : public QWidget {
    Q_OBJECT

  public:
    TerminalView(plugins::terminalplugin::workspace::WorkspaceManager& manager, TerminalSettingsStore& settings, PluginHost& host, QWidget* parent = nullptr);

  protected:
    void resizeEvent(QResizeEvent* event) override;

  private slots:
    void createTerminal();
    void createTab();
    void moveTab(int from, int to);
    void refreshTabs();
    void synchronizeCurrentTab();
    void selectTab(int index);
    void requestCloseTab(int index);
    void renameTab(int index);
    void closeFocusedTerminal();
    void requestCloseTerminal(QString terminalId, QString name);
    void showLayoutMenu();
    void applyLayoutFromButton();
    void updateLayoutSelection();
    void updateStatus();

  private:
    void createActions();
    void createInterface();

    plugins::terminalplugin::workspace::WorkspaceManager& m_manager;
    TerminalSettingsStore& m_settings;
    PluginHost& m_host;
    ui::TabBar* m_tabBar{nullptr};
    QToolButton* m_newTerminalButton{nullptr};
    QToolButton* m_layoutButton{nullptr};
    QMenu* m_layoutMenu{nullptr};
    QButtonGroup* m_layoutPresetGroup{nullptr};
    WorkspaceView* m_workspaceView{nullptr};
    QLabel* m_workspaceStatus{nullptr};
    QLabel* m_cwdStatus{nullptr};
    QAction* m_newTerminalAction{nullptr};
    QAction* m_newTabAction{nullptr};
    QAction* m_layoutAction{nullptr};
    QAction* m_closeTerminalAction{nullptr};
};

} // namespace workpane::plugins::terminalplugin
