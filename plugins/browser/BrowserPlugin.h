#pragma once

#include "plugins/PluginInterface.h"

#include <QDateTime>
#include <QObject>
#include <QUrl>

#include <memory>

class QWebEngineProfile;

namespace workpane::plugins::browser {

struct BrowserTab final {
    QString id;
    QString title;
    QUrl url;
    QDateTime createdAtUtc;
    QDateTime updatedAtUtc;
    bool active{false};
};

struct BrowserBookmarkGroup final {
    QString id;
    QString name;
    QDateTime createdAtUtc;
    QDateTime updatedAtUtc;
};

struct BrowserBookmark final {
    QString id;
    QString groupId;
    QString name;
    QUrl url;
    QDateTime createdAtUtc;
    QDateTime updatedAtUtc;
};

struct BrowserBookmarkPlacement final {
    QString bookmarkId;
    QString groupId;
};

class BrowserPlugin final : public QObject, public PluginInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID WorkpanePluginInterface_iid)
    Q_INTERFACES(workpane::plugins::PluginInterface)

  public:
    BrowserPlugin();
    ~BrowserPlugin() override;

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
    void shutdown() override;

    [[nodiscard]] static Result<QUrl> normalizeAddress(const QString& address);
    [[nodiscard]] const QVector<BrowserTab>& tabs() const;
    [[nodiscard]] const QVector<BrowserBookmarkGroup>& bookmarkGroups() const;
    [[nodiscard]] const QVector<BrowserBookmark>& bookmarks() const;
    [[nodiscard]] const QUrl& homepage() const;
    [[nodiscard]] QWebEngineProfile& profile();
    [[nodiscard]] Result<QString> createTab(const QUrl& url, bool activate);
    [[nodiscard]] Result<void> closeTab(const QString& tabId);
    [[nodiscard]] Result<void> activateTab(const QString& tabId);
    [[nodiscard]] Result<void> moveTab(int from, int to);
    [[nodiscard]] Result<void> updateTabUrl(const QString& tabId, const QUrl& url);
    [[nodiscard]] Result<void> updateTabTitle(const QString& tabId, const QString& title);
    [[nodiscard]] Result<QString> createBookmarkGroup(const QString& name);
    [[nodiscard]] Result<void> updateBookmarkGroup(const QString& groupId, const QString& name);
    [[nodiscard]] Result<void> removeBookmarkGroup(const QString& groupId);
    [[nodiscard]] Result<QString> createBookmark(const QString& name, const QString& address, const QString& groupId);
    [[nodiscard]] Result<void> updateBookmark(const QString& bookmarkId, const QString& name, const QString& address, const QString& groupId);
    [[nodiscard]] Result<void> removeBookmark(const QString& bookmarkId);
    [[nodiscard]] Result<void> applyBookmarkLayout(const QVector<QString>& orderedGroupIds, const QVector<BrowserBookmarkPlacement>& orderedBookmarks);
    [[nodiscard]] Result<void> setHomepage(const QString& address);
    [[nodiscard]] PluginHost& host() const;

  signals:
    void tabsChanged();
    void bookmarksChanged();
    void homepageChanged(const QString& homepage);

  private:
    [[nodiscard]] Result<void> restoreState();
    [[nodiscard]] Result<void> restoreBookmarks();
    [[nodiscard]] BrowserTab* findTab(const QString& tabId);
    [[nodiscard]] BrowserBookmarkGroup* findBookmarkGroup(const QString& groupId);
    [[nodiscard]] BrowserBookmark* findBookmark(const QString& bookmarkId);
    [[nodiscard]] bool bookmarkGroupExists(const QString& groupId) const;
    void persistTabs();
    void persistBookmarks();
    void persistHomepage();

    PluginHost* m_host{nullptr};
    QVector<BrowserTab> m_tabs;
    QVector<BrowserTab> m_committedTabs;
    QVector<BrowserBookmarkGroup> m_bookmarkGroups;
    QVector<BrowserBookmarkGroup> m_committedBookmarkGroups;
    QVector<BrowserBookmark> m_bookmarks;
    QVector<BrowserBookmark> m_committedBookmarks;
    QUrl m_homepage{QStringLiteral("about:blank")};
    QUrl m_committedHomepage{QStringLiteral("about:blank")};
    QString m_browserDataPath;
    std::unique_ptr<QWebEngineProfile> m_profile;
    std::unique_ptr<QObject> m_asyncContext;
    quint64 m_tabsRevision{0};
    quint64 m_committedTabsRevision{0};
    quint64 m_bookmarksRevision{0};
    quint64 m_committedBookmarksRevision{0};
    quint64 m_homepageRevision{0};
};

} // namespace workpane::plugins::browser
