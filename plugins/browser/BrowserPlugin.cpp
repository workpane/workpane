#include "BrowserPlugin.h"

#include "BrowserTranslations.h"
#include "BrowserView.h"
#include "persistence/StoredValues.h"
#include "ui/Components.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonObject>
#include <QLineEdit>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <QVBoxLayout>
#include <QWebEngineProfile>

#include <algorithm>
#include <memory>
#include <utility>

namespace workpane::plugins::browser {

constexpr auto pluginIdentifier = "browser";

BrowserPlugin::BrowserPlugin() = default;
BrowserPlugin::~BrowserPlugin() {
    shutdown();
}

QString BrowserPlugin::id() const {
    return QString::fromLatin1(pluginIdentifier);
}

QString BrowserPlugin::titleKey() const {
    return QStringLiteral("browser.plugin.title");
}

QStringList BrowserPlugin::dependencies() const {
    return {};
}

int BrowserPlugin::databaseSchemaVersion() const {
    return 1;
}

TranslationCatalog BrowserPlugin::translations() const {
    return translations::BrowserCatalog::catalog();
}

QString BrowserPlugin::styleSheet(const ui::Theme&) const {
    return QStringLiteral("QWidget#browserToolbar { background: @panel; border-bottom: 1px solid @border; } QWidget#browserBookmarksPanel { background: @panel; border-right: 1px solid @borderStrong; } QWidget#browserBookmarksHeader { background: @panel; border-bottom: 1px solid @border; } QWidget#browserBookmarksActions { background: @panel; border-top: 1px solid @border; } QTreeWidget#browserBookmarksTree { background: @panel; border: none; outline: none; } QTreeWidget#browserBookmarksTree::item { min-height: 28px; padding: 2px 5px; border-radius: @controlRadiuspx; } QTreeWidget#browserBookmarksTree::item:hover { background: @hover; } QTreeWidget#browserBookmarksTree::item:selected { background: @accent; color: @onAccent; } QTabWidget#browserTabs::pane { border: none; } QTabBar::tab { min-width: 120px; max-width: 240px; }");
}

QVector<NavigationItem> BrowserPlugin::navigationItems(const ui::Theme& theme) const {
    return {{QStringLiteral("web"), QStringLiteral("browser.navigation.web"), ui::IconCatalog::icon(ui::IconName::Browser, theme), NavigationPlacement::Primary, NavigationOrder::Browser}};
}

QVector<SettingsGroup> BrowserPlugin::settingsGroups() const {
    const SettingsSection general{QStringLiteral("general"), QStringLiteral("browser.settings.general"), {QStringLiteral("browser.settings.homepage")}};
    return {{QStringLiteral("browser"), QStringLiteral("browser.plugin.title"), {general}}};
}

Result<void> BrowserPlugin::initialize(PluginHost& host) {
    if (m_host != nullptr) {
        return Result<void>::failure({"browser_already_initialized", "The Browser plugin is already initialized", {}});
    }

    m_host = &host;
    m_asyncContext = std::make_unique<QObject>();
    const QFileInfo dataRoot(host.applicationDataPath());

    if (!dataRoot.isAbsolute() || !dataRoot.isDir()) {
        shutdown();
        return Result<void>::failure({"browser_data_directory_invalid", "The Browser application data directory is invalid", host.applicationDataPath()});
    }

    m_browserDataPath = QDir(host.applicationDataPath()).filePath(QStringLiteral("browser"));

    if (!QDir().mkpath(QDir(m_browserDataPath).filePath(QStringLiteral("storage"))) || !QDir().mkpath(QDir(m_browserDataPath).filePath(QStringLiteral("cache")))) {
        const Error error{"browser_data_directory_failed", "The Browser data directory is unavailable", m_browserDataPath};
        shutdown();
        return Result<void>::failure(error);
    }

    const auto capability = host.provideCapability({QString::fromLatin1(openPageCapability)});

    if (!capability.hasValue()) {
        shutdown();
        return capability;
    }

    const auto migration = host.migrateDatabase({{1, {QStringLiteral("CREATE TABLE browser_tabs(id TEXT PRIMARY KEY, position INTEGER NOT NULL UNIQUE CHECK(position >= 0), title TEXT NOT NULL, url TEXT NOT NULL, created_at_utc TEXT NOT NULL, updated_at_utc TEXT NOT NULL, active INTEGER NOT NULL CHECK(active IN (0, 1))) STRICT"), QStringLiteral("CREATE UNIQUE INDEX browser_tabs_active_index ON browser_tabs(active) WHERE active = 1"), QStringLiteral("CREATE TABLE browser_bookmark_groups(id TEXT PRIMARY KEY, position INTEGER NOT NULL UNIQUE CHECK(position >= 0), name TEXT NOT NULL, created_at_utc TEXT NOT NULL, updated_at_utc TEXT NOT NULL) STRICT"), QStringLiteral("CREATE TABLE browser_bookmarks(id TEXT PRIMARY KEY, group_id TEXT REFERENCES browser_bookmark_groups(id) ON DELETE RESTRICT, position INTEGER NOT NULL CHECK(position >= 0), name TEXT NOT NULL, url TEXT NOT NULL, created_at_utc TEXT NOT NULL, updated_at_utc TEXT NOT NULL) STRICT"), QStringLiteral("CREATE UNIQUE INDEX browser_bookmarks_group_position_index ON browser_bookmarks(COALESCE(group_id, ''), position)")}}});

    if (!migration.hasValue()) {
        shutdown();
        return migration;
    }

    const auto restored = restoreState();

    if (!restored.hasValue()) {
        shutdown();
        return restored;
    }

    m_committedTabs = m_tabs;
    m_committedBookmarkGroups = m_bookmarkGroups;
    m_committedBookmarks = m_bookmarks;
    m_committedHomepage = m_homepage;
    return Result<void>::success();
}

QWidget* BrowserPlugin::createNavigationView(const QString& itemId, QWidget* parent) {
    return itemId == QStringLiteral("web") && m_host != nullptr ? new BrowserView(*this, parent) : nullptr;
}

QWidget* BrowserPlugin::createSettingsSection(const QString& groupId, const QString& sectionId, QWidget* parent) {
    if (groupId != QStringLiteral("browser") || sectionId != QStringLiteral("general") || m_host == nullptr) {
        return nullptr;
    }

    const auto [page, layout] = ui::Components::settingsSectionPage(parent);
    auto* form = ui::Components::settingsForm();
    auto* homepage = new QLineEdit(m_homepage.toString(), page);
    homepage->setObjectName(QStringLiteral("browserHomepage"));
    ui::Components::addSettingsRow(form, host().translate(QStringLiteral("browser.settings.homepage")), homepage);
    layout->addLayout(form);
    layout->addStretch(1);
    // clang-format off
    connect(homepage, &QLineEdit::editingFinished, this, [this, homepage]() {
        const auto result = setHomepage(homepage->text());
        if (!result.hasValue()) {
            homepage->setText(m_homepage.toString());
            host().notify(host().translate(QStringLiteral("browser.plugin.title")), host().translate(QStringLiteral("browser.error.invalid-address")), AlertSeverity::Error);
        }
    });
    // clang-format on
    connect(this, &BrowserPlugin::homepageChanged, homepage, &QLineEdit::setText);
    return page;
}

void BrowserPlugin::handleRequest(const QString&, const QString& topic, const QJsonObject& payload, PluginReply reply) {
    if (topic != QString::fromLatin1(openPageCapability) || !SettingsReaders::hasExactKeys(payload, {QStringLiteral("url")}) || !payload.value(QStringLiteral("url")).isString()) {
        reply(SettingsReaders::unhandledTopic(topic));
        return;
    }

    const auto url = normalizeAddress(payload.value(QStringLiteral("url")).toString());

    if (!url.hasValue()) {
        reply(Result<QJsonObject>::failure(url.error()));
        return;
    }

    const auto tab = createTab(url.value(), true);

    if (tab.hasValue()) {
        m_host->showNavigation(QStringLiteral("web"));
    }

    reply(tab.hasValue() ? Result<QJsonObject>::success({{QStringLiteral("tabId"), tab.value()}}) : Result<QJsonObject>::failure(tab.error()));
}

void BrowserPlugin::shutdown() {
    m_asyncContext.reset();
    m_profile.reset();
    m_tabs.clear();
    m_committedTabs.clear();
    m_bookmarkGroups.clear();
    m_committedBookmarkGroups.clear();
    m_bookmarks.clear();
    m_committedBookmarks.clear();
    m_browserDataPath.clear();
    m_host = nullptr;
}

Result<QUrl> BrowserPlugin::normalizeAddress(const QString& address) {
    QString normalized = address.trimmed();

    if (normalized.isEmpty()) {
        return Result<QUrl>::failure({"browser_address_invalid", "The browser address is empty", {}});
    }

    if (!normalized.contains(QStringLiteral("://")) && !normalized.startsWith(QStringLiteral("about:"), Qt::CaseInsensitive)) {
        normalized.prepend(QStringLiteral("https://"));
    }

    const QUrl url(normalized, QUrl::StrictMode);
    const QSet<QString> schemes{QStringLiteral("about"), QStringLiteral("file"), QStringLiteral("http"), QStringLiteral("https")};

    if (!url.isValid() || !schemes.contains(url.scheme().toLower()) || ((url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https")) && url.host().isEmpty())) {
        return Result<QUrl>::failure({"browser_address_invalid", "The browser address is invalid", address});
    }

    return Result<QUrl>::success(url);
}

const QVector<BrowserTab>& BrowserPlugin::tabs() const {
    return m_tabs;
}

const QVector<BrowserBookmarkGroup>& BrowserPlugin::bookmarkGroups() const {
    return m_bookmarkGroups;
}

const QVector<BrowserBookmark>& BrowserPlugin::bookmarks() const {
    return m_bookmarks;
}

const QUrl& BrowserPlugin::homepage() const {
    return m_homepage;
}

QWebEngineProfile& BrowserPlugin::profile() {
    if (m_profile == nullptr) {
        m_profile = std::make_unique<QWebEngineProfile>(QStringLiteral("workpane-browser"));
        m_profile->setPersistentStoragePath(QDir(m_browserDataPath).filePath(QStringLiteral("storage")));
        m_profile->setCachePath(QDir(m_browserDataPath).filePath(QStringLiteral("cache")));
        m_profile->setDownloadPath(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
        m_profile->setPersistentCookiesPolicy(QWebEngineProfile::AllowPersistentCookies);
        m_profile->setPersistentPermissionsPolicy(QWebEngineProfile::PersistentPermissionsPolicy::StoreOnDisk);
    }

    return *m_profile;
}

Result<QString> BrowserPlugin::createTab(const QUrl& url, bool activate) {
    const auto normalized = normalizeAddress(url.toString());

    if (!normalized.hasValue()) {
        return Result<QString>::failure(normalized.error());
    }

    const bool shouldActivate = activate || m_tabs.isEmpty();

    if (shouldActivate) {
        for (auto& tab : m_tabs) {
            tab.active = false;
        }
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString tabId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_tabs.append({tabId, host().translate(QStringLiteral("browser.tabs.new")), normalized.value(), now, now, shouldActivate});
    persistTabs();
    emit tabsChanged();
    return Result<QString>::success(tabId);
}

Result<void> BrowserPlugin::closeTab(const QString& tabId) {
    int index = -1;

    for (int candidate = 0; candidate < m_tabs.size(); ++candidate) {
        if (m_tabs.at(candidate).id == tabId) {
            index = candidate;
            break;
        }
    }

    if (index < 0) {
        return Result<void>::failure({"browser_tab_unknown", "The browser tab does not exist", tabId});
    }

    const bool wasActive = m_tabs.at(index).active;
    m_tabs.removeAt(index);

    if (wasActive && !m_tabs.isEmpty()) {
        m_tabs[std::min(index, static_cast<int>(m_tabs.size()) - 1)].active = true;
    }

    persistTabs();
    emit tabsChanged();
    return Result<void>::success();
}

Result<void> BrowserPlugin::activateTab(const QString& tabId) {
    BrowserTab* selected = findTab(tabId);

    if (selected == nullptr) {
        return Result<void>::failure({"browser_tab_unknown", "The browser tab does not exist", tabId});
    }
    if (selected->active) {
        return Result<void>::success();
    }

    for (auto& tab : m_tabs) {
        tab.active = tab.id == tabId;
    }

    selected->updatedAtUtc = QDateTime::currentDateTimeUtc();
    persistTabs();
    return Result<void>::success();
}

Result<void> BrowserPlugin::moveTab(int from, int to) {
    if (from < 0 || to < 0 || from >= m_tabs.size() || to >= m_tabs.size()) {
        return Result<void>::failure({"browser_tab_position_invalid", "The browser tab position is invalid", QStringLiteral("%1 -> %2").arg(from).arg(to)});
    }

    m_tabs.move(from, to);
    persistTabs();
    return Result<void>::success();
}

Result<void> BrowserPlugin::updateTabUrl(const QString& tabId, const QUrl& url) {
    BrowserTab* tab = findTab(tabId);
    const auto normalized = normalizeAddress(url.toString());

    if (tab == nullptr || !normalized.hasValue()) {
        return tab == nullptr ? Result<void>::failure(Error{"browser_tab_unknown", "The browser tab does not exist", tabId}) : Result<void>::failure(normalized.error());
    }
    if (tab->url == normalized.value()) {
        return Result<void>::success();
    }

    tab->url = normalized.value();
    tab->updatedAtUtc = QDateTime::currentDateTimeUtc();
    persistTabs();
    return Result<void>::success();
}

Result<void> BrowserPlugin::updateTabTitle(const QString& tabId, const QString& title) {
    BrowserTab* tab = findTab(tabId);
    const QString normalized = title.trimmed();

    if (tab == nullptr || normalized.isEmpty()) {
        return Result<void>::failure({"browser_tab_title_invalid", "The browser tab title is invalid", tabId});
    }
    if (tab->title == normalized) {
        return Result<void>::success();
    }

    tab->title = normalized;
    tab->updatedAtUtc = QDateTime::currentDateTimeUtc();
    persistTabs();
    return Result<void>::success();
}

Result<QString> BrowserPlugin::createBookmarkGroup(const QString& name) {
    const QString normalizedName = name.trimmed();

    if (normalizedName.isEmpty()) {
        return Result<QString>::failure({"browser_bookmark_group_name_invalid", "The bookmark group name is invalid", name});
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString groupId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_bookmarkGroups.append({groupId, normalizedName, now, now});
    persistBookmarks();
    emit bookmarksChanged();
    return Result<QString>::success(groupId);
}

Result<void> BrowserPlugin::updateBookmarkGroup(const QString& groupId, const QString& name) {
    BrowserBookmarkGroup* group = findBookmarkGroup(groupId);
    const QString normalizedName = name.trimmed();

    if (group == nullptr) {
        return Result<void>::failure({"browser_bookmark_group_unknown", "The bookmark group does not exist", groupId});
    }
    if (normalizedName.isEmpty()) {
        return Result<void>::failure({"browser_bookmark_group_name_invalid", "The bookmark group name is invalid", name});
    }
    if (group->name == normalizedName) {
        return Result<void>::success();
    }

    group->name = normalizedName;
    group->updatedAtUtc = QDateTime::currentDateTimeUtc();
    persistBookmarks();
    emit bookmarksChanged();
    return Result<void>::success();
}

Result<void> BrowserPlugin::removeBookmarkGroup(const QString& groupId) {
    int groupIndex = -1;

    for (int index = 0; index < m_bookmarkGroups.size(); ++index) {
        if (m_bookmarkGroups.at(index).id == groupId) {
            groupIndex = index;
            break;
        }
    }

    if (groupIndex < 0) {
        return Result<void>::failure({"browser_bookmark_group_unknown", "The bookmark group does not exist", groupId});
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    m_bookmarkGroups.removeAt(groupIndex);

    for (auto& bookmark : m_bookmarks) {
        if (bookmark.groupId == groupId) {
            bookmark.groupId.clear();
            bookmark.updatedAtUtc = now;
        }
    }

    persistBookmarks();
    emit bookmarksChanged();
    return Result<void>::success();
}

Result<QString> BrowserPlugin::createBookmark(const QString& name, const QString& address, const QString& groupId) {
    const QString normalizedName = name.trimmed();
    const auto normalizedUrl = normalizeAddress(address);

    if (normalizedName.isEmpty()) {
        return Result<QString>::failure({"browser_bookmark_name_invalid", "The bookmark name is invalid", name});
    }
    if (!normalizedUrl.hasValue()) {
        return Result<QString>::failure(normalizedUrl.error());
    }
    if (!groupId.isEmpty() && !bookmarkGroupExists(groupId)) {
        return Result<QString>::failure({"browser_bookmark_group_unknown", "The bookmark group does not exist", groupId});
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString bookmarkId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_bookmarks.append({bookmarkId, groupId, normalizedName, normalizedUrl.value(), now, now});
    persistBookmarks();
    emit bookmarksChanged();
    return Result<QString>::success(bookmarkId);
}

Result<void> BrowserPlugin::updateBookmark(const QString& bookmarkId, const QString& name, const QString& address, const QString& groupId) {
    BrowserBookmark* bookmark = findBookmark(bookmarkId);
    const QString normalizedName = name.trimmed();
    const auto normalizedUrl = normalizeAddress(address);

    if (bookmark == nullptr) {
        return Result<void>::failure({"browser_bookmark_unknown", "The bookmark does not exist", bookmarkId});
    }
    if (normalizedName.isEmpty()) {
        return Result<void>::failure({"browser_bookmark_name_invalid", "The bookmark name is invalid", name});
    }
    if (!normalizedUrl.hasValue()) {
        return Result<void>::failure(normalizedUrl.error());
    }
    if (!groupId.isEmpty() && !bookmarkGroupExists(groupId)) {
        return Result<void>::failure({"browser_bookmark_group_unknown", "The bookmark group does not exist", groupId});
    }
    if (bookmark->name == normalizedName && bookmark->url == normalizedUrl.value() && bookmark->groupId == groupId) {
        return Result<void>::success();
    }

    bookmark->name = normalizedName;
    bookmark->url = normalizedUrl.value();
    bookmark->groupId = groupId;
    bookmark->updatedAtUtc = QDateTime::currentDateTimeUtc();
    persistBookmarks();
    emit bookmarksChanged();
    return Result<void>::success();
}

Result<void> BrowserPlugin::removeBookmark(const QString& bookmarkId) {
    for (int index = 0; index < m_bookmarks.size(); ++index) {
        if (m_bookmarks.at(index).id != bookmarkId) {
            continue;
        }

        m_bookmarks.removeAt(index);
        persistBookmarks();
        emit bookmarksChanged();
        return Result<void>::success();
    }

    return Result<void>::failure({"browser_bookmark_unknown", "The bookmark does not exist", bookmarkId});
}

Result<void> BrowserPlugin::applyBookmarkLayout(const QVector<QString>& orderedGroupIds, const QVector<BrowserBookmarkPlacement>& orderedBookmarks) {
    if (orderedGroupIds.size() != m_bookmarkGroups.size() || orderedBookmarks.size() != m_bookmarks.size()) {
        return Result<void>::failure({"browser_bookmark_layout_invalid", "The bookmark layout is incomplete", {}});
    }

    QSet<QString> groupIds;
    QVector<BrowserBookmarkGroup> reorderedGroups;
    reorderedGroups.reserve(m_bookmarkGroups.size());

    for (const auto& groupId : orderedGroupIds) {
        if (groupIds.contains(groupId)) {
            return Result<void>::failure({"browser_bookmark_layout_invalid", "The bookmark layout contains a duplicate group", groupId});
        }
        const BrowserBookmarkGroup* group = findBookmarkGroup(groupId);
        if (group == nullptr) {
            return Result<void>::failure({"browser_bookmark_layout_invalid", "The bookmark layout contains an unknown group", groupId});
        }
        groupIds.insert(groupId);
        reorderedGroups.append(*group);
    }

    QSet<QString> bookmarkIds;
    QVector<BrowserBookmark> reorderedBookmarks;
    reorderedBookmarks.reserve(m_bookmarks.size());
    const QDateTime now = QDateTime::currentDateTimeUtc();

    for (const auto& placement : orderedBookmarks) {
        if (bookmarkIds.contains(placement.bookmarkId) || (!placement.groupId.isEmpty() && !groupIds.contains(placement.groupId))) {
            return Result<void>::failure({"browser_bookmark_layout_invalid", "The bookmark layout contains an invalid placement", placement.bookmarkId});
        }
        const BrowserBookmark* bookmark = findBookmark(placement.bookmarkId);
        if (bookmark == nullptr) {
            return Result<void>::failure({"browser_bookmark_layout_invalid", "The bookmark layout contains an unknown bookmark", placement.bookmarkId});
        }
        BrowserBookmark reordered = *bookmark;
        if (reordered.groupId != placement.groupId) {
            reordered.groupId = placement.groupId;
            reordered.updatedAtUtc = now;
        }
        bookmarkIds.insert(placement.bookmarkId);
        reorderedBookmarks.append(std::move(reordered));
    }

    m_bookmarkGroups = std::move(reorderedGroups);
    m_bookmarks = std::move(reorderedBookmarks);
    persistBookmarks();
    emit bookmarksChanged();
    return Result<void>::success();
}

Result<void> BrowserPlugin::setHomepage(const QString& address) {
    const auto normalized = normalizeAddress(address);

    if (!normalized.hasValue()) {
        return Result<void>::failure(normalized.error());
    }

    m_homepage = normalized.value();
    persistHomepage();
    return Result<void>::success();
}

PluginHost& BrowserPlugin::host() const {
    return *m_host;
}

Result<void> BrowserPlugin::restoreState() {
    plugins::SettingsReader reader(host().settings());
    QString storedHomepage = m_homepage.toString();
    reader.readText(QStringLiteral("homepage"), storedHomepage);

    if (const auto homepage = normalizeAddress(storedHomepage); homepage.hasValue()) {
        m_homepage = homepage.value();
    }

    const auto rows = host().queryBootstrapDatabase(QStringLiteral("SELECT id, position, title, url, created_at_utc, updated_at_utc, active FROM browser_tabs ORDER BY position"));

    if (!rows.hasValue()) {
        return Result<void>::failure(rows.error());
    }

    int activeCount = 0;
    QSet<QString> tabIds;

    for (int position = 0; position < rows.value().size(); ++position) {
        const auto& row = rows.value().at(position);
        const auto url = normalizeAddress(row.value(QStringLiteral("url")).toString());
        const QDateTime created = persistence::StoredValues::parseStoredTimestamp(row.value(QStringLiteral("created_at_utc")));
        const QDateTime updated = persistence::StoredValues::parseStoredTimestamp(row.value(QStringLiteral("updated_at_utc")));
        const QString tabId = row.value(QStringLiteral("id")).toString();
        const QString title = row.value(QStringLiteral("title")).toString();
        qint64 storedPosition = -1;
        qint64 activeValue = -1;
        if (tabId.isEmpty() || tabIds.contains(tabId) || title.trimmed().isEmpty() || title != title.trimmed() || !url.hasValue() || url.value().toString() != row.value(QStringLiteral("url")).toString() || !persistence::StoredValues::readStoredInteger(row.value(QStringLiteral("position")), storedPosition) || storedPosition != position || !persistence::StoredValues::readStoredInteger(row.value(QStringLiteral("active")), activeValue) || (activeValue != 0 && activeValue != 1) || !persistence::StoredValues::validStoredTimestamp(created) || !persistence::StoredValues::validStoredTimestamp(updated) || updated < created) {
            return Result<void>::failure({"browser_session_invalid", "The saved browser session is invalid", tabId});
        }
        tabIds.insert(tabId);
        const bool active = activeValue == 1;
        activeCount += active ? 1 : 0;
        m_tabs.append({tabId, title, url.value(), created, updated, active});
    }

    if (!m_tabs.isEmpty() && activeCount != 1) {
        return Result<void>::failure({"browser_session_invalid", "The saved browser session has an invalid active tab", {}});
    }

    if (m_tabs.isEmpty()) {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        m_tabs.append({QUuid::createUuid().toString(QUuid::WithoutBraces), host().translate(QStringLiteral("browser.tabs.new")), m_homepage, now, now, true});
        const auto saved = host().executeBootstrapDatabaseTransaction({{QStringLiteral("INSERT INTO browser_tabs(id, position, title, url, created_at_utc, updated_at_utc, active) VALUES(?, 0, ?, ?, ?, ?, 1)"), {m_tabs.first().id, m_tabs.first().title, m_tabs.first().url.toString(), persistence::StoredValues::storedTimestamp(now), persistence::StoredValues::storedTimestamp(now)}}});
        if (!saved.hasValue()) {
            return saved;
        }
    }

    return restoreBookmarks();
}

Result<void> BrowserPlugin::restoreBookmarks() {
    const auto groupRows = host().queryBootstrapDatabase(QStringLiteral("SELECT id, position, name, created_at_utc, updated_at_utc FROM browser_bookmark_groups ORDER BY position"));

    if (!groupRows.hasValue()) {
        return Result<void>::failure(groupRows.error());
    }

    QSet<QString> groupIds;

    for (int position = 0; position < groupRows.value().size(); ++position) {
        const auto& row = groupRows.value().at(position);
        const QString groupId = row.value(QStringLiteral("id")).toString();
        const QString name = row.value(QStringLiteral("name")).toString();
        const QDateTime created = persistence::StoredValues::parseStoredTimestamp(row.value(QStringLiteral("created_at_utc")));
        const QDateTime updated = persistence::StoredValues::parseStoredTimestamp(row.value(QStringLiteral("updated_at_utc")));
        qint64 storedPosition = -1;
        if (groupId.isEmpty() || groupIds.contains(groupId) || name.isEmpty() || name != name.trimmed() || !persistence::StoredValues::readStoredInteger(row.value(QStringLiteral("position")), storedPosition) || storedPosition != position || !persistence::StoredValues::validStoredTimestamp(created) || !persistence::StoredValues::validStoredTimestamp(updated) || updated < created) {
            return Result<void>::failure({"browser_bookmarks_invalid", "The saved bookmark groups are invalid", groupId});
        }
        groupIds.insert(groupId);
        m_bookmarkGroups.append({groupId, name, created, updated});
    }

    const auto bookmarkRows = host().queryBootstrapDatabase(QStringLiteral("SELECT id, group_id, position, name, url, created_at_utc, updated_at_utc FROM browser_bookmarks ORDER BY CASE WHEN group_id IS NULL THEN 0 ELSE 1 END, group_id, position"));

    if (!bookmarkRows.hasValue()) {
        return Result<void>::failure(bookmarkRows.error());
    }

    QSet<QString> bookmarkIds;
    QHash<QString, int> expectedPositions;

    for (const auto& row : bookmarkRows.value()) {
        const QString bookmarkId = row.value(QStringLiteral("id")).toString();
        const QString groupId = row.value(QStringLiteral("group_id")).toString();
        const QString name = row.value(QStringLiteral("name")).toString();
        const auto url = normalizeAddress(row.value(QStringLiteral("url")).toString());
        const QDateTime created = persistence::StoredValues::parseStoredTimestamp(row.value(QStringLiteral("created_at_utc")));
        const QDateTime updated = persistence::StoredValues::parseStoredTimestamp(row.value(QStringLiteral("updated_at_utc")));
        const int expectedPosition = expectedPositions.value(groupId, 0);
        qint64 storedPosition = -1;
        if (bookmarkId.isEmpty() || bookmarkIds.contains(bookmarkId) || (!groupId.isEmpty() && !groupIds.contains(groupId)) || name.isEmpty() || name != name.trimmed() || !url.hasValue() || url.value().toString() != row.value(QStringLiteral("url")).toString() || !persistence::StoredValues::readStoredInteger(row.value(QStringLiteral("position")), storedPosition) || storedPosition != expectedPosition || !persistence::StoredValues::validStoredTimestamp(created) || !persistence::StoredValues::validStoredTimestamp(updated) || updated < created) {
            return Result<void>::failure({"browser_bookmarks_invalid", "The saved bookmarks are invalid", bookmarkId});
        }
        bookmarkIds.insert(bookmarkId);
        expectedPositions[groupId] = expectedPosition + 1;
        m_bookmarks.append({bookmarkId, groupId, name, url.value(), created, updated});
    }

    return Result<void>::success();
}

BrowserTab* BrowserPlugin::findTab(const QString& tabId) {
    for (auto& tab : m_tabs) {
        if (tab.id == tabId) {
            return &tab;
        }
    }

    return nullptr;
}

BrowserBookmarkGroup* BrowserPlugin::findBookmarkGroup(const QString& groupId) {
    for (auto& group : m_bookmarkGroups) {
        if (group.id == groupId) {
            return &group;
        }
    }

    return nullptr;
}

BrowserBookmark* BrowserPlugin::findBookmark(const QString& bookmarkId) {
    for (auto& bookmark : m_bookmarks) {
        if (bookmark.id == bookmarkId) {
            return &bookmark;
        }
    }

    return nullptr;
}

bool BrowserPlugin::bookmarkGroupExists(const QString& groupId) const {
    // clang-format off
    return std::any_of(m_bookmarkGroups.cbegin(), m_bookmarkGroups.cend(), [&groupId](const BrowserBookmarkGroup& group) { return group.id == groupId; });
    // clang-format on
}

void BrowserPlugin::persistTabs() {
    const QVector<BrowserTab> candidate = m_tabs;
    const quint64 revision = ++m_tabsRevision;
    QVector<persistence::DatabaseStatement> statements{{QStringLiteral("DELETE FROM browser_tabs"), {}}};

    for (int position = 0; position < candidate.size(); ++position) {
        const auto& tab = candidate.at(position);
        statements.append({QStringLiteral("INSERT INTO browser_tabs(id, position, title, url, created_at_utc, updated_at_utc, active) VALUES(?, ?, ?, ?, ?, ?, ?)"), {tab.id, position, tab.title, tab.url.toString(), persistence::StoredValues::storedTimestamp(tab.createdAtUtc), persistence::StoredValues::storedTimestamp(tab.updatedAtUtc), tab.active}});
    }

    auto future = host().executeDatabaseTransaction(statements);
    // clang-format off
    future.then(m_asyncContext.get(), [this, candidate, revision](Result<void> result) {
        if (result.hasValue()) {
            if (revision > m_committedTabsRevision) {
                m_committedTabs = candidate;
                m_committedTabsRevision = revision;
            }
            return;
        }
        if (revision == m_tabsRevision) {
            m_tabs = m_committedTabs;
            emit tabsChanged();
        }
        host().notify(host().translate(QStringLiteral("browser.plugin.title")), host().translate(QStringLiteral("browser.error.persistence")), AlertSeverity::Error);
    });
    // clang-format on
}

void BrowserPlugin::persistBookmarks() {
    const QVector<BrowserBookmarkGroup> candidateGroups = m_bookmarkGroups;
    const QVector<BrowserBookmark> candidateBookmarks = m_bookmarks;
    const quint64 revision = ++m_bookmarksRevision;
    QVector<persistence::DatabaseStatement> statements{{QStringLiteral("DELETE FROM browser_bookmarks"), {}}, {QStringLiteral("DELETE FROM browser_bookmark_groups"), {}}};

    for (int position = 0; position < candidateGroups.size(); ++position) {
        const auto& group = candidateGroups.at(position);
        statements.append({QStringLiteral("INSERT INTO browser_bookmark_groups(id, position, name, created_at_utc, updated_at_utc) VALUES(?, ?, ?, ?, ?)"), {group.id, position, group.name, persistence::StoredValues::storedTimestamp(group.createdAtUtc), persistence::StoredValues::storedTimestamp(group.updatedAtUtc)}});
    }

    QHash<QString, int> positions;

    for (const auto& bookmark : candidateBookmarks) {
        const int position = positions.value(bookmark.groupId, 0);
        const QVariant groupId = bookmark.groupId.isEmpty() ? QVariant{} : QVariant{bookmark.groupId};
        statements.append({QStringLiteral("INSERT INTO browser_bookmarks(id, group_id, position, name, url, created_at_utc, updated_at_utc) VALUES(?, ?, ?, ?, ?, ?, ?)"), {bookmark.id, groupId, position, bookmark.name, bookmark.url.toString(), persistence::StoredValues::storedTimestamp(bookmark.createdAtUtc), persistence::StoredValues::storedTimestamp(bookmark.updatedAtUtc)}});
        positions[bookmark.groupId] = position + 1;
    }

    auto future = host().executeDatabaseTransaction(statements);
    // clang-format off
    future.then(m_asyncContext.get(), [this, candidateGroups, candidateBookmarks, revision](Result<void> result) {
        if (result.hasValue()) {
            if (revision > m_committedBookmarksRevision) {
                m_committedBookmarkGroups = candidateGroups;
                m_committedBookmarks = candidateBookmarks;
                m_committedBookmarksRevision = revision;
            }
            return;
        }
        if (revision == m_bookmarksRevision) {
            m_bookmarkGroups = m_committedBookmarkGroups;
            m_bookmarks = m_committedBookmarks;
            emit bookmarksChanged();
        }
        host().notify(host().translate(QStringLiteral("browser.plugin.title")), host().translate(QStringLiteral("browser.error.persistence")), AlertSeverity::Error);
    });
    // clang-format on
}

void BrowserPlugin::persistHomepage() {
    const QUrl candidate = m_homepage;
    const quint64 revision = ++m_homepageRevision;
    auto future = host().saveSettings({{QStringLiteral("homepage"), candidate.toString()}});
    // clang-format off
    future.then(m_asyncContext.get(), [this, candidate, revision](Result<void> result) {
        if (result.hasValue()) {
            m_committedHomepage = candidate;
            return;
        }
        if (revision == m_homepageRevision) {
            m_homepage = m_committedHomepage;
            emit homepageChanged(m_homepage.toString());
        }
        host().notify(host().translate(QStringLiteral("browser.plugin.title")), host().translate(QStringLiteral("browser.error.persistence")), AlertSeverity::Error);
    });
    // clang-format on
}

} // namespace workpane::plugins::browser
