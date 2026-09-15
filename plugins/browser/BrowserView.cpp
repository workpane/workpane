#include "BrowserView.h"

#include "BrowserBookmarksView.h"
#include "BrowserPlugin.h"
#include "ui/Components.h"
#include "ui/Icons.h"
#include "ui/TabBar.h"
#include "ui/Theme.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWebEngineHistory>
#include <QWebEnginePage>
#include <QWebEngineView>

namespace workpane::plugins::browser {

class BrowserWebView final : public QWebEngineView {
  public:
    BrowserWebView(BrowserView& owner, QWidget* parent) : QWebEngineView(parent), m_owner(owner) {}

  protected:
    QWebEngineView* createWindow(QWebEnginePage::WebWindowType type) override {
        return m_owner.createPopupTab(type != QWebEnginePage::WebBrowserBackgroundTab);
    }

  private:
    BrowserView& m_owner;
};

BrowserView::BrowserView(BrowserPlugin& plugin, QWidget* parent) : QWidget(parent), m_plugin(plugin) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* toolbar = new QWidget(this);
    toolbar->setObjectName(QStringLiteral("browserToolbar"));
    toolbar->setFixedHeight(m_plugin.host().theme().metric(ui::ThemeMetric::PageHeaderHeight));
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(8, 0, 8, 0);
    toolbarLayout->setSpacing(4);
    m_back = ui::Components::toolButton(ui::IconName::Back, m_plugin.host().theme(), m_plugin.host().translate(QStringLiteral("browser.actions.back")), toolbar);
    m_forward = ui::Components::toolButton(ui::IconName::Forward, m_plugin.host().theme(), m_plugin.host().translate(QStringLiteral("browser.actions.forward")), toolbar);
    m_reload = ui::Components::toolButton(ui::IconName::Refresh, m_plugin.host().theme(), m_plugin.host().translate(QStringLiteral("browser.actions.reload")), toolbar);
    m_stop = ui::Components::toolButton(ui::IconName::Stop, m_plugin.host().theme(), m_plugin.host().translate(QStringLiteral("browser.actions.stop")), toolbar);
    auto* home = ui::Components::toolButton(ui::IconName::Home, m_plugin.host().theme(), m_plugin.host().translate(QStringLiteral("browser.actions.home")), toolbar);
    auto* showBookmarks = ui::Components::toolButton(ui::IconName::Bookmark, m_plugin.host().theme(), m_plugin.host().translate(QStringLiteral("browser.bookmarks.toggle")), toolbar);
    auto* add = ui::Components::toolButton(ui::IconName::Add, m_plugin.host().theme(), m_plugin.host().translate(QStringLiteral("browser.actions.new-tab")), toolbar);
    showBookmarks->setCheckable(true);
    showBookmarks->setChecked(false);
    m_address = new QLineEdit(toolbar);
    m_address->setObjectName(QStringLiteral("browserAddress"));
    m_address->setClearButtonEnabled(true);
    m_address->setPlaceholderText(m_plugin.host().translate(QStringLiteral("browser.address.placeholder")));
    toolbarLayout->addWidget(m_back);
    toolbarLayout->addWidget(m_forward);
    toolbarLayout->addWidget(m_reload);
    toolbarLayout->addWidget(m_stop);
    toolbarLayout->addWidget(home);
    toolbarLayout->addWidget(showBookmarks);
    toolbarLayout->addWidget(m_address, 1);
    toolbarLayout->addWidget(add);
    root->addWidget(toolbar);

    m_content = new QSplitter(Qt::Horizontal, this);
    m_content->setObjectName(QStringLiteral("browserContent"));
    m_content->setChildrenCollapsible(false);
    m_bookmarks = new BrowserBookmarksView(m_plugin, m_content);
    m_tabs = new ui::TabWidget(m_plugin.host().theme(), m_content);
    m_tabs->setObjectName(QStringLiteral("browserTabs"));
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_pages = new QStackedWidget(m_content);
    m_pages->setObjectName(QStringLiteral("browserPages"));
    m_empty = new QWidget(m_pages);
    auto* emptyLayout = new QVBoxLayout(m_empty);
    emptyLayout->setAlignment(Qt::AlignCenter);
    emptyLayout->setSpacing(14);
    emptyLayout->addWidget(ui::Components::emptyStateLabel(m_plugin.host().translate(QStringLiteral("browser.tabs.empty")), m_empty));
    auto* createFirstTab = new QPushButton(ui::IconCatalog::primaryIcon(ui::IconName::Add, m_plugin.host().theme()), m_plugin.host().translate(QStringLiteral("browser.actions.new-tab")), m_empty);
    createFirstTab->setObjectName(QStringLiteral("primaryButton"));
    createFirstTab->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    emptyLayout->addWidget(createFirstTab, 0, Qt::AlignHCenter);
    connect(createFirstTab, &QPushButton::clicked, this, &BrowserView::createTab);

