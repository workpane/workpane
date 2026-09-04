#include "BrowserBookmarksView.h"
#include "BrowserPlugin.h"
#include "BrowserTranslations.h"
#include "TestFuture.h"
#include "TestPluginHost.h"
#include "TestTranslations.h"
#include "persistence/StateStore.h"
#include "ui/Icons.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QLineEdit>
#include <QPromise>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <utility>

namespace workpane::plugins::browser {

TEST(BrowserPluginTest, PublishesCompleteMetadataAndNormalizesSupportedAddresses) {
    BrowserPlugin plugin;
    EXPECT_EQ(plugin.id(), QStringLiteral("browser"));
    EXPECT_EQ(plugin.titleKey(), QStringLiteral("browser.plugin.title"));
    EXPECT_TRUE(plugin.dependencies().isEmpty());
    EXPECT_EQ(plugin.navigationItems(ui::ThemeManager::instance().theme()).size(), 1);
    EXPECT_EQ(plugin.settingsGroups().size(), 1);
    const auto catalog = plugin.translations();
    EXPECT_TRUE(catalog.contains(QStringLiteral("en")));
    EXPECT_TRUE(catalog.contains(QStringLiteral("pt")));
    EXPECT_EQ(catalog.value(QStringLiteral("en")).value(QStringLiteral("browser.bookmarks.title")), QStringLiteral("Bookmarks"));
    EXPECT_EQ(catalog.value(QStringLiteral("pt")).value(QStringLiteral("browser.bookmarks.title")), QStringLiteral("Favoritos"));

    EXPECT_EQ(BrowserPlugin::normalizeAddress(QStringLiteral("example.com")).value(), QUrl(QStringLiteral("https://example.com")));
    EXPECT_EQ(BrowserPlugin::normalizeAddress(QStringLiteral("http://localhost:8080/path")).value(), QUrl(QStringLiteral("http://localhost:8080/path")));
    EXPECT_EQ(BrowserPlugin::normalizeAddress(QStringLiteral("about:blank")).value(), QUrl(QStringLiteral("about:blank")));
    EXPECT_EQ(BrowserPlugin::normalizeAddress({}).error().code, QStringLiteral("browser_address_invalid"));
    EXPECT_EQ(BrowserPlugin::normalizeAddress(QStringLiteral("javascript:alert(1)")).error().code, QStringLiteral("browser_address_invalid"));
    EXPECT_EQ(BrowserPlugin::normalizeAddress(QStringLiteral("https:///missing-host")).error().code, QStringLiteral("browser_address_invalid"));
}

class BrowserTestsHelper final {
  public:
    static test::TestPluginHost browserHost();
};

TEST(BrowserPluginTest, StartsWithTheHomepageTabWhenNoTabWasStored) {
    test::TestPluginHost host = BrowserTestsHelper::browserHost();
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_EQ(plugin.tabs().size(), 1);
    EXPECT_EQ(plugin.tabs().first().url, plugin.homepage());
    EXPECT_TRUE(plugin.tabs().first().active);
    plugin.shutdown();
}
TEST(BrowserViewTest, PresentsEitherTheTabsOrTheEmptyStateAndNeverBoth) {
    test::TestPluginHost host = BrowserTestsHelper::browserHost();
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    std::unique_ptr<QWidget> view(plugin.createNavigationView(QStringLiteral("web"), nullptr));
    ASSERT_NE(view, nullptr);
    view->resize(900, 600);
    view->show();

    auto* pages = view->findChild<QStackedWidget*>(QStringLiteral("browserPages"));
    auto* tabs = view->findChild<QTabWidget*>(QStringLiteral("browserTabs"));
    ASSERT_NE(pages, nullptr);
    ASSERT_NE(tabs, nullptr);
    ASSERT_EQ(tabs->count(), 1);
    EXPECT_EQ(pages->currentWidget(), tabs);

    ASSERT_TRUE(plugin.closeTab(plugin.tabs().first().id).hasValue());
    EXPECT_EQ(tabs->count(), 0);
    EXPECT_NE(pages->currentWidget(), tabs);
    EXPECT_TRUE(pages->currentWidget()->isVisible());
    EXPECT_FALSE(tabs->isVisible());

    ASSERT_TRUE(plugin.createTab(QUrl(QStringLiteral("about:blank")), true).hasValue());
    EXPECT_EQ(tabs->count(), 1);
    EXPECT_EQ(pages->currentWidget(), tabs);
    EXPECT_TRUE(tabs->isVisible());

    view.reset();
    plugin.shutdown();
}
TEST(BrowserPluginTest, RestoresMutatesAndPersistsTheCompleteTabSession) {
    auto host = BrowserTestsHelper::browserHost();
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_EQ(plugin.tabs().size(), 1);
    EXPECT_TRUE(plugin.tabs().first().active);
    EXPECT_EQ(host.appliedMigrations.size(), 1);
    EXPECT_EQ(host.databaseTransactions.size(), 1);

    const QString originalId = plugin.tabs().first().id;
    const auto created = plugin.createTab(QUrl(QStringLiteral("https://example.com")), true);
    ASSERT_TRUE(created.hasValue());
    ASSERT_EQ(plugin.tabs().size(), 2);
    EXPECT_TRUE(plugin.tabs().last().active);
    EXPECT_FALSE(plugin.tabs().first().active);
    EXPECT_TRUE(plugin.updateTabTitle(created.value(), QStringLiteral("Example")).hasValue());
    EXPECT_TRUE(plugin.updateTabUrl(created.value(), QUrl(QStringLiteral("https://example.org"))).hasValue());
    EXPECT_TRUE(plugin.moveTab(1, 0).hasValue());
    EXPECT_TRUE(plugin.activateTab(originalId).hasValue());
    EXPECT_TRUE(plugin.closeTab(originalId).hasValue());
    EXPECT_EQ(plugin.tabs().size(), 1);
    EXPECT_EQ(plugin.tabs().first().title, QStringLiteral("Example"));
    EXPECT_TRUE(plugin.tabs().first().active);
    EXPECT_GT(host.databaseTransactions.size(), 1);

    const QString finalId = plugin.tabs().first().id;
    EXPECT_TRUE(plugin.closeTab(finalId).hasValue());
    EXPECT_TRUE(plugin.tabs().isEmpty()) << "closing the last tab releases its renderer instead of opening a replacement";

    const auto restored = plugin.createTab(plugin.homepage(), true);
    ASSERT_TRUE(restored.hasValue());
    ASSERT_EQ(plugin.tabs().size(), 1);
    EXPECT_EQ(plugin.tabs().first().url, plugin.homepage());
    EXPECT_TRUE(plugin.tabs().first().active);

    EXPECT_EQ(plugin.closeTab(QStringLiteral("missing")).error().code, QStringLiteral("browser_tab_unknown"));
    EXPECT_EQ(plugin.activateTab(QStringLiteral("missing")).error().code, QStringLiteral("browser_tab_unknown"));
    EXPECT_EQ(plugin.moveTab(-1, 0).error().code, QStringLiteral("browser_tab_position_invalid"));
    EXPECT_EQ(plugin.updateTabTitle(plugin.tabs().first().id, {}).error().code, QStringLiteral("browser_tab_title_invalid"));
    EXPECT_EQ(plugin.setHomepage(QStringLiteral("invalid value")).error().code, QStringLiteral("browser_address_invalid"));
}
TEST(BrowserPluginTest, KeepsTabsAndBookmarksCompleteThroughManyRoundsOfMutation) {
    auto host = BrowserTestsHelper::browserHost();
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    const auto firstGroup = plugin.createBookmarkGroup(QStringLiteral("First"));
    ASSERT_TRUE(firstGroup.hasValue());
    const auto secondGroup = plugin.createBookmarkGroup(QStringLiteral("Second"));
    ASSERT_TRUE(secondGroup.hasValue());

    for (int round = 0; round < 25; ++round) {
        QStringList openedTabs;
        for (int index = 0; index < 6; ++index) {
            const auto created = plugin.createTab(QUrl(QStringLiteral("https://example.com/%1/%2").arg(round).arg(index)), index % 2 == 0);
            ASSERT_TRUE(created.hasValue());
            openedTabs.append(created.value());
        }
        ASSERT_TRUE(plugin.moveTab(static_cast<int>(plugin.tabs().size()) - 1, 0).hasValue());
        ASSERT_TRUE(plugin.activateTab(openedTabs.first()).hasValue());

        const auto bookmark = plugin.createBookmark(QStringLiteral("Entry %1").arg(round), QStringLiteral("https://example.com/%1").arg(round), round % 2 == 0 ? firstGroup.value() : secondGroup.value());
        ASSERT_TRUE(bookmark.hasValue());

        QVector<BrowserBookmarkPlacement> placements;
        for (const auto& entry : plugin.bookmarks()) {
            placements.prepend({entry.id, entry.groupId == firstGroup.value() ? secondGroup.value() : firstGroup.value()});
        }
        ASSERT_TRUE(plugin.applyBookmarkLayout({secondGroup.value(), firstGroup.value()}, placements).hasValue());

        for (const auto& tabId : openedTabs) {
            ASSERT_TRUE(plugin.closeTab(tabId).hasValue());
        }
        ASSERT_TRUE(plugin.removeBookmark(bookmark.value()).hasValue());
    }

    // Every round left the session exactly as it found it, so nothing was orphaned by a layout applied over a mutation.
    EXPECT_EQ(plugin.tabs().size(), 1);
    EXPECT_TRUE(plugin.tabs().first().active);
    EXPECT_TRUE(plugin.bookmarks().isEmpty());
    EXPECT_EQ(plugin.bookmarkGroups().size(), 2);
    plugin.shutdown();
}

TEST(BrowserPluginTest, HandlesOpenRequestsAndReportsPersistenceFailures) {
    auto host = BrowserTestsHelper::browserHost();
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    host.transactionError = Error{"database_failed", "Database failed", {}};
    ASSERT_TRUE(plugin.createTab(QUrl(QStringLiteral("https://example.com")), true).hasValue());
    QCoreApplication::processEvents();
    ASSERT_EQ(host.notifications.size(), 1);
    EXPECT_EQ(host.notifications.first().severity, AlertSeverity::Error);
    EXPECT_EQ(plugin.tabs().size(), 1);

    std::optional<Result<QJsonObject>> reply;
    // clang-format off
    plugin.handleRequest(QStringLiteral("sample"), QString::fromLatin1(plugins::openPageCapability), {{QStringLiteral("url"), QStringLiteral("qt.io")}}, [&reply](Result<QJsonObject> result) { reply = std::move(result); });
    // clang-format on
    ASSERT_TRUE(reply.has_value());
    EXPECT_TRUE(reply->hasValue());
    EXPECT_FALSE(reply->value().value(QStringLiteral("tabId")).toString().isEmpty());

    // A caller asking for a page wants to read it, so the Browser reveals itself rather than opening a tab nobody is looking at.
    EXPECT_EQ(host.revealedNavigation, QStringList{QStringLiteral("web")});

    // A request the plugin does not answer reveals nothing.
    std::optional<Result<QJsonObject>> refused;
    // clang-format off
    plugin.handleRequest(QStringLiteral("sample"), QString::fromLatin1(plugins::openPageCapability), {{QStringLiteral("url"), QStringLiteral("qt.io")}, {QStringLiteral("unknown"), true}}, [&refused](Result<QJsonObject> answer) { refused = std::move(answer); });
    // clang-format on
    ASSERT_TRUE(refused.has_value());
    EXPECT_FALSE(refused->hasValue());
    EXPECT_EQ(host.revealedNavigation.size(), 1);

    reply.reset();
    // clang-format off
    plugin.handleRequest(QStringLiteral("sample"), QStringLiteral("unknown"), {}, [&reply](Result<QJsonObject> result) { reply = std::move(result); });
    // clang-format on
    ASSERT_TRUE(reply.has_value());
    EXPECT_EQ(reply->error().code, QStringLiteral("plugin_message_topic_unknown"));
}
TEST(BrowserPluginTest, CancelsPendingPersistenceCallbacksDuringShutdown) {
    auto host = BrowserTestsHelper::browserHost();
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    auto transaction = std::make_shared<QPromise<Result<void>>>();
    transaction->start();
    // clang-format off
    host.transactionFutureHandler = [transaction](const QVector<persistence::DatabaseStatement>&) { return transaction->future(); };
    // clang-format on

    ASSERT_TRUE(plugin.createTab(QUrl(QStringLiteral("https://example.com")), true).hasValue());
    plugin.shutdown();
    transaction->addResult(Result<void>::failure({"write_failed", "Write failed", {}}));
    transaction->finish();
    QCoreApplication::processEvents();

    EXPECT_TRUE(host.notifications.isEmpty());
}
TEST(BrowserPluginTest, RejectsCorruptPersistedSessions) {
    auto host = BrowserTestsHelper::browserHost();
    // clang-format off
    host.queryHandler = [](const QString&, const QVariantList&) {
        return Result<persistence::DatabaseRows>::success({{{QStringLiteral("id"), QStringLiteral("tab")}, {QStringLiteral("position"), 0}, {QStringLiteral("title"), QStringLiteral("Broken")}, {QStringLiteral("url"), QStringLiteral("https://example.com")}, {QStringLiteral("created_at_utc"), QStringLiteral("invalid")}, {QStringLiteral("updated_at_utc"), QStringLiteral("invalid")}, {QStringLiteral("active"), 1}}});
    };
    // clang-format on
    BrowserPlugin plugin;
    EXPECT_EQ(plugin.initialize(host).error().code, QStringLiteral("browser_session_invalid"));
}
TEST(BrowserPluginTest, RestoresOrderedUrlsTitlesTimestampsAndTheActiveTab) {
    auto host = BrowserTestsHelper::browserHost();
    host.settingsDocument = {{QStringLiteral("homepage"), QStringLiteral("https://workpane.local")}};
    const QString created = QStringLiteral("2026-08-14T12:00:00.000Z");
    const QString updated = QStringLiteral("2026-08-14T12:01:00.000Z");
    // clang-format off
    host.queryHandler = [created, updated](const QString& statement, const QVariantList&) {
        if (statement.contains(QStringLiteral("browser_bookmark_groups")) || statement.contains(QStringLiteral("browser_bookmarks"))) {
            return Result<persistence::DatabaseRows>::success({});
        }
        return Result<persistence::DatabaseRows>::success({{{QStringLiteral("id"), QStringLiteral("first")}, {QStringLiteral("position"), 0}, {QStringLiteral("title"), QStringLiteral("First")}, {QStringLiteral("url"), QStringLiteral("https://example.com/one")}, {QStringLiteral("created_at_utc"), created}, {QStringLiteral("updated_at_utc"), updated}, {QStringLiteral("active"), 0}}, {{QStringLiteral("id"), QStringLiteral("second")}, {QStringLiteral("position"), 1}, {QStringLiteral("title"), QStringLiteral("Second")}, {QStringLiteral("url"), QStringLiteral("https://example.com/two")}, {QStringLiteral("created_at_utc"), created}, {QStringLiteral("updated_at_utc"), updated}, {QStringLiteral("active"), 1}}});
    };
    // clang-format on
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_EQ(plugin.tabs().size(), 2);
    EXPECT_EQ(plugin.homepage(), QUrl(QStringLiteral("https://workpane.local")));
    EXPECT_EQ(plugin.tabs().first().id, QStringLiteral("first"));
    EXPECT_EQ(plugin.tabs().last().title, QStringLiteral("Second"));
    EXPECT_EQ(plugin.tabs().last().url, QUrl(QStringLiteral("https://example.com/two")));
    EXPECT_EQ(plugin.tabs().last().createdAtUtc.timeSpec(), Qt::UTC);
    EXPECT_EQ(plugin.tabs().last().updatedAtUtc.timeSpec(), Qt::UTC);
    EXPECT_TRUE(plugin.tabs().last().active);
}
TEST(BrowserPluginTest, ManagesOrderedBookmarkGroupsAndBookmarksWithStrictValidation) {
    auto host = BrowserTestsHelper::browserHost();
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(plugin.bookmarkGroups().isEmpty());
    ASSERT_TRUE(plugin.bookmarks().isEmpty());
    ASSERT_EQ(host.appliedMigrations.size(), 1);
    EXPECT_TRUE(host.appliedMigrations.first().statements.join(QLatin1Char(' ')).contains(QStringLiteral("browser_bookmark_groups")));
    EXPECT_TRUE(host.appliedMigrations.first().statements.join(QLatin1Char(' ')).contains(QStringLiteral("browser_bookmarks")));

    const auto work = plugin.createBookmarkGroup(QStringLiteral(" Work "));
    const auto personal = plugin.createBookmarkGroup(QStringLiteral("Personal"));
    ASSERT_TRUE(work.hasValue());
    ASSERT_TRUE(personal.hasValue());
    EXPECT_EQ(plugin.bookmarkGroups().first().name, QStringLiteral("Work"));
    EXPECT_EQ(plugin.createBookmarkGroup(QStringLiteral("  ")).error().code, QStringLiteral("browser_bookmark_group_name_invalid"));
    EXPECT_EQ(plugin.updateBookmarkGroup(QStringLiteral("missing"), QStringLiteral("Name")).error().code, QStringLiteral("browser_bookmark_group_unknown"));
    EXPECT_EQ(plugin.updateBookmarkGroup(work.value(), {}).error().code, QStringLiteral("browser_bookmark_group_name_invalid"));
    ASSERT_TRUE(plugin.updateBookmarkGroup(work.value(), QStringLiteral("Engineering")).hasValue());
    EXPECT_EQ(plugin.bookmarkGroups().first().name, QStringLiteral("Engineering"));

    const auto qt = plugin.createBookmark(QStringLiteral(" Qt "), QStringLiteral("qt.io"), work.value());
    const auto news = plugin.createBookmark(QStringLiteral("News"), QStringLiteral("https://example.com/news"), {});
    ASSERT_TRUE(qt.hasValue());
    ASSERT_TRUE(news.hasValue());
    ASSERT_EQ(plugin.bookmarks().size(), 2);
    EXPECT_EQ(plugin.bookmarks().first().name, QStringLiteral("Qt"));
    EXPECT_EQ(plugin.bookmarks().first().url, QUrl(QStringLiteral("https://qt.io")));
    EXPECT_EQ(plugin.bookmarks().first().createdAtUtc.timeSpec(), Qt::UTC);
    EXPECT_EQ(plugin.createBookmark({}, QStringLiteral("https://example.com"), {}).error().code, QStringLiteral("browser_bookmark_name_invalid"));
    EXPECT_EQ(plugin.createBookmark(QStringLiteral("Invalid"), QStringLiteral("javascript:alert(1)"), {}).error().code, QStringLiteral("browser_address_invalid"));
    EXPECT_EQ(plugin.createBookmark(QStringLiteral("Invalid"), QStringLiteral("https://example.com"), QStringLiteral("missing")).error().code, QStringLiteral("browser_bookmark_group_unknown"));

    ASSERT_TRUE(plugin.updateBookmark(qt.value(), QStringLiteral("Qt Docs"), QStringLiteral("https://doc.qt.io"), personal.value()).hasValue());
    EXPECT_EQ(plugin.bookmarks().first().groupId, personal.value());
    EXPECT_EQ(plugin.bookmarks().first().url, QUrl(QStringLiteral("https://doc.qt.io")));
    EXPECT_EQ(plugin.updateBookmark(QStringLiteral("missing"), QStringLiteral("Name"), QStringLiteral("https://example.com"), {}).error().code, QStringLiteral("browser_bookmark_unknown"));
    EXPECT_EQ(plugin.updateBookmark(qt.value(), {}, QStringLiteral("https://example.com"), {}).error().code, QStringLiteral("browser_bookmark_name_invalid"));
    EXPECT_EQ(plugin.updateBookmark(qt.value(), QStringLiteral("Name"), QStringLiteral("invalid value"), {}).error().code, QStringLiteral("browser_address_invalid"));
    EXPECT_EQ(plugin.updateBookmark(qt.value(), QStringLiteral("Name"), QStringLiteral("https://example.com"), QStringLiteral("missing")).error().code, QStringLiteral("browser_bookmark_group_unknown"));

    const QVector<QString> groupOrder{personal.value(), work.value()};
    const QVector<BrowserBookmarkPlacement> bookmarkOrder{{news.value(), personal.value()}, {qt.value(), {}}};
    ASSERT_TRUE(plugin.applyBookmarkLayout(groupOrder, bookmarkOrder).hasValue());
    EXPECT_EQ(plugin.bookmarkGroups().first().id, personal.value());
    EXPECT_EQ(plugin.bookmarks().first().id, news.value());
    EXPECT_EQ(plugin.bookmarks().first().groupId, personal.value());
    EXPECT_TRUE(plugin.bookmarks().last().groupId.isEmpty());
    EXPECT_EQ(plugin.applyBookmarkLayout({personal.value()}, bookmarkOrder).error().code, QStringLiteral("browser_bookmark_layout_invalid"));
    EXPECT_EQ(plugin.applyBookmarkLayout({personal.value(), personal.value()}, bookmarkOrder).error().code, QStringLiteral("browser_bookmark_layout_invalid"));
    EXPECT_EQ(plugin.applyBookmarkLayout({personal.value(), QStringLiteral("missing")}, bookmarkOrder).error().code, QStringLiteral("browser_bookmark_layout_invalid"));
    EXPECT_EQ(plugin.applyBookmarkLayout(groupOrder, {{news.value(), {}}, {news.value(), {}}}).error().code, QStringLiteral("browser_bookmark_layout_invalid"));
    EXPECT_EQ(plugin.applyBookmarkLayout(groupOrder, {{news.value(), QStringLiteral("missing")}, {qt.value(), {}}}).error().code, QStringLiteral("browser_bookmark_layout_invalid"));
    EXPECT_EQ(plugin.applyBookmarkLayout(groupOrder, {{QStringLiteral("missing"), {}}, {qt.value(), {}}}).error().code, QStringLiteral("browser_bookmark_layout_invalid"));

    ASSERT_TRUE(plugin.removeBookmarkGroup(personal.value()).hasValue());
    EXPECT_TRUE(plugin.bookmarks().first().groupId.isEmpty());
    EXPECT_EQ(plugin.removeBookmarkGroup(QStringLiteral("missing")).error().code, QStringLiteral("browser_bookmark_group_unknown"));
    ASSERT_TRUE(plugin.removeBookmark(news.value()).hasValue());
    EXPECT_EQ(plugin.removeBookmark(QStringLiteral("missing")).error().code, QStringLiteral("browser_bookmark_unknown"));
    EXPECT_EQ(plugin.bookmarks().size(), 1);
    EXPECT_GT(host.databaseTransactions.size(), 6);
}
// Removing a group keeps every bookmark it held, in the order they were in, because a group is a way of arranging them and not a thing that owns them.
TEST(BrowserPluginTest, KeepsEveryBookmarkOfAGroupInOrderWhenThatGroupIsRemoved) {
    auto host = BrowserTestsHelper::browserHost();
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    const auto group = plugin.createBookmarkGroup(QStringLiteral("Reading"));
    ASSERT_TRUE(group.hasValue());
    const auto loose = plugin.createBookmark(QStringLiteral("Loose"), QStringLiteral("https://example.com/loose"), {});
    const auto first = plugin.createBookmark(QStringLiteral("First"), QStringLiteral("https://example.com/first"), group.value());
    const auto second = plugin.createBookmark(QStringLiteral("Second"), QStringLiteral("https://example.com/second"), group.value());
    const auto third = plugin.createBookmark(QStringLiteral("Third"), QStringLiteral("https://example.com/third"), group.value());
    ASSERT_TRUE(loose.hasValue() && first.hasValue() && second.hasValue() && third.hasValue());
    ASSERT_EQ(plugin.bookmarks().size(), 4);

    const QVector<QString> before = QVector<QString>{first.value(), second.value(), third.value()};
    ASSERT_TRUE(plugin.removeBookmarkGroup(group.value()).hasValue());
    EXPECT_TRUE(plugin.bookmarkGroups().isEmpty());
    ASSERT_EQ(plugin.bookmarks().size(), 4);

    QVector<QString> after;

    for (const auto& bookmark : plugin.bookmarks()) {
        EXPECT_TRUE(bookmark.groupId.isEmpty()) << bookmark.name.toStdString();
        if (before.contains(bookmark.id)) {
            after.append(bookmark.id);
        }
    }

    EXPECT_EQ(after, before);

    // What is written back numbers the ungrouped collection from zero without a gap, which is what the next start demands of it.
    ASSERT_FALSE(host.databaseTransactions.isEmpty());
    QVector<int> written;

    for (const auto& statement : host.databaseTransactions.constLast()) {
        if (statement.statement.contains(QStringLiteral("INSERT INTO browser_bookmarks"))) {
            written.append(statement.bindings.at(2).toInt());
        }
    }

    ASSERT_EQ(written.size(), 4);

    for (int index = 0; index < written.size(); ++index) {
        EXPECT_EQ(written.at(index), index);
    }
}

// Two snapshots of the tabs can be in flight at once, so the one written last is what the next start reads whatever order the answers arrive in.
TEST(BrowserPluginTest, KeepsTheSnapshotWrittenLastWhenTwoOfThemAnswerOutOfOrder) {
    auto host = BrowserTestsHelper::browserHost();
    QVector<std::shared_ptr<QPromise<Result<void>>>> held;
    // clang-format off
    host.transactionFutureHandler = [&held](const QVector<persistence::DatabaseStatement>&) {
        auto pending = std::make_shared<QPromise<Result<void>>>();
        pending->start();
        held.append(pending);
        return pending->future();
    };
    // clang-format on

    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(plugin.createTab(QUrl(QStringLiteral("https://example.com/first")), true).hasValue());
    ASSERT_TRUE(plugin.createTab(QUrl(QStringLiteral("https://example.com/second")), true).hasValue());
    ASSERT_GE(held.size(), 2);
    const qsizetype tabsAfterBoth = plugin.tabs().size();

    // The newer write answers first and the older one answers after it, which is what a queue of two may really do.
    held.constLast()->addResult(Result<void>::success());
    held.constLast()->finish();
    held.constFirst()->addResult(Result<void>::success());
    held.constFirst()->finish();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&plugin, tabsAfterBoth]() { return plugin.tabs().size() == tabsAfterBoth; }));
    // clang-format on

    // The state the reader sees is the later one, and a failure now rolls back to it rather than to the earlier snapshot.
    ASSERT_EQ(plugin.tabs().size(), tabsAfterBoth);
    EXPECT_EQ(plugin.tabs().constLast().url, QUrl(QStringLiteral("https://example.com/second")));

    held.clear();
    ASSERT_TRUE(plugin.createTab(QUrl(QStringLiteral("https://example.com/third")), true).hasValue());
    ASSERT_EQ(held.size(), 1);
    const qsizetype told = host.notifications.size();
    held.constFirst()->addResult(Result<void>::failure({"browser_persistence", "no", {}}));
    held.constFirst()->finish();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&host, told]() { return host.notifications.size() > told; }));
    // clang-format on
    EXPECT_EQ(plugin.tabs().size(), tabsAfterBoth);
    EXPECT_EQ(plugin.tabs().constLast().url, QUrl(QStringLiteral("https://example.com/second")));
}

