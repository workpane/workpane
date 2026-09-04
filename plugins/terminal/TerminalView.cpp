#include "TerminalView.h"

#include "terminal/TerminalShortcuts.h"
#include "ui/Components.h"
#include "ui/Icons.h"
#include "ui/TabBar.h"
#include "ui/Theme.h"
#include "ui/WorkspaceView.h"

#include <QAction>
#include <QButtonGroup>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStatusBar>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <utility>

namespace workpane::plugins::terminalplugin {

class TerminalViewHelper final {
  public:
    static QToolButton* toolbarButton(QAction* action, const ui::Theme& theme, QWidget* parent);
};

QToolButton* TerminalViewHelper::toolbarButton(QAction* action, const ui::Theme& theme, QWidget* parent) {
    const int iconSize = theme.metric(ui::ThemeMetric::SmallIconSize);
    auto* button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("toolbarActionButton"));
    button->setDefaultAction(action);
    button->setAutoRaise(true);
    button->setIconSize({iconSize, iconSize});
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    button->setFixedHeight(theme.metric(ui::ThemeMetric::CompactButtonSize));
    return button;
}

TerminalView::TerminalView(plugins::terminalplugin::workspace::WorkspaceManager& manager, TerminalSettingsStore& settings, PluginHost& host, QWidget* parent) : QWidget(parent), m_manager(manager), m_settings(settings), m_host(host) {
    createActions();
    createInterface();

    connect(&m_manager, &QAbstractItemModel::modelReset, this, &TerminalView::refreshTabs);
    connect(&m_manager, &QAbstractItemModel::rowsInserted, this, &TerminalView::refreshTabs);
    connect(&m_manager, &QAbstractItemModel::rowsRemoved, this, &TerminalView::refreshTabs);
    connect(&m_manager, &QAbstractItemModel::rowsMoved, this, &TerminalView::refreshTabs);
    connect(&m_manager, &QAbstractItemModel::dataChanged, this, &TerminalView::refreshTabs);
    connect(&m_manager, &plugins::terminalplugin::workspace::WorkspaceManager::currentTabChanged, this, &TerminalView::synchronizeCurrentTab);
    connect(&m_manager, &plugins::terminalplugin::workspace::WorkspaceManager::currentTabChanged, this, &TerminalView::updateLayoutSelection);
    connect(&m_manager, &plugins::terminalplugin::workspace::WorkspaceManager::currentTabChanged, this, &TerminalView::updateStatus);
    connect(&m_manager, &plugins::terminalplugin::workspace::WorkspaceManager::sessionsChanged, this, &TerminalView::updateStatus);

    refreshTabs();
    updateLayoutSelection();
    updateStatus();
}

