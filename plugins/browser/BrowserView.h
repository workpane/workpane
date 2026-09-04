#pragma once

#include "ui/TabBar.h"

#include <QIcon>
#include <QUrl>
#include <QWidget>

class QLineEdit;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QToolButton;
class QWebEngineView;

namespace workpane::plugins::browser {

class BrowserPlugin;
class BrowserBookmarksView;

class BrowserView final : public QWidget {
    Q_OBJECT

  public:
    BrowserView(BrowserPlugin& plugin, QWidget* parent = nullptr);
    [[nodiscard]] QWebEngineView* createPopupTab(bool activate);

  private slots:
    void createTab();
    void closeTab(int index);
    void selectTab(int index);
    void moveTab(int from, int to);
    void navigate();
    void updateAddress(const QUrl& url);
    void updateTitle(const QString& title);
    void updateIcon(const QIcon& icon);
    void updateLoadState(bool loading);
    void addCurrentPageBookmark();
    void openBookmark(const QUrl& url, bool newTab);
    void toggleBookmarks(bool visible);
    void synchronizeTabs();

  private:
    [[nodiscard]] QWebEngineView* appendTab(const QString& tabId, const QUrl& url, const QString& title, bool activate);
    [[nodiscard]] QWebEngineView* currentView() const;
    [[nodiscard]] QString tabId(int index) const;
    void showOperationError();
    void updateToolbar();
    void showInvalidAddress();

    BrowserPlugin& m_plugin;
    BrowserBookmarksView* m_bookmarks{nullptr};
    QSplitter* m_content{nullptr};
    QStackedWidget* m_pages{nullptr};
    ui::TabWidget* m_tabs{nullptr};
    QWidget* m_empty{nullptr};
    QLineEdit* m_address{nullptr};
    QToolButton* m_back{nullptr};
    QToolButton* m_forward{nullptr};
    QToolButton* m_reload{nullptr};
    QToolButton* m_stop{nullptr};
    bool m_rebuilding{false};
};

} // namespace workpane::plugins::browser