TEST(BrowserPluginTest, RestoresCompleteBookmarkStateAndRejectsCorruptRows) {
    const QString created = QStringLiteral("2026-08-14T12:00:00.000Z");
    const QString updated = QStringLiteral("2026-08-14T12:01:00.000Z");
    auto host = BrowserTestsHelper::browserHost();
    // clang-format off
    host.queryHandler = [created, updated](const QString& statement, const QVariantList&) {
        if (statement.contains(QStringLiteral("browser_tabs"))) {
            return Result<persistence::DatabaseRows>::success({});
        }
        if (statement.contains(QStringLiteral("browser_bookmark_groups"))) {
            return Result<persistence::DatabaseRows>::success({{{QStringLiteral("id"), QStringLiteral("work")}, {QStringLiteral("position"), 0}, {QStringLiteral("name"), QStringLiteral("Work")}, {QStringLiteral("created_at_utc"), created}, {QStringLiteral("updated_at_utc"), updated}}});
        }
        if (statement.contains(QStringLiteral("browser_bookmarks"))) {
            return Result<persistence::DatabaseRows>::success({{{QStringLiteral("id"), QStringLiteral("ungrouped")}, {QStringLiteral("group_id"), QVariant{}}, {QStringLiteral("position"), 0}, {QStringLiteral("name"), QStringLiteral("Workpane")}, {QStringLiteral("url"), QStringLiteral("https://workpane.local")}, {QStringLiteral("created_at_utc"), created}, {QStringLiteral("updated_at_utc"), updated}}, {{QStringLiteral("id"), QStringLiteral("grouped")}, {QStringLiteral("group_id"), QStringLiteral("work")}, {QStringLiteral("position"), 0}, {QStringLiteral("name"), QStringLiteral("Qt")}, {QStringLiteral("url"), QStringLiteral("https://qt.io")}, {QStringLiteral("created_at_utc"), created}, {QStringLiteral("updated_at_utc"), updated}}});
        }
        return Result<persistence::DatabaseRows>::failure({"unexpected_query", "Unexpected browser query", statement});
    };
    // clang-format on
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_EQ(plugin.bookmarkGroups().size(), 1);
    ASSERT_EQ(plugin.bookmarks().size(), 2);
    EXPECT_EQ(plugin.bookmarkGroups().first().id, QStringLiteral("work"));
    EXPECT_TRUE(plugin.bookmarks().first().groupId.isEmpty());
    EXPECT_EQ(plugin.bookmarks().last().groupId, QStringLiteral("work"));
    EXPECT_EQ(plugin.bookmarks().last().updatedAtUtc.timeSpec(), Qt::UTC);

    auto corruptHost = BrowserTestsHelper::browserHost();
    // clang-format off
    corruptHost.queryHandler = [created, updated](const QString& statement, const QVariantList&) {
        if (statement.contains(QStringLiteral("browser_tabs")) || statement.contains(QStringLiteral("browser_bookmark_groups"))) {
            return Result<persistence::DatabaseRows>::success({});
        }
        return Result<persistence::DatabaseRows>::success({{{QStringLiteral("id"), QStringLiteral("orphan")}, {QStringLiteral("group_id"), QStringLiteral("missing")}, {QStringLiteral("position"), 0}, {QStringLiteral("name"), QStringLiteral("Orphan")}, {QStringLiteral("url"), QStringLiteral("https://example.com")}, {QStringLiteral("created_at_utc"), created}, {QStringLiteral("updated_at_utc"), updated}}});
    };
    // clang-format on
    BrowserPlugin corruptPlugin;
    EXPECT_EQ(corruptPlugin.initialize(corruptHost).error().code, QStringLiteral("browser_bookmarks_invalid"));
}
TEST(BrowserPluginTest, RollsBackBookmarkMutationsWhenAsynchronousPersistenceFails) {
    auto host = BrowserTestsHelper::browserHost();
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(plugin.createBookmarkGroup(QStringLiteral("Committed")).hasValue());
    QCoreApplication::processEvents();
    ASSERT_EQ(plugin.bookmarkGroups().size(), 1);
    host.transactionError = Error{"database_failed", "Database failed", {}};

    ASSERT_TRUE(plugin.createBookmark(QStringLiteral("Transient"), QStringLiteral("https://example.com"), plugin.bookmarkGroups().first().id).hasValue());
    QCoreApplication::processEvents();
    EXPECT_TRUE(plugin.bookmarks().isEmpty());
    EXPECT_EQ(plugin.bookmarkGroups().size(), 1);
    ASSERT_EQ(host.notifications.size(), 1);
    EXPECT_EQ(host.notifications.first().severity, AlertSeverity::Error);
}
TEST(BrowserBookmarksViewTest, PresentsGroupsAndOpensTheSelectedBookmarkInEitherTarget) {
    auto host = BrowserTestsHelper::browserHost();
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    const auto group = plugin.createBookmarkGroup(QStringLiteral("Work"));
    ASSERT_TRUE(group.hasValue());
    ASSERT_TRUE(plugin.createBookmark(QStringLiteral("Qt"), QStringLiteral("https://qt.io"), group.value()).hasValue());

    BrowserBookmarksView view(plugin);
    auto* tree = view.findChild<QTreeWidget*>(QStringLiteral("browserBookmarksTree"));
    auto* openCurrent = view.findChild<QPushButton*>(QStringLiteral("browserBookmarkOpenCurrent"));
    auto* openNew = view.findChild<QPushButton*>(QStringLiteral("browserBookmarkOpenNew"));
    ASSERT_NE(tree, nullptr);
    ASSERT_NE(openCurrent, nullptr);
    ASSERT_NE(openNew, nullptr);
    ASSERT_EQ(tree->topLevelItemCount(), 2);
    EXPECT_EQ(tree->topLevelItem(0)->text(0), QStringLiteral("Ungrouped"));
    ASSERT_EQ(tree->topLevelItem(1)->childCount(), 1);
    QTreeWidgetItem* bookmark = tree->topLevelItem(1)->child(0);

    // A glyph beside a name follows the selection of its row, otherwise it sits on the accent in a colour that reads through it.
    EXPECT_EQ(bookmark->icon(0).pixmap(32, 32).toImage(), ui::IconCatalog::icon(ui::IconName::Bookmark, plugin.host().theme().color(ui::ThemeColor::Text)).pixmap(32, 32).toImage());
    tree->setCurrentItem(bookmark);
    QCoreApplication::processEvents();
    EXPECT_EQ(bookmark->icon(0).pixmap(32, 32).toImage(), ui::IconCatalog::icon(ui::IconName::Bookmark, plugin.host().theme().color(ui::ThemeColor::OnAccent)).pixmap(32, 32).toImage());

    QSignalSpy opened(&view, &BrowserBookmarksView::openRequested);
    openCurrent->click();
    openNew->click();
    ASSERT_EQ(opened.count(), 2);
    EXPECT_EQ(opened.at(0).at(0).toUrl(), QUrl(QStringLiteral("https://qt.io")));
    EXPECT_FALSE(opened.at(0).at(1).toBool());
    EXPECT_TRUE(opened.at(1).at(1).toBool());
}

