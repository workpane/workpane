#include "LogsPlugin.h"
#include "TestFuture.h"
#include "TestPluginHost.h"
#include "TestTranslations.h"
#include "persistence/StateStore.h"

#include <QLabel>
#include <QLayout>
#include <QPromise>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <memory>

namespace workpane::plugins::logs {

TEST(LogsPluginTest, ValidatesPaginationClearingAndMessages) {
    test::TestPluginHost host;
    LogsPlugin plugin;
    EXPECT_EQ(test::TestFutures::awaitFuture(plugin.entries(0, 100)).error().code, QStringLiteral("logs_page_invalid"));
    EXPECT_EQ(test::TestFutures::awaitFuture(plugin.clearEntries()).error().code, QStringLiteral("logs_unavailable"));
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    EXPECT_EQ(test::TestFutures::awaitFuture(plugin.entries(-1, 100)).error().code, QStringLiteral("logs_page_invalid"));
    EXPECT_EQ(test::TestFutures::awaitFuture(plugin.entries(0, 101)).error().code, QStringLiteral("logs_page_invalid"));
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.clearEntries()).hasValue());
    ASSERT_EQ(host.databaseExecutions.size(), 1);

    bool replied = false;
    // clang-format off
    const auto captureReply = [&replied](Result<QJsonObject> result) {
        replied = true;
        EXPECT_EQ(result.error().code, QStringLiteral("plugin_message_topic_unknown"));
    };
    // clang-format on
    plugin.handleRequest(QStringLiteral("terminal"), QStringLiteral("invalid"), {}, captureReply);
    plugin.handleRequest(QStringLiteral("terminal"), QStringLiteral("logs.entries.page"), {{QStringLiteral("beforeSequence"), 1.5}, {QStringLiteral("limit"), 100}}, captureReply);
    EXPECT_TRUE(replied);
}