    m_pages->addWidget(m_tabs);
    m_pages->addWidget(m_empty);
    m_content->addWidget(m_bookmarks);
    m_content->addWidget(m_pages);
    m_content->setStretchFactor(0, 0);
    m_content->setStretchFactor(1, 1);
    m_content->setSizes({280, 900});
    m_bookmarks->hide();
    root->addWidget(m_content, 1);

    m_rebuilding = true;

    for (const auto& tab : m_plugin.tabs()) {
        auto* view = appendTab(tab.id, tab.url, tab.title, tab.active);
        Q_UNUSED(view);
    }

    m_rebuilding = false;
    updateToolbar();

    auto* closeTabAction = new QAction(m_plugin.host().translate(QStringLiteral("browser.actions.close-tab")), this);
    closeTabAction->setObjectName(QStringLiteral("browserCloseTab"));
    closeTabAction->setShortcuts(QKeySequence::keyBindings(QKeySequence::Close));
    closeTabAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(closeTabAction);

    connect(add, &QToolButton::clicked, this, &BrowserView::createTab);
    // clang-format off
    connect(closeTabAction, &QAction::triggered, this, [this]() { if (m_tabs->currentIndex() >= 0) { closeTab(m_tabs->currentIndex()); } });
    // clang-format on
    connect(showBookmarks, &QToolButton::toggled, this, &BrowserView::toggleBookmarks);
    // clang-format off
    connect(m_back, &QToolButton::clicked, this, [this]() { if (auto* view = currentView(); view != nullptr) { view->back(); } });
    connect(m_forward, &QToolButton::clicked, this, [this]() { if (auto* view = currentView(); view != nullptr) { view->forward(); } });
    connect(m_reload, &QToolButton::clicked, this, [this]() { if (auto* view = currentView(); view != nullptr) { view->reload(); } });
    connect(m_stop, &QToolButton::clicked, this, [this]() { if (auto* view = currentView(); view != nullptr) { view->stop(); } });
    connect(home, &QToolButton::clicked, this, [this]() { if (auto* view = currentView(); view != nullptr) { view->setUrl(m_plugin.homepage()); } });
    // clang-format on
    connect(m_address, &QLineEdit::returnPressed, this, &BrowserView::navigate);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &BrowserView::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, &BrowserView::selectTab);
    connect(m_tabs->tabBar(), &QTabBar::tabMoved, this, &BrowserView::moveTab);
    connect(m_bookmarks, &BrowserBookmarksView::addBookmarkRequested, this, &BrowserView::addCurrentPageBookmark);
    connect(m_bookmarks, &BrowserBookmarksView::openRequested, this, &BrowserView::openBookmark);
    connect(&m_plugin, &BrowserPlugin::tabsChanged, this, &BrowserView::synchronizeTabs);
}

QWebEngineView* BrowserView::createPopupTab(bool activate) {
    const auto created = m_plugin.createTab(QUrl(QStringLiteral("about:blank")), activate);

    if (!created.hasValue()) {
        return nullptr;
    }

    auto* view = findChild<QWebEngineView*>(created.value());
    return view;
}

void BrowserView::createTab() {
    auto* view = createPopupTab(true);

    if (view == nullptr) {
        showOperationError();
        return;
    }

    view->setUrl(m_plugin.homepage());
}

void BrowserView::closeTab(int index) {
    const auto result = m_plugin.closeTab(tabId(index));

    if (!result.hasValue()) {
        showOperationError();
    }
}

void BrowserView::selectTab(int index) {
    if (m_rebuilding || index < 0) {
        return;
    }

    const auto result = m_plugin.activateTab(tabId(index));

    if (!result.hasValue()) {
        showOperationError();
        return;
    }

    updateToolbar();
}

void BrowserView::moveTab(int from, int to) {
    if (m_rebuilding) {
        return;
    }

    const auto result = m_plugin.moveTab(from, to);

    if (!result.hasValue()) {
        showOperationError();
    }
}

void BrowserView::navigate() {
    const auto url = BrowserPlugin::normalizeAddress(m_address->text());

    if (!url.hasValue()) {
        showInvalidAddress();
        return;
    }

    auto* view = currentView();

    if (view == nullptr) {
        showOperationError();
        return;
    }

    view->setUrl(url.value());
}

void BrowserView::updateAddress(const QUrl& url) {
    auto* view = qobject_cast<QWebEngineView*>(sender());

    if (view == nullptr) {
        return;
    }

    const int index = m_tabs->indexOf(view);

    if (index < 0) {
        return;
    }

    const auto result = m_plugin.updateTabUrl(tabId(index), url);

    if (!result.hasValue()) {
        showOperationError();
        return;
    }

    if (view == currentView()) {
        m_address->setText(url.toString());
        updateToolbar();
    }
}

void BrowserView::updateTitle(const QString& title) {
    auto* view = qobject_cast<QWebEngineView*>(sender());
    const int index = m_tabs->indexOf(view);

    if (index < 0 || title.trimmed().isEmpty()) {
        return;
    }

    m_tabs->setTabText(index, title);
    const auto result = m_plugin.updateTabTitle(tabId(index), title);

    if (!result.hasValue()) {
        showOperationError();
    }
}