// The tree is where a drag becomes a layout, so what the reader dragged is what the plugin is asked to keep.
TEST(BrowserBookmarksViewTest, TurnsWhatWasDraggedInTheTreeIntoTheLayoutThePluginKeeps) {
    auto host = BrowserTestsHelper::browserHost();
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    const auto work = plugin.createBookmarkGroup(QStringLiteral("Work"));
    const auto reading = plugin.createBookmarkGroup(QStringLiteral("Reading"));
    ASSERT_TRUE(work.hasValue());
    ASSERT_TRUE(reading.hasValue());
    const auto qt = plugin.createBookmark(QStringLiteral("Qt"), QStringLiteral("https://qt.io"), work.value());
    const auto news = plugin.createBookmark(QStringLiteral("News"), QStringLiteral("https://example.com"), work.value());
    ASSERT_TRUE(qt.hasValue());
    ASSERT_TRUE(news.hasValue());

    BrowserBookmarksView view(plugin);
    auto* tree = view.findChild<QTreeWidget*>(QStringLiteral("browserBookmarksTree"));
    ASSERT_NE(tree, nullptr);

    // clang-format off
    const auto groupItem = [tree](const QString& title) -> QTreeWidgetItem* {
        for (int index = 0; index < tree->topLevelItemCount(); ++index) {
            if (tree->topLevelItem(index)->text(0) == title) {
                return tree->topLevelItem(index);
            }
        }
        return nullptr;
    };
    const auto groupOf = [&plugin](const QString& bookmarkId) {
        QString found;
        for (const auto& bookmark : plugin.bookmarks()) {
            if (bookmark.id == bookmarkId) {
                found = bookmark.groupId;
            }
        }
        return found;
    };
    // clang-format on

    QTreeWidgetItem* source = groupItem(QStringLiteral("Work"));
    QTreeWidgetItem* destination = groupItem(QStringLiteral("Reading"));
    ASSERT_NE(source, nullptr);
    ASSERT_NE(destination, nullptr);
    ASSERT_EQ(source->childCount(), 2);

    // The reader drags the second bookmark of one group into another, which is what a drop leaves behind.
    destination->addChild(source->takeChild(1));
    ASSERT_TRUE(QMetaObject::invokeMethod(&view, "applyTreeLayout"));

    EXPECT_EQ(groupOf(news.value()), reading.value());
    EXPECT_EQ(groupOf(qt.value()), work.value());

    // A tree that lost a bookmark is not a layout, so the stored one is kept and the panel is built again from it.
    QTreeWidgetItem* moved = groupItem(QStringLiteral("Reading"));
    ASSERT_NE(moved, nullptr);
    ASSERT_EQ(moved->childCount(), 1);
    delete moved->takeChild(0);
    ASSERT_TRUE(QMetaObject::invokeMethod(&view, "applyTreeLayout"));

    EXPECT_EQ(groupOf(news.value()), reading.value());
    EXPECT_EQ(plugin.bookmarks().size(), 2);
    auto* rebuilt = groupItem(QStringLiteral("Reading"));
    ASSERT_NE(rebuilt, nullptr);
    EXPECT_EQ(rebuilt->childCount(), 1);
}