TEST(LogsPluginTest, ParsesRowsAndRejectsCorruptStoredDetails) {
    test::TestPluginHost host;
    LogsPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    host.databaseRows = {{{QStringLiteral("sequence"), 1}, {QStringLiteral("timestamp_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}, {QStringLiteral("source_plugin_id"), QStringLiteral("terminal")}, {QStringLiteral("level"), QStringLiteral("error")}, {QStringLiteral("category"), QStringLiteral("terminal.process")}, {QStringLiteral("message"), QStringLiteral("Exited")}, {QStringLiteral("details_json"), QStringLiteral("{}")}}};
    const auto entries = test::TestFutures::awaitFuture(plugin.entries(0, 100));
    ASSERT_TRUE(entries.hasValue());
    ASSERT_EQ(entries.value().size(), 1);
    EXPECT_EQ(entries.value().first().sourcePluginId, QStringLiteral("terminal"));

    const QStringList invalidFields{QStringLiteral("sequence"), QStringLiteral("timestamp_utc"), QStringLiteral("source_plugin_id"), QStringLiteral("level"), QStringLiteral("category"), QStringLiteral("message"), QStringLiteral("details_json")};
    const QVariantList invalidValues{0, QStringLiteral("2026-08-14T12:00:00.000-03:00"), QString{}, QStringLiteral("fatal"), QString{}, QString{}, QStringLiteral("[")};

    for (qsizetype index = 0; index < invalidFields.size(); ++index) {
        auto corrupted = host.databaseRows;
        corrupted.first()[invalidFields.at(index)] = invalidValues.at(index);
        host.databaseRows = corrupted;
        EXPECT_EQ(test::TestFutures::awaitFuture(plugin.entries(0, 100)).error().code, QStringLiteral("logs_entry_invalid"));
        host.databaseRows = {{{QStringLiteral("sequence"), 1}, {QStringLiteral("timestamp_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}, {QStringLiteral("source_plugin_id"), QStringLiteral("terminal")}, {QStringLiteral("level"), QStringLiteral("error")}, {QStringLiteral("category"), QStringLiteral("terminal.process")}, {QStringLiteral("message"), QStringLiteral("Exited")}, {QStringLiteral("details_json"), QStringLiteral("{}")}}};
    }

    host.databaseRows.first()[QStringLiteral("sequence")] = 1.5;
    EXPECT_EQ(test::TestFutures::awaitFuture(plugin.entries(0, 100)).error().code, QStringLiteral("logs_entry_invalid"));
}

TEST(LogsPluginTest, RequiresConfirmationBeforeClearingFromTheViewer) {
    test::TestPluginHost host;
    host.databaseRows = {{{QStringLiteral("sequence"), 1}, {QStringLiteral("timestamp_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}, {QStringLiteral("source_plugin_id"), QStringLiteral("terminal")}, {QStringLiteral("level"), QStringLiteral("error")}, {QStringLiteral("category"), QStringLiteral("terminal.process")}, {QStringLiteral("message"), QStringLiteral("Exited")}, {QStringLiteral("details_json"), QStringLiteral("{}")}}};
    LogsPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    std::unique_ptr<QWidget> view(plugin.createNavigationView(QStringLiteral("viewer"), nullptr));
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(view->layout()->contentsMargins(), QMargins());
    auto* table = view->findChild<QTableWidget*>(QStringLiteral("logsTable"));
    ASSERT_NE(table, nullptr);
    view->resize(900, 600);
    view->show();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([table]() { return table->rowCount() == 1; }));
    // clang-format on
    EXPECT_EQ(table->geometry().left(), 0);
    EXPECT_EQ(table->geometry().right(), view->rect().right());
    EXPECT_EQ(table->geometry().bottom(), view->rect().bottom());
    QPushButton* clear = nullptr;

    for (auto* button : view->findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("logs.viewer.clear")) {
            clear = button;
            break;
        }
    }

    ASSERT_NE(clear, nullptr);

    host.confirmation = false;
    QTest::mouseClick(clear, Qt::LeftButton);
    EXPECT_TRUE(host.databaseExecutions.isEmpty());

    host.confirmation = true;
    host.databaseRows.clear();
    QTest::mouseClick(clear, Qt::LeftButton);
    ASSERT_EQ(host.databaseExecutions.size(), 1);
    EXPECT_EQ(host.databaseExecutions.first().value(QStringLiteral("statement")).toString(), QStringLiteral("DELETE FROM logs_entries"));

    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([table]() { return table->rowCount() == 0 && table->isHidden(); }));
    // clang-format on
    auto* empty = view->findChild<QLabel*>(QStringLiteral("emptyState"));
    ASSERT_NE(empty, nullptr);
    EXPECT_TRUE(empty->isVisible());
}

TEST(LogsPluginTest, IgnoresStalePagesWhenAViewerReloadOvertakesAnEarlierQuery) {
    test::TestPluginHost host;
    LogsPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    auto firstPage = std::make_shared<QPromise<Result<persistence::DatabaseRows>>>();
    auto secondPage = std::make_shared<QPromise<Result<persistence::DatabaseRows>>>();
    firstPage->start();
    secondPage->start();
    int queryCount = 0;
    // clang-format off
    host.queryFutureHandler = [firstPage, secondPage, &queryCount](const QString&, const QVariantList&) {
        ++queryCount;
        return queryCount == 1 ? firstPage->future() : secondPage->future();
    };
    // clang-format on
    std::unique_ptr<QWidget> view(plugin.createNavigationView(QStringLiteral("viewer"), nullptr));
    ASSERT_NE(view, nullptr);
    ASSERT_EQ(queryCount, 1);
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.clearEntries()).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return queryCount == 2; }, 1000));
    // clang-format on

    const QString timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    persistence::DatabaseRows newer{{{QStringLiteral("sequence"), 2}, {QStringLiteral("timestamp_utc"), timestamp}, {QStringLiteral("source_plugin_id"), QStringLiteral("browser")}, {QStringLiteral("level"), QStringLiteral("info")}, {QStringLiteral("category"), QStringLiteral("browser.tabs")}, {QStringLiteral("message"), QStringLiteral("Newer")}, {QStringLiteral("details_json"), QStringLiteral("{}")}}};
    persistence::DatabaseRows stale{{{QStringLiteral("sequence"), 1}, {QStringLiteral("timestamp_utc"), timestamp}, {QStringLiteral("source_plugin_id"), QStringLiteral("terminal")}, {QStringLiteral("level"), QStringLiteral("info")}, {QStringLiteral("category"), QStringLiteral("terminal.session")}, {QStringLiteral("message"), QStringLiteral("Stale")}, {QStringLiteral("details_json"), QStringLiteral("{}")}}};
    secondPage->addResult(Result<persistence::DatabaseRows>::success(newer));
    secondPage->finish();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return view->findChild<QTableWidget*>(QStringLiteral("logsTable"))->rowCount() == 1; }));
    // clang-format on
    firstPage->addResult(Result<persistence::DatabaseRows>::success(stale));
    firstPage->finish();
    QApplication::processEvents();
    auto* table = view->findChild<QTableWidget*>(QStringLiteral("logsTable"));
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 1);
    EXPECT_EQ(table->item(0, 4)->text(), QStringLiteral("Newer"));
}

TEST(LogsPluginTest, CancelsPendingPluginCallbacksDuringShutdown) {
    test::TestPluginHost host;
    LogsPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    auto operation = std::make_shared<QPromise<Result<void>>>();
    operation->start();
    // clang-format off
    host.executeFutureHandler = [operation](const QString&, const QVariantList&) { return operation->future(); };
    // clang-format on
    QSignalSpy entriesChanged(&plugin, &LogsPlugin::entriesChanged);

    auto future = plugin.clearEntries();
    plugin.shutdown();
    operation->addResult(Result<void>::success());
    operation->finish();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return future.isFinished(); }));
    // clang-format on

    EXPECT_TRUE(future.isCanceled());
    EXPECT_EQ(entriesChanged.count(), 0);
}