void BrowserView::updateIcon(const QIcon& icon) {
    auto* view = qobject_cast<QWebEngineView*>(sender());
    const int index = m_tabs->indexOf(view);

    if (index >= 0) {
        m_tabs->setTabIcon(index, icon);
    }
}

void BrowserView::updateLoadState(bool loading) {
    auto* view = qobject_cast<QWebEngineView*>(sender());

    if (view == currentView()) {
        m_reload->setVisible(!loading);
        m_stop->setVisible(loading);
        updateToolbar();
    }
}

void BrowserView::addCurrentPageBookmark() {
    auto* view = currentView();

    if (view == nullptr) {
        showOperationError();
        return;
    }

    m_bookmarks->beginAddBookmark(view->title(), view->url());
}

void BrowserView::openBookmark(const QUrl& url, bool newTab) {
    if (!newTab) {
        auto* view = currentView();
        if (view == nullptr) {
            showOperationError();
            return;
        }
        view->setUrl(url);
        return;
    }

    const auto result = m_plugin.createTab(url, true);

    if (!result.hasValue()) {
        showOperationError();
    }
}

void BrowserView::toggleBookmarks(bool visible) {
    m_bookmarks->setVisible(visible);
}

void BrowserView::synchronizeTabs() {
    m_rebuilding = true;

    for (int index = m_tabs->count() - 1; index >= 0; --index) {
        const QString existingId = tabId(index);
        bool exists = false;
        for (const auto& tab : m_plugin.tabs()) {
            exists = exists || tab.id == existingId;
        }
        if (!exists) {
            QWidget* removed = m_tabs->widget(index);
            m_tabs->removeTab(index);
            removed->deleteLater();
        }
    }

    for (int index = 0; index < m_plugin.tabs().size(); ++index) {
        const auto& tab = m_plugin.tabs().at(index);
        auto* view = findChild<QWebEngineView*>(tab.id);
        if (view == nullptr) {
            view = appendTab(tab.id, tab.url, tab.title, tab.active);
        }
        const int currentPosition = m_tabs->indexOf(view);
        if (currentPosition != index) {
            m_tabs->tabBar()->moveTab(currentPosition, index);
        }
        if (tab.active) {
            m_tabs->setCurrentIndex(index);
        }
    }

    m_rebuilding = false;

    m_pages->setCurrentWidget(m_tabs->count() > 0 ? static_cast<QWidget*>(m_tabs) : m_empty);
    updateToolbar();
}

QWebEngineView* BrowserView::appendTab(const QString& tabId, const QUrl& url, const QString& title, bool activate) {
    auto* view = new BrowserWebView(*this, m_tabs);
    view->setObjectName(tabId);
    view->setPage(new QWebEnginePage(&m_plugin.profile(), view));
    const int index = m_tabs->addTab(view, title);
    m_tabs->setTabToolTip(index, url.toString());

    if (activate) {
        m_tabs->setCurrentIndex(index);
    }

    connect(view, &QWebEngineView::urlChanged, this, &BrowserView::updateAddress);
    connect(view, &QWebEngineView::titleChanged, this, &BrowserView::updateTitle);
    connect(view, &QWebEngineView::iconChanged, this, &BrowserView::updateIcon);
    // clang-format off
    connect(view, &QWebEngineView::loadStarted, this, [this]() { updateLoadState(true); });
    connect(view, &QWebEngineView::loadFinished, this, [this](bool) { updateLoadState(false); });
    // clang-format on
    view->setUrl(url);
    return view;
}

QWebEngineView* BrowserView::currentView() const {
    return qobject_cast<QWebEngineView*>(m_tabs->currentWidget());
}

QString BrowserView::tabId(int index) const {
    auto* view = qobject_cast<QWebEngineView*>(m_tabs->widget(index));
    return view == nullptr ? QString{} : view->objectName();
}

void BrowserView::showOperationError() {
    m_plugin.host().notify(m_plugin.host().translate(QStringLiteral("browser.plugin.title")), m_plugin.host().translate(QStringLiteral("browser.error.operation")), AlertSeverity::Error);
}

void BrowserView::updateToolbar() {
    auto* view = currentView();

    if (view == nullptr) {
        m_address->clear();
        m_back->setEnabled(false);
        m_forward->setEnabled(false);
        m_reload->setEnabled(false);
        m_stop->setEnabled(false);
        return;
    }

    m_reload->setEnabled(true);
    m_stop->setEnabled(true);
    m_address->setText(view->url().toString());
    m_back->setEnabled(view->history()->canGoBack());
    m_forward->setEnabled(view->history()->canGoForward());
}

void BrowserView::showInvalidAddress() {
    m_plugin.host().notify(m_plugin.host().translate(QStringLiteral("browser.plugin.title")), m_plugin.host().translate(QStringLiteral("browser.error.invalid-address")), AlertSeverity::Error);
}

} // namespace workpane::plugins::browser