// A list that opens with the choice meaning none keeps that choice first and sorts every name after it.
TEST(BrowserBookmarksViewTest, OffersTheUngroupedChoiceFirstAndTheGroupsSortedAfterIt) {
    auto host = BrowserTestsHelper::browserHost();
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(plugin.createBookmarkGroup(QStringLiteral("Zeta")).hasValue());
    const auto beta = plugin.createBookmarkGroup(QStringLiteral("beta"));
    ASSERT_TRUE(beta.hasValue());
    ASSERT_TRUE(plugin.createBookmarkGroup(QStringLiteral("Alpha")).hasValue());

    BrowserBookmarksView view(plugin);
    QStringList offered;
    // clang-format off
    QTimer::singleShot(0, qApp, [&offered, &beta]() {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(dialog, nullptr);
        auto* group = dialog->findChild<QComboBox*>(QStringLiteral("browserBookmarkGroup"));
        auto* name = dialog->findChild<QLineEdit*>(QStringLiteral("browserBookmarkName"));
        auto* save = dialog->findChild<QPushButton*>(QStringLiteral("primaryButton"));
        ASSERT_NE(group, nullptr);
        ASSERT_NE(name, nullptr);
        ASSERT_NE(save, nullptr);
        for (int index = 0; index < group->count(); ++index) {
            offered.append(group->itemText(index));
        }
        group->setCurrentIndex(group->findData(beta.value()));
        name->setText(QStringLiteral("Docs"));
        save->click();
    });
    // clang-format on

    view.beginAddBookmark(QStringLiteral("Docs"), QUrl(QStringLiteral("https://qt.io")));

    // The choice that means none opens the list, and the names after it are ordered by folding their case.
    EXPECT_EQ(offered, QStringList({host.translate(QStringLiteral("browser.bookmarks.ungrouped")), QStringLiteral("Alpha"), QStringLiteral("beta"), QStringLiteral("Zeta")}));

    // What was typed reached the plugin as a bookmark of the group that was chosen.
    ASSERT_EQ(plugin.bookmarks().size(), 1);
    EXPECT_EQ(plugin.bookmarks().first().name, QStringLiteral("Docs"));
    EXPECT_EQ(plugin.bookmarks().first().url, QUrl(QStringLiteral("https://qt.io")));
    EXPECT_EQ(plugin.bookmarks().first().groupId, beta.value());
}