class LogsTestsHelper final {
  public:
    static QJsonObject validLogPayload();
};

TEST(LogsPluginTest, InitializesMetadataAndStoresStrictEvents) {
    test::TestPluginHost host;
    LogsPlugin plugin;
    EXPECT_EQ(plugin.id(), QStringLiteral("logs"));
    EXPECT_TRUE(plugin.dependencies().isEmpty());
    EXPECT_EQ(plugin.navigationItems(host.theme()).size(), 1);
    ASSERT_EQ(plugin.settingsGroups().size(), 1);
    EXPECT_EQ(plugin.settingsGroups().first().sections.first().id, QStringLiteral("general"));
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    EXPECT_EQ(plugin.initialize(host).error().code, QStringLiteral("logs_already_initialized"));
    ASSERT_EQ(host.appliedMigrations.size(), 1);

    plugin.handleEvent(QStringLiteral("terminal"), QStringLiteral("workpane.log.entry"), LogsTestsHelper::validLogPayload());
    ASSERT_EQ(host.databaseExecutions.size(), 1);
    EXPECT_TRUE(host.databaseExecutions.first().value(QStringLiteral("statement")).toString().startsWith(QStringLiteral("INSERT INTO logs_entries")));

    plugin.handleEvent(QStringLiteral("terminal"), QStringLiteral("other"), LogsTestsHelper::validLogPayload());
    EXPECT_EQ(host.databaseExecutions.size(), 1);
    auto invalid = LogsTestsHelper::validLogPayload();
    invalid.insert(QStringLiteral("level"), QStringLiteral("fatal"));
    plugin.handleEvent(QStringLiteral("terminal"), QStringLiteral("workpane.log.entry"), invalid);
    EXPECT_EQ(host.databaseExecutions.size(), 1);
    invalid = LogsTestsHelper::validLogPayload();
    invalid.insert(QStringLiteral("timestampUtc"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    plugin.handleEvent(QStringLiteral("terminal"), QStringLiteral("workpane.log.entry"), invalid);
    EXPECT_EQ(host.databaseExecutions.size(), 1);

    test::TestPluginHost migrationHost;
    migrationHost.migrationError = Error{QStringLiteral("migration_failed"), QStringLiteral("Migration failed"), {}};
    plugins::logs::LogsPlugin migrationFailure;
    EXPECT_EQ(migrationFailure.initialize(migrationHost).error().code, QStringLiteral("migration_failed"));
    migrationHost.migrationError.reset();
    EXPECT_TRUE(migrationFailure.initialize(migrationHost).hasValue());
}

// Paging is answered by real SQL, so a double that returns the rows it was handed proves nothing about the order or the page.
TEST(LogsPluginTest, PagesItsEntriesNewestFirstOutOfARealDatabase) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    persistence::StateStore store(directory.filePath(QStringLiteral("workpane.sqlite3")));
    ASSERT_TRUE(store.initialize().hasValue());

    test::TestPluginHost host;
    host.useDatabase(store, QStringLiteral("logs"));
    LogsPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    const QDateTime base = QDateTime::currentDateTimeUtc();
    constexpr int written = 7;

    for (int index = 0; index < written; ++index) {
        QJsonObject payload = LogsTestsHelper::validLogPayload();
        payload.insert(QStringLiteral("timestampUtc"), base.addSecs(index).toString(Qt::ISODateWithMs));
        payload.insert(QStringLiteral("message"), QStringLiteral("entry %1").arg(QString::number(index)));
        plugin.handleEvent(QStringLiteral("terminal"), QStringLiteral("workpane.log.entry"), payload);
    }

    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&plugin]() { const auto page = test::TestFutures::awaitFuture(plugin.entries(0, 100)); return page.hasValue() && page.value().size() == written; }));
    // clang-format on

    // The newest entries come first, which is what the viewer opens on.
    const auto newest = test::TestFutures::awaitFuture(plugin.entries(0, 3));
    ASSERT_TRUE(newest.hasValue());
    ASSERT_EQ(newest.value().size(), 3);
    EXPECT_EQ(newest.value().at(0).message, QStringLiteral("entry 6"));
    EXPECT_EQ(newest.value().at(1).message, QStringLiteral("entry 5"));
    EXPECT_EQ(newest.value().at(2).message, QStringLiteral("entry 4"));
    EXPECT_EQ(newest.value().at(0).sourcePluginId, QStringLiteral("terminal"));
    EXPECT_EQ(newest.value().at(0).level, QStringLiteral("info"));
    EXPECT_TRUE(newest.value().at(0).timestampUtc.isValid());

    // The page before it starts where the reader stopped rather than at the newest entry again.
    const auto older = test::TestFutures::awaitFuture(plugin.entries(newest.value().last().sequence, 3));
    ASSERT_TRUE(older.hasValue());
    ASSERT_EQ(older.value().size(), 3);
    EXPECT_EQ(older.value().at(0).message, QStringLiteral("entry 3"));
    EXPECT_EQ(older.value().at(2).message, QStringLiteral("entry 1"));

    // Clearing removes what was stored and leaves the reader an empty page.
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.clearEntries()).hasValue());
    const auto cleared = test::TestFutures::awaitFuture(plugin.entries(0, 100));
    ASSERT_TRUE(cleared.hasValue());
    EXPECT_TRUE(cleared.value().isEmpty());
    plugin.shutdown();
}

QJsonObject LogsTestsHelper::validLogPayload() {
    return {{QStringLiteral("timestampUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}, {QStringLiteral("level"), QStringLiteral("info")}, {QStringLiteral("category"), QStringLiteral("test.category")}, {QStringLiteral("message"), QStringLiteral("Test message")}, {QStringLiteral("details"), QJsonObject{{QStringLiteral("value"), 1}}}};
}
} // namespace workpane::plugins::logs

TEST(LogsTranslationsTest, SpellsEveryKeyInEveryLanguageTheSelectorOffers) {
    workpane::plugins::logs::LogsPlugin plugin;
    workpane::test::TestCatalogs::expectCompleteCatalog(QStringLiteral("logs"), plugin.translations());
}
