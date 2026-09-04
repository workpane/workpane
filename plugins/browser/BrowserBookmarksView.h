#pragma once

#include <QTreeWidget>
#include <QUrl>
#include <QWidget>

class QDropEvent;
class QPushButton;
class QToolButton;

namespace workpane::plugins::browser {

class BrowserPlugin;

class BookmarkTree final : public QTreeWidget {
    Q_OBJECT

  public:
    explicit BookmarkTree(QWidget* parent = nullptr);

  signals:
    void layoutChanged();

  protected:
    void dropEvent(QDropEvent* event) override;
};

class BrowserBookmarksView final : public QWidget {
    Q_OBJECT

  public:
    BrowserBookmarksView(BrowserPlugin& plugin, QWidget* parent = nullptr);
    void beginAddBookmark(const QString& suggestedName, const QUrl& suggestedUrl);

  signals:
    void addBookmarkRequested();
    void openRequested(const QUrl& url, bool newTab);

  private slots:
    void addGroup();
    void editSelected();
    void removeSelected();
    void openSelectedInCurrentTab();
    void openSelectedInNewTab();
    void openSelectedFromDoubleClick(QTreeWidgetItem* item, int column);
    void showContextMenu(const QPoint& position);
    void applyTreeLayout();
    void rebuild();
    void updateActions();

  private:
    [[nodiscard]] QString selectedGroupId() const;
    [[nodiscard]] QString selectedBookmarkId() const;
    [[nodiscard]] QTreeWidgetItem* bookmarkItem(const QString& bookmarkId) const;
    void editGroup(const QString& groupId);
    void editBookmark(const QString& bookmarkId);
    void notifyInvalidEditorValue();

    BrowserPlugin& m_plugin;
    BookmarkTree* m_tree{nullptr};
    QToolButton* m_edit{nullptr};
    QToolButton* m_remove{nullptr};
    QPushButton* m_openCurrent{nullptr};
    QPushButton* m_openNew{nullptr};
    bool m_rebuilding{false};
};

} // namespace workpane::plugins::browser