void TerminalView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    const bool compact = event->size().width() < 920;
    m_newTerminalButton->setToolButtonStyle(compact ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
    m_layoutButton->setToolButtonStyle(compact ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
}

void TerminalView::createTerminal() {
    const QString terminalId = m_manager.createTerminal();
    m_workspaceView->focusTerminal(terminalId);
}

void TerminalView::createTab() {
    m_manager.createTab();
}

void TerminalView::moveTab(int from, int to) {
    m_manager.moveTab(from, to);
}

void TerminalView::refreshTabs() {
    const QSignalBlocker blocker(m_tabBar);

    while (m_tabBar->count() > 0) {
        m_tabBar->removeTab(0);
    }

    for (int row = 0; row < m_manager.rowCount(); ++row) {
        const QModelIndex index = m_manager.index(row);
        m_tabBar->addTab(ui::IconCatalog::icon(ui::IconName::Terminal, m_host.theme()), m_manager.data(index, plugins::terminalplugin::workspace::WorkspaceManager::NameRole).toString());
    }

    m_tabBar->setCurrentIndex(m_manager.currentTabIndex());
}

void TerminalView::synchronizeCurrentTab() {
    const QSignalBlocker blocker(m_tabBar);
    m_tabBar->setCurrentIndex(m_manager.currentTabIndex());
    m_workspaceView->focusCurrentTerminal();
}

void TerminalView::selectTab(int index) {
    m_manager.setCurrentTabIndex(index);
}

void TerminalView::requestCloseTab(int index) {
    const QString name = m_manager.data(m_manager.index(index), plugins::terminalplugin::workspace::WorkspaceManager::NameRole).toString();
    const bool confirmed = m_host.confirm(this, m_host.translate(QStringLiteral("terminal.tabs.close-title")), m_host.translate(QStringLiteral("terminal.tabs.close-message")).arg(name), m_host.translate(QStringLiteral("terminal.tabs.close-detail")), m_host.translate(QStringLiteral("terminal.tabs.close-action")), true);

    if (confirmed) {
        m_manager.closeTab(index);
    }
}

void TerminalView::renameTab(int index) {
    if (index < 0) {
        return;
    }

    const QString currentName = m_manager.data(m_manager.index(index), plugins::terminalplugin::workspace::WorkspaceManager::NameRole).toString();
    bool accepted = false;
    const QString name = QInputDialog::getText(this, m_host.translate(QStringLiteral("terminal.tabs.rename-title")), m_host.translate(QStringLiteral("terminal.tabs.name")), QLineEdit::Normal, currentName, &accepted);

    if (accepted) {
        m_manager.renameTab(index, name);
    }
}

void TerminalView::closeFocusedTerminal() {
    const QString sessionId = m_manager.currentFocusedSessionId();

    if (sessionId.isEmpty()) {
        return;
    }

    requestCloseTerminal(sessionId, m_manager.sessionName(sessionId));
}

void TerminalView::requestCloseTerminal(QString terminalId, QString name) {
    const bool confirmed = m_host.confirm(this, m_host.translate(QStringLiteral("terminal.session.close-title")), m_host.translate(QStringLiteral("terminal.session.close-message")).arg(name), m_host.translate(QStringLiteral("terminal.session.close-detail")), m_host.translate(QStringLiteral("terminal.session.close-action")), true);

    if (confirmed) {
        m_manager.closeTerminal(std::move(terminalId));
    }
}

void TerminalView::showLayoutMenu() {
    updateLayoutSelection();
    m_layoutMenu->popup(m_layoutButton->mapToGlobal(QPoint(0, m_layoutButton->height())));
}

void TerminalView::applyLayoutFromButton() {
    const auto* button = qobject_cast<QToolButton*>(sender());

    if (button == nullptr) {
        return;
    }

    m_layoutMenu->close();
    m_manager.changeLayout(button->property("layoutId").toString());
}

void TerminalView::updateLayoutSelection() {
    const QString presetId = m_manager.currentPresetId();

    for (auto* button : m_layoutPresetGroup->buttons()) {
        button->setChecked(button->property("layoutId").toString() == presetId);
    }
}

void TerminalView::updateStatus() {
    const int workspaceCount = m_manager.rowCount({});
    const int terminalCount = m_manager.terminalCount();
    const QString workspaces = workspaceCount == 1 ? m_host.translate(QStringLiteral("terminal.status.workspace-single")) : m_host.translate(QStringLiteral("terminal.status.workspace-multiple")).arg(workspaceCount);
    const QString terminals = terminalCount == 1 ? m_host.translate(QStringLiteral("terminal.status.single")) : m_host.translate(QStringLiteral("terminal.status.multiple")).arg(terminalCount);
    m_workspaceStatus->setText(QStringLiteral("%1 · %2").arg(workspaces, terminals));
    m_cwdStatus->setText(m_manager.currentCwd());
    m_cwdStatus->setToolTip(m_manager.currentCwd());
}

void TerminalView::createActions() {
    m_newTerminalAction = new QAction(ui::IconCatalog::icon(ui::IconName::Terminal, m_host.theme()), m_host.translate(QStringLiteral("terminal.actions.new-terminal")), this);
    m_newTerminalAction->setObjectName(QStringLiteral("terminalNewTerminalAction"));
    m_newTerminalAction->setShortcut(terminalcore::TerminalShortcuts::newTerminal());
    m_newTerminalAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_newTabAction = new QAction(ui::IconCatalog::icon(ui::IconName::Add, m_host.theme()), m_host.translate(QStringLiteral("terminal.actions.new-tab")), this);
    m_newTabAction->setObjectName(QStringLiteral("terminalNewTabAction"));
    m_newTabAction->setShortcut(terminalcore::TerminalShortcuts::newTab());
    m_newTabAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_layoutAction = new QAction(ui::IconCatalog::icon(ui::IconName::Layout, m_host.theme()), m_host.translate(QStringLiteral("terminal.actions.layout")), this);
    m_layoutAction->setObjectName(QStringLiteral("terminalLayoutAction"));
    m_layoutAction->setShortcut(terminalcore::TerminalShortcuts::layout());
    m_layoutAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_closeTerminalAction = new QAction(ui::IconCatalog::icon(ui::IconName::Close, m_host.theme()), m_host.translate(QStringLiteral("terminal.actions.close-terminal")), this);
    m_closeTerminalAction->setObjectName(QStringLiteral("terminalCloseTerminalAction"));
    m_closeTerminalAction->setShortcut(terminalcore::TerminalShortcuts::closeTerminal());
    m_closeTerminalAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addActions({m_newTerminalAction, m_newTabAction, m_layoutAction, m_closeTerminalAction});
    connect(m_closeTerminalAction, &QAction::triggered, this, &TerminalView::closeFocusedTerminal);
    connect(m_newTerminalAction, &QAction::triggered, this, &TerminalView::createTerminal);
    connect(m_newTabAction, &QAction::triggered, this, &TerminalView::createTab);
    connect(m_layoutAction, &QAction::triggered, this, &TerminalView::showLayoutMenu);
}

void TerminalView::createInterface() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* workspaceBar = new QWidget(this);
    workspaceBar->setObjectName(QStringLiteral("workspaceBar"));
    workspaceBar->setFixedHeight(m_host.theme().metric(ui::ThemeMetric::WorkspaceBarHeight));
    auto* barLayout = new QHBoxLayout(workspaceBar);
    barLayout->setContentsMargins(0, 0, 5, 0);
    barLayout->setSpacing(2);
    m_tabBar = new ui::TabBar(m_host.theme(), workspaceBar);
    m_tabBar->setMovable(true);
    m_tabBar->setTabsClosable(true);
    auto* addTabButton = new QToolButton(workspaceBar);
    addTabButton->setAutoRaise(true);
    addTabButton->setIcon(ui::IconCatalog::icon(ui::IconName::Add, m_host.theme()));
    addTabButton->setToolTip(m_host.translate(QStringLiteral("terminal.actions.new-tab")));
    addTabButton->setFixedSize(m_host.theme().metric(ui::ThemeMetric::CompactButtonSize), m_host.theme().metric(ui::ThemeMetric::CompactButtonSize));
    m_newTerminalButton = TerminalViewHelper::toolbarButton(m_newTerminalAction, m_host.theme(), workspaceBar);
    m_layoutButton = TerminalViewHelper::toolbarButton(m_layoutAction, m_host.theme(), workspaceBar);
    barLayout->addWidget(m_tabBar);
    barLayout->addWidget(addTabButton);
    barLayout->addStretch(1);
    barLayout->addWidget(m_newTerminalButton);
    barLayout->addWidget(m_layoutButton);
    root->addWidget(workspaceBar);

    root->addWidget(ui::Components::horizontalDivider(this));
    m_workspaceView = new WorkspaceView(m_manager, m_settings, m_host, this);
    root->addWidget(m_workspaceView, 1);

    auto* status = new QStatusBar(this);
    status->setSizeGripEnabled(false);
    status->setFixedHeight(m_host.theme().metric(ui::ThemeMetric::StatusBarHeight));
    m_workspaceStatus = new QLabel(status);
    m_cwdStatus = new QLabel(status);
    status->addWidget(m_workspaceStatus);
    status->addPermanentWidget(m_cwdStatus);
    root->addWidget(status);

    m_layoutMenu = new QMenu(this);
    m_layoutMenu->setObjectName(QStringLiteral("layoutMenu"));
    auto* chooser = new QWidget(m_layoutMenu);
    auto* grid = new QGridLayout(chooser);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(3);
    grid->setVerticalSpacing(3);
    m_layoutPresetGroup = new QButtonGroup(chooser);
    m_layoutPresetGroup->setExclusive(true);
    int presetIndex = 0;

    for (const auto& preset : m_manager.layoutPresets()) {
        const QVariantMap values = preset.toMap();
        auto* button = new QToolButton(chooser);
        const QString name = values.value("name").toString();
        const int slotCount = values.value("slotCount").toInt();
        button->setObjectName(QStringLiteral("layoutPresetButton"));
        button->setAutoRaise(true);
        button->setCheckable(true);
        button->setFixedSize(48, 38);
        button->setIcon(ui::IconCatalog::layoutIcon(values.value("id").toString(), values.value("columns").toInt(), values.value("rows").toInt(), slotCount, m_host.theme()));
        button->setIconSize({38, 26});
        button->setProperty("layoutId", values.value("id"));
        button->setToolTip(m_host.translate(QStringLiteral("terminal.layout.slots")).arg(name).arg(slotCount));
        button->setAccessibleName(name);
        m_layoutPresetGroup->addButton(button);
        grid->addWidget(button, presetIndex / 4, presetIndex % 4);
        connect(button, &QToolButton::clicked, this, &TerminalView::applyLayoutFromButton);
        ++presetIndex;
    }

    auto* chooserAction = new QWidgetAction(m_layoutMenu);
    chooserAction->setDefaultWidget(chooser);
    m_layoutMenu->addAction(chooserAction);

    connect(m_tabBar, &QTabBar::tabCloseRequested, this, &TerminalView::requestCloseTab);
    connect(m_tabBar, &QTabBar::currentChanged, this, &TerminalView::selectTab);
    connect(m_tabBar, &QTabBar::tabMoved, this, &TerminalView::moveTab);
    connect(m_tabBar, &QTabBar::tabBarDoubleClicked, this, &TerminalView::renameTab);
    connect(addTabButton, &QToolButton::clicked, this, &TerminalView::createTab);
    connect(m_workspaceView, &WorkspaceView::closeTerminalRequested, this, &TerminalView::requestCloseTerminal);
    // clang-format off
    connect(m_workspaceView, &WorkspaceView::interactionError, this, [this](const Error& error) { m_host.notify(m_host.translate(QStringLiteral("terminal.error.interaction")), TerminalFailures::terminalFailureMessage(error, m_host), plugins::AlertSeverity::Error); });
    // clang-format on
}

} // namespace workpane::plugins::terminalplugin