// A double reproduces the statement and not the semantics that answer it, so what the browser keeps is written to a real database and read from it.
TEST(BrowserPluginTest, KeepsItsTabsAndBookmarksThroughARealDatabase) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    persistence::StateStore store(directory.filePath(QStringLiteral("workpane.sqlite3")));
    ASSERT_TRUE(store.initialize().hasValue());

    QString workGroupId;
    QString bookmarkId;
    QString secondTabId;
    {
        auto host = BrowserTestsHelper::browserHost();
        host.useDatabase(store, QStringLiteral("browser"));
        BrowserPlugin plugin;
        ASSERT_TRUE(plugin.initialize(host).hasValue());

        ASSERT_TRUE(plugin.setHomepage(QStringLiteral("https://workpane.local/start")).hasValue());
        const auto second = plugin.createTab(QUrl(QStringLiteral("https://qt.io/docs")), true);
        ASSERT_TRUE(second.hasValue());
        secondTabId = second.value();
        ASSERT_TRUE(plugin.updateTabTitle(secondTabId, QStringLiteral("Qt Documentation")).hasValue());

        const auto group = plugin.createBookmarkGroup(QStringLiteral("Work"));
        ASSERT_TRUE(group.hasValue());
        workGroupId = group.value();
        const auto bookmark = plugin.createBookmark(QStringLiteral("Qt"), QStringLiteral("https://qt.io"), workGroupId);
        ASSERT_TRUE(bookmark.hasValue());
        bookmarkId = bookmark.value();
        plugin.shutdown();
    }

    // A second start reads what the first one wrote, which is what a restart really does.
    auto host = BrowserTestsHelper::browserHost();
    host.useDatabase(store, QStringLiteral("browser"));
    BrowserPlugin reopened;
    ASSERT_TRUE(reopened.initialize(host).hasValue());

    ASSERT_EQ(reopened.tabs().size(), 2);
    const BrowserTab* restored = nullptr;
    for (const auto& tab : reopened.tabs()) {
        if (tab.id == secondTabId) {
            restored = &tab;
        }
    }
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->title, QStringLiteral("Qt Documentation"));
    EXPECT_EQ(restored->url, QUrl(QStringLiteral("https://qt.io/docs")));
    EXPECT_TRUE(restored->active);
    EXPECT_TRUE(restored->createdAtUtc.isValid());
    EXPECT_TRUE(restored->updatedAtUtc.isValid());

    ASSERT_EQ(reopened.bookmarkGroups().size(), 1);
    EXPECT_EQ(reopened.bookmarkGroups().first().id, workGroupId);
    EXPECT_EQ(reopened.bookmarkGroups().first().name, QStringLiteral("Work"));
    ASSERT_EQ(reopened.bookmarks().size(), 1);
    EXPECT_EQ(reopened.bookmarks().first().id, bookmarkId);
    EXPECT_EQ(reopened.bookmarks().first().name, QStringLiteral("Qt"));
    EXPECT_EQ(reopened.bookmarks().first().url, QUrl(QStringLiteral("https://qt.io")));
    EXPECT_EQ(reopened.bookmarks().first().groupId, workGroupId);
    reopened.shutdown();
}

test::TestPluginHost BrowserTestsHelper::browserHost() {
    test::TestPluginHost host;
    host.dataPath = QDir::tempPath();
    host.translations = translations::BrowserCatalog::english();
    // clang-format off
    host.queryHandler = [](const QString& statement, const QVariantList&) {
        if (statement.contains(QStringLiteral("browser_tabs"))) {
            return Result<persistence::DatabaseRows>::success({});
        }
        if (statement.contains(QStringLiteral("browser_bookmark_groups")) || statement.contains(QStringLiteral("browser_bookmarks"))) {
            return Result<persistence::DatabaseRows>::success({});
        }
        return Result<persistence::DatabaseRows>::failure({"unexpected_query", "Unexpected browser query", statement});
    };
    // clang-format on
    return host;
}
} // namespace workpane::plugins::browser

TEST(BrowserTranslationsTest, SpellsEveryKeyInEveryLanguageTheSelectorOffers) {
    workpane::plugins::browser::BrowserPlugin plugin;
    workpane::test::TestCatalogs::expectCompleteCatalog(QStringLiteral("browser"), plugin.translations());
}
