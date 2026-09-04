#include "TestFuture.h"
#include "TestTranslations.h"
#include "app/Application.h"
#include "app/ApplicationSettingsStore.h"
#include "app/ConfigurationManager.h"
#include "domain/ApplicationLanguage.h"
#include "domain/Result.h"
#include "persistence/ConfigurationTransfer.h"
#include "persistence/CoreDatabaseSchema.h"
#include "persistence/DatabaseExecutor.h"
#include "persistence/StateStore.h"
#include "persistence/StoredValues.h"
#include "plugins/CapabilityRegistry.h"
#include "plugins/CoreTranslations.h"
#include "plugins/LocalizationService.h"
#include "plugins/PluginManager.h"
#include "ui/AppStyle.h"
#include "ui/ApplicationSettingsView.h"
#include "ui/ApplicationShortcuts.h"
#include "ui/Components.h"
#include "ui/ConfirmationDialog.h"
#include "ui/Icons.h"
#include "ui/MainWindow.h"
#include "ui/ModeBar.h"
#include "ui/SettingsView.h"
#include "ui/TabBar.h"
#include "ui/Theme.h"
#include "ui/ToastOverlay.h"

#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPluginLoader>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSignalSpy>
#include <QSpinBox>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextFragment>
#include <QTimeZone>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <type_traits>

namespace workpane {

TEST(ResultTest, PreservesSuccessfulValuesAndFailures) {
    auto success = Result<QString>::success(QStringLiteral("value"));
    ASSERT_TRUE(success.hasValue());
    EXPECT_EQ(success.value(), QStringLiteral("value"));
    success.value() = QStringLiteral("updated");
    EXPECT_EQ(success.value(), QStringLiteral("updated"));

    auto failure = Result<QString>::failure({QStringLiteral("code"), QStringLiteral("message"), QStringLiteral("detail")});
    ASSERT_FALSE(failure.hasValue());
    EXPECT_EQ(failure.error().code, QStringLiteral("code"));
    EXPECT_EQ(failure.error().message, QStringLiteral("message"));
    EXPECT_EQ(failure.error().detail, QStringLiteral("detail"));
    EXPECT_THROW((void)failure.value(), std::bad_optional_access);

    EXPECT_TRUE(Result<void>::success().hasValue());
    const auto emptyFailure = Result<void>::failure(failure.error());
    EXPECT_FALSE(emptyFailure.hasValue());
    EXPECT_EQ(emptyFailure.error().code, QStringLiteral("code"));
}

TEST(ApplicationLanguageTest, ResolvesSupportedRegionalBaseAndEnglishLanguages) {
    EXPECT_EQ(domain::ApplicationLanguages::supportedApplicationLanguages(), QStringList({QStringLiteral("en"), QStringLiteral("pt")}));
    EXPECT_EQ(domain::ApplicationLanguages::resolveApplicationLanguage(QStringLiteral("pt_BR")), QStringLiteral("pt"));
    EXPECT_EQ(domain::ApplicationLanguages::resolveApplicationLanguage(QStringLiteral("pt-PT")), QStringLiteral("pt"));
    EXPECT_EQ(domain::ApplicationLanguages::resolveApplicationLanguage(QStringLiteral("en-US")), QStringLiteral("en"));
    EXPECT_EQ(domain::ApplicationLanguages::resolveApplicationLanguage(QStringLiteral("de-DE")), QStringLiteral("en"));
    EXPECT_TRUE(domain::ApplicationLanguages::isSupportedApplicationLanguage(QStringLiteral("pt")));
    EXPECT_FALSE(domain::ApplicationLanguages::isSupportedApplicationLanguage(QStringLiteral("pt-br")));
}

// A language offered without a name would reach the selector as a raw key, so the two travel in one declaration.
TEST(ApplicationLanguageTest, CarriesACoreTranslationForEveryLanguageTheSelectorOffers) {
    const auto& catalog = domain::ApplicationLanguages::applicationLanguageCatalog();
    const auto translations = workpane::plugins::coretranslations::CoreCatalog::catalog();

    ASSERT_EQ(catalog.size(), domain::ApplicationLanguages::supportedApplicationLanguages().size());

    for (const auto& descriptor : catalog) {
        EXPECT_TRUE(domain::ApplicationLanguages::isSupportedApplicationLanguage(descriptor.code)) << descriptor.code.toStdString();
        EXPECT_FALSE(descriptor.titleKey.isEmpty()) << descriptor.code.toStdString();

        for (const auto& language : domain::ApplicationLanguages::supportedApplicationLanguages()) {
            EXPECT_TRUE(translations.value(language).contains(descriptor.titleKey)) << descriptor.titleKey.toStdString() << " is missing from " << language.toStdString();
        }
    }
}

TEST(StateStoreTest, CreatesAndPersistsCoreStateAndPluginOwnedSchema) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("workpane.sqlite3"));

    {
        persistence::StateStore store(path);
        ASSERT_TRUE(store.initialize().hasValue());
        ASSERT_TRUE(store.wasCleanShutdown().hasValue());
        EXPECT_TRUE(store.wasCleanShutdown().value());
        EXPECT_TRUE(store.settings(QStringLiteral("workpane")).isEmpty());
        ASSERT_TRUE(store.markShutdown(false).hasValue());
        ASSERT_TRUE(store.saveSettings(QStringLiteral("workpane"), {{QStringLiteral("language"), QStringLiteral("en")}}).hasValue());
        EXPECT_EQ(store.settings(QStringLiteral("workpane")).value(QStringLiteral("language")).toString(), QStringLiteral("en"));
        EXPECT_EQ(store.saveSettings(QString{}, {}).error().code, QStringLiteral("database_settings_invalid"));
        ASSERT_TRUE(store.migratePluginDatabase(QStringLiteral("sample"), {{1, {QStringLiteral("CREATE TABLE sample_state(id INTEGER PRIMARY KEY, value TEXT NOT NULL) STRICT")}}}).hasValue());
        ASSERT_TRUE(store.executePluginDatabase(QStringLiteral("sample"), QStringLiteral("INSERT INTO sample_state(id, value) VALUES(1, ?)"), {QStringLiteral("persisted")}).hasValue());
        ASSERT_TRUE(store.markShutdown(true).hasValue());
    }

    persistence::StateStore restored(path);
    ASSERT_TRUE(restored.initialize().hasValue());
    ASSERT_TRUE(restored.wasCleanShutdown().hasValue());
    EXPECT_TRUE(restored.wasCleanShutdown().value());
    EXPECT_EQ(restored.settings(QStringLiteral("workpane")).value(QStringLiteral("language")).toString(), QStringLiteral("en"));
    EXPECT_TRUE(restored.settings(QStringLiteral("absent")).isEmpty());
    EXPECT_EQ(restored.pluginSchemaVersion(QStringLiteral("sample")).value(), 1);
    const auto rows = restored.queryPluginDatabase(QStringLiteral("sample"), QStringLiteral("SELECT value FROM sample_state"), {});
    ASSERT_TRUE(rows.hasValue());
    ASSERT_EQ(rows.value().size(), 1);
    EXPECT_EQ(rows.value().first().value(QStringLiteral("value")).toString(), QStringLiteral("persisted"));
}

// What a plugin recorded survives the version that evolves its schema, because that history is the memory of the feature and only the reader removes it.
TEST(StateStoreTest, KeepsWhatAPluginStoredWhenALaterVersionEvolvesItsSchema) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    persistence::StateStore store(directory.filePath(QStringLiteral("workpane.sqlite3")));
    ASSERT_TRUE(store.initialize().hasValue());

    const QString creating = QStringLiteral("CREATE TABLE sample_runs(id TEXT PRIMARY KEY, tokens INTEGER NOT NULL)");
    const QVector<persistence::DatabaseMigration> first{{1, {creating}}};
    ASSERT_TRUE(store.migratePluginDatabase(QStringLiteral("sample"), first).hasValue());
    ASSERT_TRUE(store.executePluginDatabase(QStringLiteral("sample"), QStringLiteral("INSERT INTO sample_runs(id, tokens) VALUES(?, ?)"), {QStringLiteral("run-1"), 42}).hasValue());

    const QVector<persistence::DatabaseMigration> evolved{{1, {creating}}, {2, {QStringLiteral("ALTER TABLE sample_runs ADD COLUMN model_id TEXT NOT NULL DEFAULT ''")}}};
    ASSERT_TRUE(store.migratePluginDatabase(QStringLiteral("sample"), evolved).hasValue());
    EXPECT_EQ(store.pluginSchemaVersion(QStringLiteral("sample")).value(), 2);

    // The row written before the change is still there, and the column the change added is empty for it.
    const auto rows = store.queryPluginDatabase(QStringLiteral("sample"), QStringLiteral("SELECT id, tokens, model_id FROM sample_runs"), {});
    ASSERT_TRUE(rows.hasValue()) << rows.error().message.toStdString();
    ASSERT_EQ(rows.value().size(), 1);
    EXPECT_EQ(rows.value().first().value(QStringLiteral("id")).toString(), QStringLiteral("run-1"));
    EXPECT_EQ(rows.value().first().value(QStringLiteral("tokens")).toInt(), 42);
    EXPECT_TRUE(rows.value().first().value(QStringLiteral("model_id")).toString().isEmpty());

    // Opening again with the same migrations changes nothing, because the schema is compared against what those migrations produce.
    ASSERT_TRUE(store.migratePluginDatabase(QStringLiteral("sample"), evolved).hasValue());
    const auto again = store.queryPluginDatabase(QStringLiteral("sample"), QStringLiteral("SELECT id FROM sample_runs"), {});
    ASSERT_TRUE(again.hasValue());
    EXPECT_EQ(again.value().size(), 1);
}

// A schema written by a later version of the product is refused rather than rebuilt, because rebuilding it would answer running an older build by destroying what the reader recorded.
TEST(StateStoreTest, RefusesASchemaNewerThanThePluginInsteadOfRebuildingIt) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    persistence::StateStore store(directory.filePath(QStringLiteral("workpane.sqlite3")));
    ASSERT_TRUE(store.initialize().hasValue());

    const QString creating = QStringLiteral("CREATE TABLE sample_runs(id TEXT PRIMARY KEY, tokens INTEGER NOT NULL)");
    const QString evolving = QStringLiteral("ALTER TABLE sample_runs ADD COLUMN model_id TEXT NOT NULL DEFAULT ''");
    const QVector<persistence::DatabaseMigration> newer{{1, {creating}}, {2, {evolving}}};
    ASSERT_TRUE(store.migratePluginDatabase(QStringLiteral("sample"), newer).hasValue());
    ASSERT_TRUE(store.executePluginDatabase(QStringLiteral("sample"), QStringLiteral("INSERT INTO sample_runs(id, tokens, model_id) VALUES(?, ?, ?)"), {QStringLiteral("run-1"), 42, QStringLiteral("gpt-4o")}).hasValue());

    // An older build of the product declares only the creating migration and meets a database two versions along.
    const QVector<persistence::DatabaseMigration> older{{1, {creating}}};
    const auto refused = store.migratePluginDatabase(QStringLiteral("sample"), older);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.error().code, QStringLiteral("plugin_database_version_newer"));

    // What the reader recorded is still there, and the stored version was not moved back.
    const auto rows = store.queryPluginDatabase(QStringLiteral("sample"), QStringLiteral("SELECT id, model_id FROM sample_runs"), {});
    ASSERT_TRUE(rows.hasValue()) << rows.error().message.toStdString();
    ASSERT_EQ(rows.value().size(), 1);
    EXPECT_EQ(rows.value().first().value(QStringLiteral("model_id")).toString(), QStringLiteral("gpt-4o"));
    EXPECT_EQ(store.pluginSchemaVersion(QStringLiteral("sample")).value(), 2);

    // A stored schema that really cannot be used is still rebuilt, because that is what the rebuild is for.
    const QVector<persistence::DatabaseMigration> different{{1, {QStringLiteral("CREATE TABLE sample_runs(id TEXT PRIMARY KEY, note TEXT NOT NULL)")}}, {2, {evolving}}};
    ASSERT_TRUE(store.migratePluginDatabase(QStringLiteral("sample"), different).hasValue());
    const auto rebuilt = store.queryPluginDatabase(QStringLiteral("sample"), QStringLiteral("SELECT id FROM sample_runs"), {});
    ASSERT_TRUE(rebuilt.hasValue());
    EXPECT_TRUE(rebuilt.value().isEmpty());
}

TEST(StateStoreTest, ReportsPathMigrationAndTransactionErrors) {
    persistence::StateStore emptyPath(QString{});
    EXPECT_EQ(emptyPath.initialize().error().code, QStringLiteral("database_path_invalid"));

    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    persistence::StateStore missingParent(directory.filePath(QStringLiteral("missing/workpane.sqlite3")));
    EXPECT_EQ(missingParent.initialize().error().code, QStringLiteral("database_path_invalid"));

    const QString directoryPath = directory.filePath(QStringLiteral("state-directory"));
    ASSERT_TRUE(QDir().mkpath(directoryPath));
    persistence::StateStore unreadable(directoryPath);
    EXPECT_EQ(unreadable.initialize().error().code, QStringLiteral("database_path_invalid"));

    persistence::StateStore valid(directory.filePath(QStringLiteral("valid.sqlite3")));
    const auto validInitialization = valid.initialize();
    ASSERT_TRUE(validInitialization.hasValue()) << validInitialization.error().message.toStdString() << " " << validInitialization.error().detail.toStdString();
    EXPECT_EQ(valid.initialize().error().code, QStringLiteral("database_already_initialized"));
    EXPECT_EQ(valid.pluginSchemaVersion(QStringLiteral("Invalid")).error().code, QStringLiteral("plugin_database_owner_invalid"));
    EXPECT_EQ(valid.queryPluginDatabase(QStringLiteral("sample"), QStringLiteral("SELECT * FROM core_state"), {}).error().code, QStringLiteral("plugin_database_ownership_invalid"));
    EXPECT_EQ(valid.executePluginDatabase(QStringLiteral("sample"), QStringLiteral("PRAGMA user_version"), {}).error().code, QStringLiteral("plugin_database_statement_unsupported"));
    EXPECT_EQ(valid.queryPluginDatabase(QStringLiteral("sample"), QStringLiteral("SELECT sample_values.value FROM sample_values, core_state"), {}).error().code, QStringLiteral("plugin_database_statement_unsupported"));
    EXPECT_EQ(valid.queryPluginDatabase(QStringLiteral("sample"), QStringLiteral("SELECT * FROM sample_values JOIN sample_values AS duplicate ON duplicate.id = sample_values.id, core_state"), {}).error().code, QStringLiteral("plugin_database_statement_unsupported"));
    EXPECT_EQ(valid.queryPluginDatabase(QStringLiteral("sample"), QStringLiteral("SELECT * FROM sample_values INDEXED BY sample_values_index, core_state"), {}).error().code, QStringLiteral("plugin_database_statement_unsupported"));
    EXPECT_EQ(valid.queryPluginDatabase(QStringLiteral("sample"), QStringLiteral("SELECT sample_values.value FROM sample_values JOIN \"core_state\" ON 1 = 1"), {}).error().code, QStringLiteral("plugin_database_statement_unsupported"));
    EXPECT_EQ(valid.queryPluginDatabase(QStringLiteral("sample"), QStringLiteral("SELECT * FROM (sample_values JOIN core_state)"), {}).error().code, QStringLiteral("plugin_database_statement_unsupported"));
    EXPECT_EQ(valid.queryPluginDatabase(QStringLiteral("sample"), QStringLiteral("SELECT * FROM sample_values INDEXED BY core_preferences_singleton"), {}).error().code, QStringLiteral("plugin_database_ownership_invalid"));
    EXPECT_EQ(valid.executePluginDatabase(QStringLiteral("sample"), QStringLiteral("CREATE VIEW sample_view AS SELECT * FROM sample_values"), {}).error().code, QStringLiteral("plugin_database_statement_unsupported"));
    EXPECT_EQ(valid.executePluginDatabase(QStringLiteral("sample"), QStringLiteral("DELETE FROM sample_values; DELETE FROM core_state"), {}).error().code, QStringLiteral("plugin_database_statement_unsupported"));
    EXPECT_EQ(valid.migratePluginDatabase(QStringLiteral("sample"), {}).error().code, QStringLiteral("plugin_database_migration_invalid"));
    EXPECT_EQ(valid.migratePluginDatabase(QStringLiteral("sample"), {{2, {QStringLiteral("CREATE TABLE sample_invalid(id INTEGER)")}}}).error().code, QStringLiteral("plugin_database_migration_invalid"));
    EXPECT_EQ(valid.migratePluginDatabase(QStringLiteral("sample"), {{1, {QStringLiteral("CREATE TABLE sample_first(id INTEGER)")}}, {3, {QStringLiteral("CREATE TABLE sample_third(id INTEGER)")}}}).error().code, QStringLiteral("plugin_database_migration_invalid"));
    EXPECT_EQ(valid.migratePluginDatabase(QStringLiteral("foreign"), {{1, {QStringLiteral("CREATE TABLE sample_foreign(id INTEGER PRIMARY KEY) STRICT")}}}).error().code, QStringLiteral("plugin_database_ownership_invalid"));
    ASSERT_TRUE(valid.migratePluginDatabase(QStringLiteral("sample"), {{1, {QStringLiteral("CREATE TABLE sample_values(id INTEGER PRIMARY KEY, value TEXT NOT NULL) STRICT")}}}).hasValue());
    EXPECT_EQ(valid.pluginSchemaVersion(QStringLiteral("sample")).value(), 1);
    EXPECT_EQ(valid.executePluginDatabaseTransaction(QStringLiteral("sample"), {}).error().code, QStringLiteral("plugin_database_transaction_invalid"));
    const QVector<persistence::DatabaseStatement> failingTransaction{{QStringLiteral("INSERT INTO sample_values(id, value) VALUES(?, ?)"), {1, QStringLiteral("kept-out")}}, {QStringLiteral("INSERT INTO sample_values(id, value) VALUES(?, ?)"), {1, QStringLiteral("duplicate")}}};
    EXPECT_FALSE(valid.executePluginDatabaseTransaction(QStringLiteral("sample"), failingTransaction).hasValue());
    EXPECT_TRUE(valid.queryPluginDatabase(QStringLiteral("sample"), QStringLiteral("SELECT id FROM sample_values"), {}).value().isEmpty());

    // A schema that changed while its version stayed the same is rebuilt from the migrations the plugin declares, because nothing stored may keep a feature from opening.
    ASSERT_TRUE(valid.migratePluginDatabase(QStringLiteral("sample"), {{1, {QStringLiteral("CREATE TABLE sample_values(id INTEGER PRIMARY KEY, value TEXT NOT NULL, extra TEXT NOT NULL) STRICT")}}}).hasValue());
    EXPECT_EQ(valid.rebuiltSchemas(), QStringList{QStringLiteral("sample")});
    ASSERT_TRUE(valid.migratePluginDatabase(QStringLiteral("sample"), {{1, {QStringLiteral("CREATE TABLE sample_values(id INTEGER PRIMARY KEY, value TEXT NOT NULL) STRICT"), QStringLiteral("CREATE INDEX sample_values_index ON sample_values(value)")}}}).hasValue());
    EXPECT_TRUE(valid.migratePluginDatabase(QStringLiteral("sample"), {{1, {QStringLiteral("CREATE TABLE sample_values(id INTEGER PRIMARY KEY, value TEXT NOT NULL) STRICT")}}}).hasValue());
    EXPECT_EQ(valid.pluginSchemaVersion(QStringLiteral("sample")).value(), 1);
}

// A write queued as the product closes is what the next start reads, so the executor runs what is waiting before it closes rather than discarding it.
TEST(DatabaseExecutorTest, RunsEveryWriteStillWaitingWhenItIsDestroyed) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("workpane.sqlite3"));
    {
        persistence::StateStore store(path);
        ASSERT_TRUE(store.initialize().hasValue());
        ASSERT_TRUE(store.migratePluginDatabase(QStringLiteral("sample"), {{1, {QStringLiteral("CREATE TABLE sample_values(id INTEGER PRIMARY KEY, value TEXT NOT NULL)")}}}).hasValue());
    }

    constexpr int queued = 40;
    {
        persistence::DatabaseExecutor executor(path);

        for (int index = 0; index < queued; ++index) {
            // The future is dropped exactly as a plugin closing its state drops it, so nothing here waits for the write.
            [[maybe_unused]] auto pending = executor.executePluginDatabase(QStringLiteral("sample"), QStringLiteral("INSERT INTO sample_values(id, value) VALUES(?, ?)"), {index, QStringLiteral("kept")});
        }
    }

    // Every one of them is on disk, because the executor closes behind what it was already given.
    persistence::StateStore reopened(path);
    ASSERT_TRUE(reopened.initialize().hasValue());
    const auto rows = reopened.queryPluginDatabase(QStringLiteral("sample"), QStringLiteral("SELECT id FROM sample_values"), {});
    ASSERT_TRUE(rows.hasValue()) << rows.error().message.toStdString();
    EXPECT_EQ(rows.value().size(), queued);
}

// An order the platform decides shows up in repetition, so the executor is driven with reads, writes and transactions whose contexts die mid-flight.
TEST(DatabaseExecutorTest, SurvivesManyOverlappingWritesReadsAndCancelledContinuations) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("workpane.sqlite3"));
    {
        persistence::StateStore store(path);
        ASSERT_TRUE(store.initialize().hasValue());
        ASSERT_TRUE(store.migratePluginDatabase(QStringLiteral("sample"), {{1, {QStringLiteral("CREATE TABLE sample_values(id INTEGER PRIMARY KEY, note TEXT NOT NULL) STRICT")}}}).hasValue());
    }

    constexpr int rounds = 60;
    int answered = 0;
    {
        persistence::DatabaseExecutor executor(path);

        for (int round = 0; round < rounds; ++round) {
            // The context of a continuation is destroyed while its work may still be running, which is what a plugin closing does.
            auto context = std::make_unique<QObject>();
            auto write = executor.executePluginDatabase(QStringLiteral("sample"), QStringLiteral("INSERT INTO sample_values(id, note) VALUES(?, ?)"), {round, QStringLiteral("note %1").arg(QString::number(round))});
            auto transaction = executor.executePluginDatabaseTransaction(QStringLiteral("sample"), {{QStringLiteral("UPDATE sample_values SET note = ? WHERE id = ?"), {QStringLiteral("changed"), round}}});
            auto read = executor.queryPluginDatabase(QStringLiteral("sample"), QStringLiteral("SELECT id FROM sample_values"), {});
            // clang-format off
            write.then(context.get(), [&answered](Result<void>) { ++answered; });
            transaction.then(context.get(), [&answered](Result<void>) { ++answered; });
            read.then(context.get(), [&answered](Result<persistence::DatabaseRows>) { ++answered; });
            // clang-format on

            if (round % 3 == 0) {
                context.reset();
            }

            QCoreApplication::processEvents();
        }
    }

    // Every row reached the disk whatever order the work ran in, and the answers nobody was left to hear were dropped rather than delivered.
    persistence::StateStore reopened(path);
    ASSERT_TRUE(reopened.initialize().hasValue());
    const auto rows = reopened.queryPluginDatabase(QStringLiteral("sample"), QStringLiteral("SELECT id, note FROM sample_values"), {});
    ASSERT_TRUE(rows.hasValue()) << rows.error().message.toStdString();
    EXPECT_EQ(rows.value().size(), rounds);

    for (const auto& row : rows.value()) {
        EXPECT_EQ(row.value(QStringLiteral("note")).toString(), QStringLiteral("changed"));
    }
}

TEST(DatabaseExecutorTest, SerializesRuntimeQueriesAndReportsStorageErrorsAsynchronously) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("workpane.sqlite3"));
    persistence::StateStore store(path);
    ASSERT_TRUE(store.initialize().hasValue());
    ASSERT_TRUE(store.migratePluginDatabase(QStringLiteral("sample"), {{1, {QStringLiteral("CREATE TABLE sample_values(id INTEGER PRIMARY KEY, value TEXT NOT NULL) STRICT")}}}).hasValue());

    persistence::DatabaseExecutor executor(path);
    auto firstInsert = executor.executePluginDatabase(QStringLiteral("sample"), QStringLiteral("INSERT INTO sample_values(id, value) VALUES(?, ?)"), {1, QStringLiteral("first")});
    auto secondInsert = executor.executePluginDatabase(QStringLiteral("sample"), QStringLiteral("INSERT INTO sample_values(id, value) VALUES(?, ?)"), {2, QStringLiteral("second")});
    ASSERT_TRUE(test::TestFutures::awaitFuture(firstInsert).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(secondInsert).hasValue());

    const auto rows = test::TestFutures::awaitFuture(executor.queryPluginDatabase(QStringLiteral("sample"), QStringLiteral("SELECT id, value FROM sample_values ORDER BY id"), {}));
    ASSERT_TRUE(rows.hasValue());
    ASSERT_EQ(rows.value().size(), 2);
    EXPECT_EQ(rows.value().first().value(QStringLiteral("value")).toString(), QStringLiteral("first"));
    EXPECT_EQ(rows.value().last().value(QStringLiteral("value")).toString(), QStringLiteral("second"));

    const auto duplicate = test::TestFutures::awaitFuture(executor.executePluginDatabase(QStringLiteral("sample"), QStringLiteral("INSERT INTO sample_values(id, value) VALUES(?, ?)"), {1, QStringLiteral("duplicate")}));
    EXPECT_EQ(duplicate.error().code, QStringLiteral("database_execute_failed"));
    const auto invalidOwner = test::TestFutures::awaitFuture(executor.queryPluginDatabase(QStringLiteral("Invalid"), QStringLiteral("SELECT id FROM sample_values"), {}));
    EXPECT_EQ(invalidOwner.error().code, QStringLiteral("plugin_database_query_invalid"));
}

TEST(ConfigurationTransferTest, ExportsStagesAndAppliesACompleteDatabaseSnapshot) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("source.sqlite3"));
    const QString exportPath = directory.filePath(QStringLiteral("export.sqlite3"));
    const QString pendingPath = directory.filePath(QStringLiteral("pending.sqlite3"));
    const QString importedPath = directory.filePath(QStringLiteral("imported.sqlite3"));

    {
        persistence::StateStore source(sourcePath);
        ASSERT_TRUE(source.initialize().hasValue());
        ASSERT_TRUE(source.migratePluginDatabase(QStringLiteral("sample"), {{1, {QStringLiteral("CREATE TABLE sample_values(id INTEGER PRIMARY KEY, value TEXT NOT NULL) STRICT")}}}).hasValue());
        ASSERT_TRUE(source.executePluginDatabase(QStringLiteral("sample"), QStringLiteral("INSERT INTO sample_values(id, value) VALUES(1, 'saved')"), {}).hasValue());
        ASSERT_TRUE(source.saveSettings(QStringLiteral("workpane"), {{QStringLiteral("language"), QStringLiteral("pt")}}).hasValue());
        ASSERT_TRUE(source.markShutdown(false).hasValue());
        const auto exported = test::TestFutures::awaitFuture(persistence::ConfigurationTransfer::exportDatabase(sourcePath, exportPath));
        ASSERT_TRUE(exported.hasValue()) << exported.error().message.toStdString();
        const auto conflict = test::TestFutures::awaitFuture(persistence::ConfigurationTransfer::exportDatabase(sourcePath, sourcePath));
        EXPECT_EQ(conflict.error().code, QStringLiteral("configuration_destination_conflict"));
    }

    ASSERT_TRUE(QFileInfo::exists(exportPath));
    const QHash<QString, int> schemaVersions{{QStringLiteral("sample"), 1}};
    const auto incompatible = test::TestFutures::awaitFuture(persistence::ConfigurationTransfer::stageImport(exportPath, directory.filePath(QStringLiteral("incompatible.sqlite3")), {{QStringLiteral("sample"), 2}}));
    EXPECT_EQ(incompatible.error().code, QStringLiteral("configuration_database_invalid"));
    const auto staged = test::TestFutures::awaitFuture(persistence::ConfigurationTransfer::stageImport(exportPath, pendingPath, schemaVersions));
    ASSERT_TRUE(staged.hasValue()) << staged.error().message.toStdString();
    {
        persistence::StateStore current(importedPath);
        ASSERT_TRUE(current.initialize().hasValue());
        ASSERT_TRUE(current.migratePluginDatabase(QStringLiteral("sample"), {{1, {QStringLiteral("CREATE TABLE sample_values(id INTEGER PRIMARY KEY, value TEXT NOT NULL) STRICT")}}}).hasValue());
    }
    const QString backupPath = directory.filePath(QStringLiteral("backup.sqlite3"));
    const auto applied = persistence::ConfigurationTransfer::beginPendingImport(importedPath, pendingPath, backupPath, schemaVersions);
    ASSERT_TRUE(applied.hasValue()) << applied.error().code.toStdString() << " " << applied.error().detail.toStdString();
    EXPECT_TRUE(applied.value());
    EXPECT_TRUE(QFileInfo::exists(pendingPath));
    ASSERT_TRUE(persistence::ConfigurationTransfer::finalizePendingImport(pendingPath, backupPath).hasValue());
    EXPECT_FALSE(QFileInfo::exists(pendingPath));

    persistence::StateStore imported(importedPath);
    ASSERT_TRUE(imported.initialize().hasValue());
    EXPECT_TRUE(imported.wasCleanShutdown().value());
    EXPECT_EQ(imported.settings(QStringLiteral("workpane")).value(QStringLiteral("language")).toString(), QStringLiteral("pt"));
    const auto rows = imported.queryPluginDatabase(QStringLiteral("sample"), QStringLiteral("SELECT value FROM sample_values"), {});
    ASSERT_TRUE(rows.hasValue());
    ASSERT_EQ(rows.value().size(), 1);
    EXPECT_EQ(rows.value().first().value(QStringLiteral("value")).toString(), QStringLiteral("saved"));
}

// A log belongs to the database that wrote it, so one a crash left behind must never be replayed over what an import brought.
TEST(ConfigurationTransferTest, DiscardsTheLogOfTheDatabaseAnImportReplaces) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("source.sqlite3"));
    const QString exportPath = directory.filePath(QStringLiteral("export.sqlite3"));
    const QString pendingPath = directory.filePath(QStringLiteral("pending.sqlite3"));
    const QString currentPath = directory.filePath(QStringLiteral("current.sqlite3"));
    const QString backupPath = directory.filePath(QStringLiteral("backup.sqlite3"));
    const QString capturedLog = directory.filePath(QStringLiteral("captured.log"));
    const QString currentLog = currentPath + QStringLiteral("-wal");
    const QHash<QString, int> schemaVersions;

    {
        persistence::StateStore source(sourcePath);
        ASSERT_TRUE(source.initialize().hasValue());
        ASSERT_TRUE(source.saveSettings(QStringLiteral("workpane"), {{QStringLiteral("language"), QStringLiteral("pt")}}).hasValue());
        const auto exported = test::TestFutures::awaitFuture(persistence::ConfigurationTransfer::exportDatabase(sourcePath, exportPath));
        ASSERT_TRUE(exported.hasValue()) << exported.error().message.toStdString();
    }

    // The current database is left as a crash leaves one, with its newest commits still in the log beside it.
    {
        persistence::StateStore current(currentPath);
        ASSERT_TRUE(current.initialize().hasValue());
        ASSERT_TRUE(current.saveSettings(QStringLiteral("workpane"), {{QStringLiteral("language"), QStringLiteral("en")}}).hasValue());
        ASSERT_TRUE(QFileInfo::exists(currentLog));
        ASSERT_TRUE(QFile::copy(currentLog, capturedLog));
    }

    QFile::remove(currentLog);
    ASSERT_TRUE(QFile::copy(capturedLog, currentLog));
    const auto staged = test::TestFutures::awaitFuture(persistence::ConfigurationTransfer::stageImport(exportPath, pendingPath, schemaVersions));
    ASSERT_TRUE(staged.hasValue()) << staged.error().message.toStdString();
    const auto applied = persistence::ConfigurationTransfer::beginPendingImport(currentPath, pendingPath, backupPath, schemaVersions);
    ASSERT_TRUE(applied.hasValue()) << applied.error().code.toStdString() << " " << applied.error().detail.toStdString();
    EXPECT_TRUE(applied.value());

    // Nothing of the replaced database is left beside the one that arrived.
    EXPECT_FALSE(QFileInfo::exists(currentLog));

    persistence::StateStore imported(currentPath);
    ASSERT_TRUE(imported.initialize().hasValue());
    EXPECT_EQ(imported.settings(QStringLiteral("workpane")).value(QStringLiteral("language")).toString(), QStringLiteral("pt"));
}

TEST(ConfigurationTransferTest, RestoresThePreviousDatabaseWhenAnAppliedImportIsRejected) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString currentPath = directory.filePath(QStringLiteral("current.sqlite3"));
    const QString sourcePath = directory.filePath(QStringLiteral("source.sqlite3"));
    const QString exportPath = directory.filePath(QStringLiteral("export.sqlite3"));
    const QString pendingPath = directory.filePath(QStringLiteral("pending.sqlite3"));
    const QString backupPath = directory.filePath(QStringLiteral("backup.sqlite3"));
    {
        persistence::StateStore current(currentPath);
        ASSERT_TRUE(current.initialize().hasValue());
    }
    {
        persistence::StateStore source(sourcePath);
        ASSERT_TRUE(source.initialize().hasValue());
        ASSERT_TRUE(persistence::ConfigurationTransfer::exportDatabaseNow(sourcePath, exportPath).hasValue());
    }
    ASSERT_TRUE(test::TestFutures::awaitFuture(persistence::ConfigurationTransfer::stageImport(exportPath, pendingPath, {})).hasValue());
    const auto applied = persistence::ConfigurationTransfer::beginPendingImport(currentPath, pendingPath, backupPath, {});
    ASSERT_TRUE(applied.hasValue()) << applied.error().code.toStdString() << " " << applied.error().detail.toStdString();
    ASSERT_TRUE(applied.value());
    ASSERT_TRUE(persistence::ConfigurationTransfer::rollbackPendingImport(currentPath, pendingPath, backupPath).hasValue());
    EXPECT_FALSE(QFileInfo::exists(pendingPath));
    EXPECT_FALSE(QFileInfo::exists(backupPath));

    persistence::StateStore restored(currentPath);
    ASSERT_TRUE(restored.initialize().hasValue());
    EXPECT_TRUE(restored.settings(QStringLiteral("workpane")).isEmpty());
}

TEST(ConfigurationTransferTest, RecoversAnInterruptedAppliedImportWithoutReapplyingIt) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString currentPath = directory.filePath(QStringLiteral("current.sqlite3"));
    const QString sourcePath = directory.filePath(QStringLiteral("source.sqlite3"));
    const QString pendingPath = directory.filePath(QStringLiteral("pending.sqlite3"));
    const QString backupPath = directory.filePath(QStringLiteral("backup.sqlite3"));
    {
        persistence::StateStore current(currentPath);
        ASSERT_TRUE(current.initialize().hasValue());
    }
    {
        persistence::StateStore source(sourcePath);
        ASSERT_TRUE(source.initialize().hasValue());
    }
    ASSERT_TRUE(test::TestFutures::awaitFuture(persistence::ConfigurationTransfer::stageImport(sourcePath, pendingPath, {})).hasValue());
    const auto applied = persistence::ConfigurationTransfer::beginPendingImport(currentPath, pendingPath, backupPath, {});
    ASSERT_TRUE(applied.hasValue());
    ASSERT_TRUE(applied.value());

    const auto recovered = persistence::ConfigurationTransfer::beginPendingImport(currentPath, pendingPath, backupPath, {});
    ASSERT_TRUE(recovered.hasValue());
    EXPECT_FALSE(recovered.value());
    EXPECT_FALSE(QFileInfo::exists(pendingPath));
    EXPECT_FALSE(QFileInfo::exists(backupPath));

    persistence::StateStore restored(currentPath);
    ASSERT_TRUE(restored.initialize().hasValue());
    EXPECT_TRUE(restored.settings(QStringLiteral("workpane")).isEmpty());
}

TEST(ConfigurationManagerTest, RejectsConcurrentConfigurationTransfers) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("workpane.sqlite3"));
    persistence::StateStore store(databasePath);
    ASSERT_TRUE(store.initialize().hasValue());
    persistence::DatabaseExecutor executor(databasePath);
    app::ConfigurationManager manager(executor, directory.filePath(QStringLiteral("pending.sqlite3")), {});
    QSignalSpy stateChanged(&manager, &app::ConfigurationManager::transferStateChanged);
    std::optional<Result<void>> concurrentResult;
    // clang-format off
    QObject::connect(&manager, &app::ConfigurationManager::transferStateChanged, &manager, [&manager, &directory, &concurrentResult](bool active) {
        if (active && !concurrentResult.has_value()) {
            concurrentResult = test::TestFutures::awaitFuture(manager.exportConfiguration(directory.filePath(QStringLiteral("export.sqlite3"))));
        }
    });
    // clang-format on

    auto first = manager.importConfiguration(directory.filePath(QStringLiteral("missing.sqlite3")));
    EXPECT_FALSE(test::TestFutures::awaitFuture(first).hasValue());
    ASSERT_TRUE(concurrentResult.has_value());
    EXPECT_EQ(concurrentResult->error().code, QStringLiteral("configuration_transfer_pending"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return stateChanged.count() == 2; }));
    // clang-format on
    EXPECT_TRUE(stateChanged.first().first().toBool());
    EXPECT_FALSE(stateChanged.last().first().toBool());
}

// Exporting says it started and says it finished, and what it left behind is a database the product can open.
TEST(ConfigurationManagerTest, ExportsTheConfigurationAndReleasesTheTransferItHeld) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("workpane.sqlite3"));
    persistence::StateStore store(databasePath);
    ASSERT_TRUE(store.initialize().hasValue());
    ASSERT_TRUE(store.saveSettings(QStringLiteral("core"), QJsonObject{{QStringLiteral("language"), QStringLiteral("pt-br")}}).hasValue());
    persistence::DatabaseExecutor executor(databasePath);
    app::ConfigurationManager manager(executor, directory.filePath(QStringLiteral("pending.sqlite3")), {});
    QSignalSpy stateChanged(&manager, &app::ConfigurationManager::transferStateChanged);

    const QString exported = directory.filePath(QStringLiteral("export.sqlite3"));
    ASSERT_TRUE(test::TestFutures::awaitFuture(manager.exportConfiguration(exported)).hasValue());

    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&stateChanged]() { return stateChanged.count() == 2; }));
    // clang-format on
    EXPECT_TRUE(stateChanged.first().first().toBool());
    EXPECT_FALSE(stateChanged.last().first().toBool());

    // What was written is a database this version opens, carrying what the settings of the shell held.
    persistence::StateStore snapshot(exported);
    ASSERT_TRUE(snapshot.initialize().hasValue());
    EXPECT_EQ(snapshot.settings(QStringLiteral("core")).value(QStringLiteral("language")).toString(), QStringLiteral("pt-br"));

    // The transfer was released, so the next one is not refused as one that is already running.
    ASSERT_TRUE(test::TestFutures::awaitFuture(manager.exportConfiguration(directory.filePath(QStringLiteral("again.sqlite3")))).hasValue());
}

TEST(ApplicationSettingsTest, LoadsPersistsAndValidatesTheApplicationSettingsDocument) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("workpane.sqlite3"));
    QString persistedLanguage;
    const QString persistedTheme = QStringLiteral("blue");

    {
        persistence::StateStore store(path);
        {
            persistence::DatabaseExecutor databaseExecutor(path);
            app::ApplicationSettingsStore settings(store, databaseExecutor);
            ASSERT_TRUE(settings.initialize().hasValue());
            EXPECT_TRUE(settings.windowGeometry().isEmpty());
            EXPECT_EQ(settings.language(), domain::ApplicationLanguages::resolveApplicationLanguage(QLocale::system().name(QLocale::TagSeparator::Dash)));
            EXPECT_EQ(settings.themeId(), QStringLiteral("green"));
            persistedLanguage = settings.language() == QStringLiteral("pt") ? QStringLiteral("en") : QStringLiteral("pt");
            QSignalSpy languageChanged(&settings, &app::ApplicationSettingsStore::languageChanged);
            QSignalSpy themeChanged(&settings, &app::ApplicationSettingsStore::themeChanged);
            settings.setWindowGeometry({});
            settings.setWindowGeometry(QByteArrayLiteral("geometry"));
            settings.setWindowGeometry(QByteArrayLiteral("geometry"));
            ASSERT_TRUE(settings.setLanguage(persistedLanguage).hasValue());
            EXPECT_TRUE(settings.setLanguage(persistedLanguage).hasValue());
            EXPECT_EQ(settings.setLanguage(QStringLiteral("de")).error().code, QStringLiteral("application_language_invalid"));
            ASSERT_TRUE(settings.setTheme(persistedTheme).hasValue());
            EXPECT_TRUE(settings.setTheme(persistedTheme).hasValue());
            EXPECT_EQ(settings.setTheme(QStringLiteral("missing")).error().code, QStringLiteral("application_theme_invalid"));
            EXPECT_EQ(settings.windowGeometry(), QByteArrayLiteral("geometry"));
            EXPECT_EQ(settings.language(), persistedLanguage);
            EXPECT_EQ(settings.themeId(), persistedTheme);
            EXPECT_EQ(languageChanged.count(), 1);
            EXPECT_EQ(themeChanged.count(), 1);
        }
        ASSERT_TRUE(store.markShutdown(true).hasValue());
    }

    persistence::StateStore store(path);
    persistence::DatabaseExecutor databaseExecutor(path);
    app::ApplicationSettingsStore settings(store, databaseExecutor);
    ASSERT_TRUE(settings.initialize().hasValue());
    EXPECT_EQ(settings.windowGeometry(), QByteArrayLiteral("geometry"));
    EXPECT_EQ(settings.language(), persistedLanguage);
    EXPECT_EQ(settings.themeId(), persistedTheme);
}

TEST(ApplicationSettingsTest, KeepsTheDeclaredDefaultForEveryValueItCannotUse) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("workpane.sqlite3"));

    {
        persistence::StateStore store(path);
        ASSERT_TRUE(store.initialize().hasValue());
        ASSERT_TRUE(store.saveSettings(QStringLiteral("workpane"), {{QStringLiteral("themeId"), QStringLiteral("unavailable")}}).hasValue());
    }

    // A stored theme nobody offers is the default theme, and a value nobody declared changes nothing.
    persistence::StateStore store(path);
    persistence::DatabaseExecutor databaseExecutor(path);
    app::ApplicationSettingsStore settings(store, databaseExecutor);
    ASSERT_TRUE(settings.initialize().hasValue());
    EXPECT_EQ(settings.themeId(), QStringLiteral("green"));
    EXPECT_EQ(settings.language(), domain::ApplicationLanguages::resolveApplicationLanguage(QLocale::system().name(QLocale::TagSeparator::Dash)));

    ASSERT_TRUE(store.saveSettings(QStringLiteral("workpane"), {{QStringLiteral("nobodyDeclaresThis"), true}}).hasValue());
    persistence::StateStore unknownStore(path);
    persistence::DatabaseExecutor unknownExecutor(path);
    app::ApplicationSettingsStore unknown(unknownStore, unknownExecutor);
    ASSERT_TRUE(unknown.initialize().hasValue());
    EXPECT_EQ(unknown.themeId(), QStringLiteral("green"));

    // A value of the wrong type is the declared default, for the shell exactly as for every plugin.
    const QVector<QJsonObject> mistyped{{{QStringLiteral("windowGeometry"), 7}}, {{QStringLiteral("language"), 7}}, {{QStringLiteral("themeId"), 7}}};

    for (const auto& document : mistyped) {
        ASSERT_TRUE(store.saveSettings(QStringLiteral("workpane"), document).hasValue());
        persistence::StateStore mistypedStore(path);
        persistence::DatabaseExecutor mistypedExecutor(path);
        app::ApplicationSettingsStore mistypedSettings(mistypedStore, mistypedExecutor);
        ASSERT_TRUE(mistypedSettings.initialize().hasValue()) << QJsonDocument(document).toJson(QJsonDocument::Compact).toStdString();
        EXPECT_EQ(mistypedSettings.themeId(), QStringLiteral("green"));
        EXPECT_TRUE(mistypedSettings.windowGeometry().isEmpty());
        EXPECT_TRUE(domain::ApplicationLanguages::isSupportedApplicationLanguage(mistypedSettings.language()));
    }
}

TEST(ApplicationSettingsTest, RollsBackLanguageWhenAsynchronousPersistenceFails) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    persistence::StateStore store(directory.filePath(QStringLiteral("workpane.sqlite3")));
    persistence::DatabaseExecutor unavailableExecutor(directory.filePath(QStringLiteral("missing/workpane.sqlite3")));
    app::ApplicationSettingsStore settings(store, unavailableExecutor);
    ASSERT_TRUE(settings.initialize().hasValue());
    const QString committedLanguage = settings.language();
    const QString requestedLanguage = committedLanguage == QStringLiteral("en") ? QStringLiteral("pt") : QStringLiteral("en");
    QSignalSpy languageChanged(&settings, &app::ApplicationSettingsStore::languageChanged);
    QSignalSpy saveFailed(&settings, &app::ApplicationSettingsStore::saveFailed);

    ASSERT_TRUE(settings.setLanguage(requestedLanguage).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return saveFailed.count() == 1; }, 3000));
    // clang-format on
    EXPECT_EQ(settings.language(), committedLanguage);
    EXPECT_EQ(languageChanged.count(), 2);
}

TEST(ApplicationSettingsTest, CommitsWhatReachedStorageWhenManySavesOverlap) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("workpane.sqlite3"));
    const QStringList themes{QStringLiteral("green"), QStringLiteral("blue"), QStringLiteral("red")};
    QString lastRequested;
    {
        persistence::StateStore store(path);
        persistence::DatabaseExecutor executor(path);
        app::ApplicationSettingsStore settings(store, executor);
        ASSERT_TRUE(settings.initialize().hasValue());
        QSignalSpy saveFailed(&settings, &app::ApplicationSettingsStore::saveFailed);

        for (int round = 0; round < 60; ++round) {
            lastRequested = themes.at(round % themes.size());
            ASSERT_TRUE(settings.setTheme(lastRequested).hasValue());
            ASSERT_TRUE(settings.setLanguage(round % 2 == 0 ? QStringLiteral("en") : QStringLiteral("pt")).hasValue());
        }
        // clang-format off
        ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return settings.themeId() == lastRequested; }));
        // clang-format on
        EXPECT_EQ(saveFailed.count(), 0);
    }

    // What the next start reads is what the last write put there, because a memory that disagrees with storage is a lie that start discovers.
    persistence::StateStore reopened(path);
    persistence::DatabaseExecutor reopenedExecutor(path);
    app::ApplicationSettingsStore restored(reopened, reopenedExecutor);
    ASSERT_TRUE(restored.initialize().hasValue());
    EXPECT_EQ(restored.themeId(), lastRequested);
}

TEST(ApplicationSettingsTest, RollsBackThemeWhenAsynchronousPersistenceFails) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    persistence::StateStore store(directory.filePath(QStringLiteral("workpane.sqlite3")));
    persistence::DatabaseExecutor unavailableExecutor(directory.filePath(QStringLiteral("missing/workpane.sqlite3")));
    app::ApplicationSettingsStore settings(store, unavailableExecutor);
    ASSERT_TRUE(settings.initialize().hasValue());
    QSignalSpy themeChanged(&settings, &app::ApplicationSettingsStore::themeChanged);
    QSignalSpy saveFailed(&settings, &app::ApplicationSettingsStore::saveFailed);

    ASSERT_TRUE(settings.setTheme(QStringLiteral("blue")).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return saveFailed.count() == 1; }, 3000));
    // clang-format on
    EXPECT_EQ(settings.themeId(), QStringLiteral("green"));
    EXPECT_EQ(themeChanged.count(), 2);
}

TEST(AppStyleTest, DefinesCompletePaletteMetricsHintsAndControlSizes) {
    const std::array roles{ui::AppStyle::Color::Window, ui::AppStyle::Color::Panel, ui::AppStyle::Color::Raised, ui::AppStyle::Color::Hover, ui::AppStyle::Color::Pressed, ui::AppStyle::Color::Border, ui::AppStyle::Color::BorderStrong, ui::AppStyle::Color::Text, ui::AppStyle::Color::TextMuted, ui::AppStyle::Color::Accent, ui::AppStyle::Color::AccentHover, ui::AppStyle::Color::OnAccent, ui::AppStyle::Color::Success, ui::AppStyle::Color::Warning, ui::AppStyle::Color::Danger, ui::AppStyle::Color::DangerBackground, ui::AppStyle::Color::DangerText, ui::AppStyle::Color::Terminal};

    for (const auto role : roles) {
        EXPECT_TRUE(ui::AppStyle::color(role).isValid());
    }

    const QPalette palette = ui::AppStyle::applicationPalette();
    EXPECT_EQ(palette.color(QPalette::Window), ui::AppStyle::color(ui::AppStyle::Color::Window));
    EXPECT_EQ(palette.color(QPalette::Highlight), ui::AppStyle::color(ui::AppStyle::Color::Accent));

    ui::AppStyle style(QStyleFactory::create(QStringLiteral("Fusion")));
    EXPECT_EQ(style.pixelMetric(QStyle::PM_DefaultFrameWidth), 1);
    EXPECT_EQ(style.pixelMetric(QStyle::PM_ScrollBarExtent), 10);
    EXPECT_EQ(style.pixelMetric(QStyle::PM_SplitterWidth), 1);
    EXPECT_EQ(style.styleHint(QStyle::SH_ToolButtonStyle), Qt::ToolButtonIconOnly);
    EXPECT_TRUE(style.styleHint(QStyle::SH_Menu_AllowActiveAndDisabled));
    EXPECT_EQ(style.sizeFromContents(QStyle::CT_PushButton, nullptr, QSize(40, 20)), QSize(40, 20));
}

TEST(ToastOverlayTest, StacksBoundedToastsAndDismissesThemWithoutChangingHostLayout) {
    QWidget host;
    host.resize(900, 600);
    auto* content = new QWidget(&host);
    auto* layout = new QVBoxLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(content);
    host.show();
    QApplication::processEvents();
    const QRect contentGeometry = content->geometry();

    const ui::ThemeCatalog catalog;
    auto* overlay = new ui::ToastOverlay(catalog.defaultTheme(), &host);
    overlay->showNotification(QStringLiteral("First"), QStringLiteral("Message"), plugins::AlertSeverity::Information);
    overlay->showNotification(QStringLiteral("Second"), QString{}, plugins::AlertSeverity::Success);
    QApplication::processEvents();
    EXPECT_EQ(content->geometry(), contentGeometry);
    auto toasts = overlay->findChildren<QWidget*>(QStringLiteral("toast"));
    ASSERT_EQ(toasts.size(), 2);
    EXPECT_LT(toasts.at(0)->geometry().bottom(), toasts.at(1)->geometry().top());
    EXPECT_GT(overlay->geometry().left(), host.width() / 2);
    auto* secondMessage = toasts.at(1)->findChild<QLabel*>(QStringLiteral("toastMessage"));
    ASSERT_NE(secondMessage, nullptr);
    EXPECT_FALSE(secondMessage->isVisible());

    for (int index = 0; index < 6; ++index) {
        overlay->showNotification(QStringLiteral("Extra %1").arg(index), QStringLiteral("Message"), plugins::AlertSeverity::Warning);
    }
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return overlay->findChildren<QWidget*>(QStringLiteral("toast")).size() <= 4; }, 3000));
    // clang-format on

    overlay->applyTheme(*catalog.find(QStringLiteral("red")));
    overlay->dismissAll();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return overlay->findChildren<QWidget*>(QStringLiteral("toast")).isEmpty(); }, 3000));
    // clang-format on
    EXPECT_EQ(content->geometry(), contentGeometry);
}

TEST(ToastOverlayTest, CoversOnlyItsLiveToastsAndDisappearsWhenEmpty) {
    QWidget host;
    host.resize(900, 600);
    auto* content = new QWidget(&host);
    auto* layout = new QVBoxLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(content);
    host.show();
    host.layout()->activate();
    QApplication::processEvents();

    const ui::ThemeCatalog catalog;
    auto* overlay = new ui::ToastOverlay(catalog.defaultTheme(), &host);
    QApplication::processEvents();

    // An overlay without any toast must not sit over the window, otherwise it intercepts every click.
    EXPECT_FALSE(overlay->isVisible());

    overlay->showNotification(QStringLiteral("First"), QStringLiteral("Message"), plugins::AlertSeverity::Information);
    overlay->showNotification(QStringLiteral("Second"), QStringLiteral("Message"), plugins::AlertSeverity::Warning);
    QApplication::processEvents();
    EXPECT_TRUE(overlay->isVisible());
    EXPECT_NE(overlay->geometry(), host.rect());
    EXPECT_FALSE(overlay->geometry().contains(host.rect().center()));
    EXPECT_FALSE(overlay->geometry().contains(QPoint(4, 4)));
    EXPECT_GT(overlay->geometry().left(), host.width() / 2);
    EXPECT_LT(overlay->geometry().bottom(), host.height());

    const auto toasts = overlay->findChildren<QWidget*>(QStringLiteral("toast"));
    ASSERT_EQ(toasts.size(), 2);

    for (const auto* toast : toasts) {
        EXPECT_TRUE(overlay->mask().contains(toast->geometry()));
    }

    overlay->dismissAll();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return overlay->findChildren<QWidget*>(QStringLiteral("toast")).isEmpty(); }, 3000));
    // clang-format on
    EXPECT_FALSE(overlay->isVisible());
}

TEST(ToastOverlayTest, DismissesTheToastFromItsOwnCloseButton) {
    QWidget host;
    host.resize(900, 600);
    auto* content = new QWidget(&host);
    auto* layout = new QVBoxLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(content);
    host.show();
    QApplication::processEvents();

    const ui::ThemeCatalog catalog;
    auto* overlay = new ui::ToastOverlay(catalog.defaultTheme(), &host);
    overlay->showNotification(QStringLiteral("Only"), QStringLiteral("Message"), plugins::AlertSeverity::Error);
    QApplication::processEvents();
    auto toasts = overlay->findChildren<QWidget*>(QStringLiteral("toast"));
    ASSERT_EQ(toasts.size(), 1);

    auto* close = toasts.first()->findChild<QToolButton*>(QStringLiteral("toastClose"));
    ASSERT_NE(close, nullptr);
    EXPECT_FALSE(overlay->testAttribute(Qt::WA_TransparentForMouseEvents));
    EXPECT_FALSE(close->testAttribute(Qt::WA_TransparentForMouseEvents));
    EXPECT_TRUE(close->isEnabled());

    QTest::mouseClick(close, Qt::LeftButton);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return overlay->findChildren<QWidget*>(QStringLiteral("toast")).isEmpty(); }, 3000));
    // clang-format on
    EXPECT_FALSE(overlay->isVisible());
}

// Qt answers the Quit standard key with a media key nobody can press, so the application declares the combination instead of asking for that one.
TEST(ApplicationShortcutsTest, QuitsWithACombinationEveryKeyboardCarries) {
    const QKeySequence quit = ui::ApplicationShortcuts::quit();
    ASSERT_EQ(quit.count(), 1);
    EXPECT_EQ(quit, QKeySequence(Qt::CTRL | Qt::Key_Q));

    // The control modifier is what Qt maps to the native key, which is Command on macOS and Control everywhere else.
    EXPECT_EQ(quit[0].keyboardModifiers(), Qt::ControlModifier);
    EXPECT_EQ(quit[0].key(), Qt::Key_Q);

    // The standard key answers Qt::Key_Exit outside macOS and nothing at all under some platform themes, which is why it is not used.
    EXPECT_NE(quit[0].key(), Qt::Key_Exit);
}

TEST(ApplicationShortcutsTest, ZoomsWithEveryKeyAKeyboardPutsInFrontOfTheReader) {
    const QList<QKeySequence> increase = ui::ApplicationShortcuts::increaseContentFont();
    const QList<QKeySequence> decrease = ui::ApplicationShortcuts::decreaseContentFont();
    const QList<QKeySequence> reset = ui::ApplicationShortcuts::resetContentFont();

    // The plain keys of the market convention are what each direction opens with.
    EXPECT_EQ(increase.first(), QKeySequence(Qt::CTRL | Qt::Key_Equal));
    EXPECT_EQ(decrease.first(), QKeySequence(Qt::CTRL | Qt::Key_Minus));
    EXPECT_EQ(reset.first(), QKeySequence(Qt::CTRL | Qt::Key_0));

    // A keyboard with a numeric pad puts the plus and the minus there, and that is where a reader reaches for them.
    EXPECT_TRUE(increase.contains(QKeySequence(Qt::ControlModifier | Qt::KeypadModifier | Qt::Key_Plus)));
    EXPECT_TRUE(decrease.contains(QKeySequence(Qt::ControlModifier | Qt::KeypadModifier | Qt::Key_Minus)));
    EXPECT_TRUE(reset.contains(QKeySequence(Qt::ControlModifier | Qt::KeypadModifier | Qt::Key_0)));

    // Qt answers the zoom standard keys with the plus and the minus, so increasing accepts the one it names.
    EXPECT_TRUE(increase.contains(QKeySequence(Qt::CTRL | Qt::Key_Plus)));
    EXPECT_TRUE(QKeySequence::keyBindings(QKeySequence::ZoomIn).contains(QKeySequence(Qt::CTRL | Qt::Key_Plus)));
    EXPECT_TRUE(QKeySequence::keyBindings(QKeySequence::ZoomOut).contains(QKeySequence(Qt::CTRL | Qt::Key_Minus)));

    for (const auto& direction : {increase, decrease, reset}) {
        ASSERT_FALSE(direction.isEmpty());
        for (const auto& sequence : direction) {
            ASSERT_EQ(sequence.count(), 1) << sequence.toString().toStdString();
            EXPECT_TRUE(sequence[0].keyboardModifiers().testFlag(Qt::ControlModifier)) << sequence.toString().toStdString();
            // The shifted forms belong to the shell, so no direction takes one.
            EXPECT_FALSE(sequence[0].keyboardModifiers().testFlag(Qt::ShiftModifier)) << sequence.toString().toStdString();
        }
    }
}
TEST(SharedComponentsTest, PaintsItsOwnIndicatorOnTheSelectableField) {
    const ui::Theme& theme = ui::ThemeManager::instance().theme();
    QWidget host;
    auto* combo = new ui::ComboBox(theme, &host);
    combo->addItems({QStringLiteral("Vivid"), QStringLiteral("Balanced")});
    host.resize(320, 60);
    host.show();

    // The platform drop-down box is suppressed, so nothing but the painted indicator marks the field.
    EXPECT_TRUE(combo->styleSheet().contains(QStringLiteral("QComboBox::down-arrow")));
    EXPECT_TRUE(combo->styleSheet().contains(QStringLiteral("image: none")));
    EXPECT_FALSE(combo->styleSheet().contains(QLatin1Char('@')));

    const QImage painted = combo->grab().toImage();
    const int indicator = combo->width() - theme.metric(ui::ThemeMetric::ComboIndicatorWidth) / 2;
    const QColor background = painted.pixelColor(1, painted.height() / 2);
    int indicatorPixels = 0;

    for (int y = 0; y < painted.height(); ++y) {
        for (int x = std::max(0, indicator - 6); x < std::min(indicator + 6, painted.width()); ++x) {
            indicatorPixels += painted.pixelColor(x, y) != background ? 1 : 0;
        }
    }

    EXPECT_GT(indicatorPixels, 0) << "the selectable field paints its own indicator";
}

TEST(SharedComponentsTest, ChangesTheRevealIconWhenTheSecretBecomesVisible) {
    const ui::Theme& theme = ui::ThemeManager::instance().theme();
    QWidget host;
    bool allowed = false;
    // clang-format off
    auto* secret = new ui::SecretField(theme, QStringLiteral("Key"), [&allowed]() { return allowed; }, &host);
    // clang-format on
    secret->setValue(QStringLiteral("sk-test"));
    host.show();

    auto* reveal = secret->findChild<QToolButton*>();
    auto* editor = secret->findChild<QLineEdit*>();
    ASSERT_NE(reveal, nullptr);
    ASSERT_NE(editor, nullptr);
    const QImage masked = reveal->icon().pixmap(16, 16).toImage();
    EXPECT_FALSE(secret->revealed());
    EXPECT_EQ(editor->echoMode(), QLineEdit::Password);

    // A refused confirmation keeps the value masked and the icon unchanged.
    reveal->click();
    EXPECT_FALSE(secret->revealed());
    EXPECT_EQ(reveal->icon().pixmap(16, 16).toImage(), masked);

    allowed = true;
    reveal->click();
    EXPECT_TRUE(secret->revealed());
    EXPECT_EQ(editor->echoMode(), QLineEdit::Normal);
    const QImage revealed = reveal->icon().pixmap(16, 16).toImage();
    EXPECT_NE(revealed, masked) << "the button must state that the next click hides the value";

    reveal->click();
    EXPECT_FALSE(secret->revealed());
    EXPECT_EQ(reveal->icon().pixmap(16, 16).toImage(), masked);
}

TEST(SharedComponentsTest, BuildsThePageHeaderGridButtonsAndLabelsFromTheActiveTheme) {
    const ui::Theme& theme = ui::ThemeManager::instance().theme();
    QWidget host;

    auto* header = new ui::PageHeader(theme, QStringLiteral("Workspace"), &host);
    EXPECT_EQ(header->objectName(), QStringLiteral("pageHeader"));
    EXPECT_EQ(header->height(), theme.metric(ui::ThemeMetric::PageHeaderHeight));
    auto* headerTitle = header->findChild<QLabel*>(QStringLiteral("pageTitle"));
    ASSERT_NE(headerTitle, nullptr);
    EXPECT_EQ(headerTitle->text(), QStringLiteral("Workspace"));
    header->setTitle(QStringLiteral("Renamed"));
    EXPECT_EQ(headerTitle->text(), QStringLiteral("Renamed"));
    auto* trailing = new QLabel(QStringLiteral("trailing"), header);
    header->addStretch();
    header->addWidget(trailing);
    EXPECT_EQ(trailing->parentWidget(), header);

    auto* grid = ui::Components::dataGrid({QStringLiteral("Name"), QStringLiteral("Value")}, &host);
    EXPECT_EQ(grid->columnCount(), 2);
    EXPECT_EQ(grid->horizontalHeaderItem(0)->text(), QStringLiteral("Name"));
    EXPECT_EQ(grid->editTriggers(), QAbstractItemView::NoEditTriggers);
    EXPECT_EQ(grid->selectionBehavior(), QAbstractItemView::SelectRows);
    EXPECT_EQ(grid->selectionMode(), QAbstractItemView::SingleSelection);
    EXPECT_TRUE(grid->alternatingRowColors());
    EXPECT_FALSE(grid->showGrid());
    EXPECT_TRUE(grid->verticalHeader()->isHidden());
    EXPECT_TRUE(grid->horizontalHeader()->stretchLastSection());

    auto* button = ui::Components::toolButton(ui::IconName::Refresh, theme, QStringLiteral("Refresh"), &host);
    EXPECT_EQ(button->objectName(), QStringLiteral("toolbarIconButton"));
    EXPECT_EQ(button->toolTip(), QStringLiteral("Refresh"));
    EXPECT_FALSE(button->icon().isNull());
    EXPECT_EQ(button->width(), theme.metric(ui::ThemeMetric::CompactButtonSize));
    EXPECT_EQ(button->height(), theme.metric(ui::ThemeMetric::CompactButtonSize));
    EXPECT_EQ(button->iconSize(), QSize(theme.metric(ui::ThemeMetric::SmallIconSize), theme.metric(ui::ThemeMetric::SmallIconSize)));

    auto* section = ui::Components::sectionTitleLabel(QStringLiteral("General"), &host);
    EXPECT_EQ(section->objectName(), QStringLiteral("settingsSectionTitle"));
    auto* empty = ui::Components::emptyStateLabel(QStringLiteral("Nothing here"), &host);
    EXPECT_EQ(empty->objectName(), QStringLiteral("emptyState"));
    EXPECT_EQ(empty->alignment(), Qt::AlignCenter);
    EXPECT_TRUE(empty->wordWrap());

    auto* indicator = new ui::StatusIndicator(&host);
    EXPECT_EQ(indicator->objectName(), QStringLiteral("statusIndicator"));
    EXPECT_EQ(indicator->width(), indicator->height());
    indicator->setColor(theme.color(ui::ThemeColor::Success));

    const QDateTime captured = QDateTime(QDate(2026, 8, 15), QTime(12, 30), QTimeZone::UTC);
    EXPECT_EQ(ui::Components::localTimestamp(captured), QLocale::system().toString(captured.toLocalTime(), QLocale::ShortFormat));
}

TEST(StoredValuesTest, NormalizesTimestampsAndRejectsEveryInvalidStoredValue) {
    const QDateTime local = QDateTime(QDate(2026, 8, 15), QTime(9, 0), QTimeZone::systemTimeZone());
    const QString stored = persistence::StoredValues::storedTimestamp(local);
    EXPECT_TRUE(stored.endsWith(QLatin1Char('Z')));
    EXPECT_EQ(persistence::StoredValues::parseStoredTimestamp(stored).toMSecsSinceEpoch(), local.toMSecsSinceEpoch());
    EXPECT_TRUE(persistence::StoredValues::validStoredTimestamp(persistence::StoredValues::parseStoredTimestamp(stored)));

    EXPECT_FALSE(persistence::StoredValues::parseStoredTimestamp(QString{}).isValid());
    EXPECT_FALSE(persistence::StoredValues::parseStoredTimestamp(QStringLiteral("not a timestamp")).isValid());
    EXPECT_FALSE(persistence::StoredValues::validStoredTimestamp(QDateTime{}));
    EXPECT_FALSE(persistence::StoredValues::validStoredTimestamp(local));
    EXPECT_FALSE(persistence::StoredValues::validStoredTimestamp(persistence::StoredValues::parseStoredTimestamp(QStringLiteral("2026-08-15T09:00:00.000-03:00"))));

    qint64 value = -1;
    EXPECT_TRUE(persistence::StoredValues::readStoredInteger(QVariant(7), value));
    EXPECT_EQ(value, 7);
    EXPECT_TRUE(persistence::StoredValues::readStoredInteger(QVariant(qint64{9000000000}), value));
    EXPECT_EQ(value, 9000000000);
    EXPECT_FALSE(persistence::StoredValues::readStoredInteger(QVariant(1.5), value));
    EXPECT_FALSE(persistence::StoredValues::readStoredInteger(QVariant(QStringLiteral("7")), value));
    EXPECT_FALSE(persistence::StoredValues::readStoredInteger(QVariant{}, value));
}

TEST(PluginPayloadTest, AcceptsExactKeysAndExactIntegersOnly) {
    const QJsonObject payload{{QStringLiteral("limit"), 10}, {QStringLiteral("beforeSequence"), 0}};
    EXPECT_TRUE(plugins::SettingsReaders::hasExactKeys(payload, {QStringLiteral("limit"), QStringLiteral("beforeSequence")}));
    EXPECT_FALSE(plugins::SettingsReaders::hasExactKeys(payload, {QStringLiteral("limit")}));
    EXPECT_FALSE(plugins::SettingsReaders::hasExactKeys(payload, {QStringLiteral("limit"), QStringLiteral("beforeSequence"), QStringLiteral("extra")}));
    EXPECT_TRUE(plugins::SettingsReaders::hasExactKeys(QJsonObject{}, {}));

    qint64 value = -1;
    EXPECT_TRUE(plugins::SettingsReaders::readJsonInteger(QJsonValue(42), value));
    EXPECT_EQ(value, 42);
    EXPECT_FALSE(plugins::SettingsReaders::readJsonInteger(QJsonValue(1.5), value));
    EXPECT_FALSE(plugins::SettingsReaders::readJsonInteger(QJsonValue(QStringLiteral("42")), value));
    EXPECT_FALSE(plugins::SettingsReaders::readJsonInteger(QJsonValue(), value));
    EXPECT_FALSE(plugins::SettingsReaders::readJsonInteger(QJsonValue(std::numeric_limits<double>::infinity()), value));
    EXPECT_FALSE(plugins::SettingsReaders::readJsonInteger(QJsonValue(9.3e18), value));
}

TEST(StateStoreTest, ReplacesEveryStoredCoreVersionItCannotUseAndKeepsTheOldFile) {
    for (const int storedVersion : {2, 3, 7}) {
        QTemporaryDir directory;
        ASSERT_TRUE(directory.isValid());
        const QString databasePath = directory.filePath(QStringLiteral("workpane.sqlite3"));
        {
            const QString connectionName = QStringLiteral("unsupported-%1").arg(storedVersion);
            QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            database.setDatabaseName(databasePath);
            ASSERT_TRUE(database.open());
            QSqlQuery query(database);
            ASSERT_TRUE(query.exec(QStringLiteral("PRAGMA user_version = %1").arg(storedVersion)));
            database.close();
            database = {};
            QSqlDatabase::removeDatabase(connectionName);
        }

        // A database this version cannot use is set aside and a new one takes its place, so the application still opens.
        persistence::StateStore store(databasePath);
        ASSERT_TRUE(store.initialize().hasValue());
        EXPECT_TRUE(store.wasCleanShutdown().hasValue());
        EXPECT_FALSE(store.replacedDatabasePath().isEmpty());
        EXPECT_TRUE(QFileInfo::exists(store.replacedDatabasePath()));
        EXPECT_TRUE(QFileInfo::exists(databasePath));
    }
}

TEST(TabBarTest, PaintsApplicationTabsAndReplacesTheCloseButton) {
    const ui::Theme& theme = ui::ThemeManager::instance().theme();
    ui::TabWidget tabs(theme, nullptr);
    tabs.setTabsClosable(true);
    tabs.addTab(new QWidget(&tabs), QStringLiteral("First"));
    tabs.addTab(new QWidget(&tabs), QStringLiteral("Second"));
    tabs.resize(600, 60);
    tabs.show();

    auto* bar = tabs.findChild<ui::TabBar*>();
    ASSERT_NE(bar, nullptr);
    EXPECT_EQ(bar->count(), 2);
    EXPECT_EQ(bar->height(), theme.metric(ui::ThemeMetric::WorkspaceBarHeight));

    for (int index = 0; index < bar->count(); ++index) {
        auto* close = qobject_cast<ui::TabCloseButton*>(bar->tabButton(index, QTabBar::RightSide));
        ASSERT_NE(close, nullptr) << "tab " << index << " keeps the Qt close button";
        EXPECT_EQ(close->size(), close->sizeHint());
    }

    QSignalSpy closeRequested(bar, &QTabBar::tabCloseRequested);
    qobject_cast<ui::TabCloseButton*>(bar->tabButton(1, QTabBar::RightSide))->click();
    ASSERT_EQ(closeRequested.count(), 1);
    EXPECT_EQ(closeRequested.first().first().toInt(), 1);

    const QImage painted = bar->grab().toImage();
    EXPECT_FALSE(painted.isNull());
    EXPECT_EQ(painted.pixelColor(2, 0), theme.color(ui::ThemeColor::Window)) << "the selected tab never paints an accent bar";

    // clang-format off
    const auto containsColor = [&painted](const QRect& area, const QColor& color) {
        for (int y = area.top(); y <= area.bottom(); ++y) {
            for (int x = area.left(); x <= area.right(); ++x) {
                if (painted.pixelColor(x, y) == color) {
                    return true;
                }
            }
        }
        return false;
    };
    // clang-format on
    EXPECT_TRUE(containsColor(bar->tabRect(0), theme.color(ui::ThemeColor::Accent))) << "the selected tab marks itself with the accent indicator";
    EXPECT_TRUE(containsColor(bar->tabRect(1), theme.color(ui::ThemeColor::TextMuted))) << "an unselected tab marks itself with the muted indicator";
    EXPECT_FALSE(containsColor(bar->tabRect(1), theme.color(ui::ThemeColor::Accent)));

    // The divider belongs to the component, so a surface that renames the widget keeps it.
    tabs.setObjectName(QStringLiteral("renamedTabs"));
    EXPECT_TRUE(tabs.styleSheet().contains(QStringLiteral("QTabWidget::pane")));
    EXPECT_FALSE(tabs.styleSheet().contains(QStringLiteral("#appTabWidget")));

    const QImage tabsPainted = tabs.grab().toImage();
    const int dividerY = tabs.tabBar()->geometry().bottom() + 1;
    EXPECT_EQ(tabsPainted.pixelColor(tabs.width() / 2, dividerY), theme.color(ui::ThemeColor::Border)) << "a tab strip always separates itself from the content below";

    // The divider is a reserved row rather than a painted one, so the page below it can never cover it.
    EXPECT_NE(tabsPainted.pixelColor(tabs.width() / 2, dividerY + 1), theme.color(ui::ThemeColor::Border));
    EXPECT_EQ(tabs.widget(0)->mapTo(&tabs, QPoint(0, 0)).y(), dividerY + 1);
}

TEST(ThemeTest, ResolvesEveryStyleTokenAndNeverLeavesAPrefixBehind) {
    const ui::ThemeCatalog catalog;
    const ui::Theme& theme = catalog.defaultTheme();
    const QString resolved = ui::ThemeTokens::substituted(QStringLiteral("@text @textMuted @border @borderStrong @accent @accentHover @accentStrong @onAccent @onDanger @danger @dangerHover @dangerBackground @dangerText @window @panel @raised @hover @success @warning @terminal @badgeRadius @badgeHorizontalPadding @badgeVerticalPadding @controlRadius @controlHorizontalPadding @controlVerticalPadding @scrollBarExtent @interfaceFontSize @pageTitleFontSize @sectionTitleFontSize"), theme);
    EXPECT_FALSE(resolved.contains(QLatin1Char('@')));
    EXPECT_TRUE(resolved.contains(theme.color(ui::ThemeColor::TextMuted).name()));
    EXPECT_TRUE(resolved.contains(theme.color(ui::ThemeColor::BorderStrong).name()));

    // A filled destructive surface carries its own content color, so an icon on it never stays neutral grey.
    for (const auto& candidate : catalog.themes()) {
        EXPECT_NE(candidate->color(ui::ThemeColor::OnDanger), candidate->color(ui::ThemeColor::TextMuted));
        EXPECT_GT(candidate->color(ui::ThemeColor::OnDanger).lightness(), candidate->color(ui::ThemeColor::Danger).lightness());
    }

    EXPECT_NE(ui::IconCatalog::destructiveIcon(ui::IconName::Clear, theme).pixmap(16, 16).toImage(), ui::IconCatalog::icon(ui::IconName::Clear, theme).pixmap(16, 16).toImage());

    // The strong accent is darker than the accent in every built-in theme, so a filled chip separates itself from a selected row.
    for (const auto& candidate : catalog.themes()) {
        EXPECT_LT(candidate->color(ui::ThemeColor::AccentStrong).lightness(), candidate->color(ui::ThemeColor::Accent).lightness()) << qPrintable(candidate->id());
        EXPECT_NE(candidate->color(ui::ThemeColor::AccentStrong), candidate->color(ui::ThemeColor::Accent));
    }

    EXPECT_TRUE(resolved.contains(QString::number(theme.metric(ui::ThemeMetric::ControlRadius))));
    EXPECT_TRUE(resolved.contains(QString::number(theme.metric(ui::ThemeMetric::ControlHorizontalPadding))));

    // A shorter token must never consume the prefix of a longer one.
    EXPECT_EQ(ui::ThemeTokens::substituted(QStringLiteral("@textMuted"), theme), theme.color(ui::ThemeColor::TextMuted).name());
    EXPECT_EQ(ui::ThemeTokens::substituted(QStringLiteral("@borderStrong"), theme), theme.color(ui::ThemeColor::BorderStrong).name());
    EXPECT_EQ(ui::ThemeTokens::substituted(QStringLiteral("@accentHover"), theme), theme.color(ui::ThemeColor::AccentHover).name());
    EXPECT_EQ(ui::ThemeTokens::substituted(QStringLiteral("@dangerBackground"), theme), theme.color(ui::ThemeColor::DangerBackground).name());
}

TEST(ThemeTest, ProvidesThreeCompleteThemesAndStrictSelection) {
    static_assert(std::is_abstract_v<ui::Theme>);
    const ui::ThemeCatalog catalog;
    ASSERT_EQ(catalog.themes().size(), 3U);
    EXPECT_EQ(catalog.themes().at(0)->id(), QStringLiteral("green"));
    EXPECT_EQ(catalog.themes().at(1)->id(), QStringLiteral("blue"));
    EXPECT_EQ(catalog.themes().at(2)->id(), QStringLiteral("red"));
    EXPECT_EQ(catalog.defaultTheme().id(), QStringLiteral("green"));
    EXPECT_EQ(catalog.themeOrDefault(QStringLiteral("missing")).id(), QStringLiteral("green"));

    const std::array colors{ui::ThemeColor::Window, ui::ThemeColor::Panel, ui::ThemeColor::Raised, ui::ThemeColor::Hover, ui::ThemeColor::Pressed, ui::ThemeColor::Border, ui::ThemeColor::BorderStrong, ui::ThemeColor::Text, ui::ThemeColor::TextMuted, ui::ThemeColor::Accent, ui::ThemeColor::AccentHover, ui::ThemeColor::OnAccent, ui::ThemeColor::Success, ui::ThemeColor::Warning, ui::ThemeColor::Danger, ui::ThemeColor::DangerBackground, ui::ThemeColor::DangerText, ui::ThemeColor::Terminal};
    const std::array metrics{ui::ThemeMetric::ModeBarMinimumWidth, ui::ThemeMetric::ModeButtonMinimumHeight, ui::ThemeMetric::PageHeaderHeight, ui::ThemeMetric::SmallIconSize, ui::ThemeMetric::ScrollBarExtent, ui::ThemeMetric::ControlRadius, ui::ThemeMetric::TerminalMinimumColumns};
    const std::array fonts{ui::ThemeFont::Interface, ui::ThemeFont::Navigation, ui::ThemeFont::PageTitle, ui::ThemeFont::SectionTitle, ui::ThemeFont::Monospace};
    QSet<QRgb> accents;

    for (const auto& theme : catalog.themes()) {
        EXPECT_FALSE(theme->id().isEmpty());
        EXPECT_FALSE(theme->titleKey().isEmpty());
        EXPECT_EQ(theme->color(ui::ThemeColor::OnAccent), QColor(Qt::white));
        for (const auto color : colors) {
            EXPECT_TRUE(theme->color(color).isValid());
        }
        for (const auto metric : metrics) {
            EXPECT_GT(theme->metric(metric), 0);
        }
        for (const auto font : fonts) {
            EXPECT_FALSE(theme->font(font).family().isEmpty());
        }
        accents.insert(theme->color(ui::ThemeColor::Accent).rgba());
    }

    EXPECT_EQ(accents.size(), 3);

    for (const auto& theme : catalog.themes()) {
        EXPECT_EQ(theme->color(ui::ThemeColor::Success), theme->color(ui::ThemeColor::AccentHover));
        EXPECT_NE(theme->color(ui::ThemeColor::Warning), theme->color(ui::ThemeColor::Accent));
    }

    ui::ThemeManager manager;
    EXPECT_EQ(manager.theme().id(), QStringLiteral("green"));
    ASSERT_TRUE(manager.selectTheme(QStringLiteral("blue")).hasValue());
    EXPECT_EQ(manager.theme().id(), QStringLiteral("blue"));
    EXPECT_EQ(manager.selectTheme(QStringLiteral("missing")).error().code, QStringLiteral("application_theme_invalid"));
    EXPECT_EQ(manager.theme().id(), QStringLiteral("blue"));
    manager.loadTheme(QStringLiteral("missing"));
    EXPECT_EQ(manager.theme().id(), QStringLiteral("green"));
}

TEST(IconsTest, RendersEveryDeclaredIcon) {
    const ui::Theme& theme = ui::ThemeManager::instance().theme();
    const QVector<ui::IconName> names = ui::IconCatalog::allIconNames();

    // An icon that ships without a shape reaches the interface as an empty button, so every one of them is rendered here.
    for (const auto name : names) {
        const QIcon icon = ui::IconCatalog::icon(name, theme);
        ASSERT_FALSE(icon.isNull()) << static_cast<int>(name);
        const QImage image = icon.pixmap(32, 32).toImage();
        ASSERT_FALSE(image.isNull()) << static_cast<int>(name);
        bool painted = false;
        for (int y = 0; y < image.height() && !painted; ++y) {
            for (int x = 0; x < image.width() && !painted; ++x) {
                painted = qAlpha(image.pixel(x, y)) > 0;
            }
        }
        EXPECT_TRUE(painted) << static_cast<int>(name);
    }
}

TEST(IconsTest, UsesWhiteForegroundForPrimaryActions) {
    const QImage image = ui::IconCatalog::primaryIcon(ui::IconName::Add, ui::ThemeManager::instance().catalog().themeOrDefault(QStringLiteral("red"))).pixmap(32, 32).toImage();
    bool foundOpaquePixel = false;

    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor color = image.pixelColor(x, y);
            if (color.alpha() < 200) {
                continue;
            }
            foundOpaquePixel = true;
            EXPECT_EQ(color.red(), 255);
            EXPECT_EQ(color.green(), 255);
            EXPECT_EQ(color.blue(), 255);
        }
    }

    EXPECT_TRUE(foundOpaquePixel);
}

TEST(IconsTest, RendersEveryVectorIconAndLayoutVariant) {
    const QVector<ui::IconName> names = ui::IconCatalog::allIconNames();

    for (const auto name : names) {
        const QIcon generated = ui::IconCatalog::icon(name, ui::ThemeManager::instance().theme());
        EXPECT_FALSE(generated.isNull());
        EXPECT_FALSE(generated.pixmap(32, 32).isNull());
        EXPECT_FALSE(ui::IconCatalog::icon(name, QColor(Qt::red)).pixmap(32, 32).isNull());
    }

    EXPECT_FALSE(ui::IconCatalog::layoutIcon(QStringLiteral("3-left"), 2, 2, 3, ui::ThemeManager::instance().theme()).pixmap(32, 32).isNull());
    EXPECT_FALSE(ui::IconCatalog::layoutIcon(QStringLiteral("3-bottom"), 2, 2, 3, ui::ThemeManager::instance().theme()).pixmap(32, 32).isNull());
    EXPECT_FALSE(ui::IconCatalog::layoutIcon(QStringLiteral("4-grid"), 2, 2, 4, ui::ThemeManager::instance().theme()).pixmap(32, 32).isNull());
}

// An icon says which destination it opens, so two destinations never draw the same thing.
TEST(IconsTest, DrawsEveryIconApartFromEveryOther) {
    const QVector<ui::IconName> names = ui::IconCatalog::allIconNames();

    QVector<QImage> drawings;

    for (const auto name : names) {
        const QImage drawing = ui::IconCatalog::icon(name, QColor(Qt::white)).pixmap(48, 48).toImage();
        ASSERT_FALSE(drawing.isNull());
        for (const auto& other : drawings) {
            EXPECT_NE(drawing, other);
        }
        drawings.append(drawing);
    }

    EXPECT_EQ(drawings.size(), names.size());
}

TEST(ComponentsTest, BreaksALabelBetweenItsWordsUntilOneWordHasNoBoundaryLeft) {
    QFont font = QApplication::font();
    font.setPointSizeF(std::max(8.0, font.pointSizeF() - 2.0));
    const QFontMetrics metrics(font);

    // Every word of the label fits, so the break belongs between them.
    const QString label = QStringLiteral("Editor de Código");
    const int fits = ui::Components::longestWordWidth(label, metrics) + 4;
    EXPECT_EQ(ui::Components::labelWrapping(label, metrics, fits), Qt::TextWordWrap);

    // A single word wider than the space it has left has no word boundary to break at.
    EXPECT_EQ(ui::Components::labelWrapping(QStringLiteral("Configurações"), metrics, 8), Qt::TextWrapAnywhere);
    EXPECT_EQ(ui::Components::longestWordWidth(QStringLiteral("Editor de Código"), metrics), ui::Components::longestWordWidth(QStringLiteral("Código"), metrics));
    EXPECT_EQ(ui::Components::longestWordWidth(QString{}, metrics), 0);
}

TEST(ModeBarTest, ExpandsForTranslatedWordsWrapsLabelsAndEmitsModeRequests) {
    ui::ModeBar bar;
    const int initialWidth = bar.sizeHint().width();
    bar.addMode(QStringLiteral("terminal/main"), ui::IconCatalog::icon(ui::IconName::Terminal, ui::ThemeManager::instance().theme()), QStringLiteral("Terminal"), plugins::NavigationPlacement::Primary);
    bar.addMode(QStringLiteral("workpane/settings"), ui::IconCatalog::icon(ui::IconName::Settings, ui::ThemeManager::instance().theme()), QStringLiteral("Configurações avançadas"), plugins::NavigationPlacement::Secondary);
    EXPECT_GT(bar.sizeHint().width(), initialWidth);

    // A word longer than the bar wraps inside it, because a language with long words must not widen the shell.
    bar.addMode(QStringLiteral("system-information/overview"), ui::IconCatalog::icon(ui::IconName::System, ui::ThemeManager::instance().theme()), QStringLiteral("Unwrappablylongnavigationword"), plugins::NavigationPlacement::Secondary);
    EXPECT_LE(bar.sizeHint().width(), ui::ThemeManager::instance().theme().metric(ui::ThemeMetric::ModeBarMaximumWidth));
    EXPECT_GE(bar.sizeHint().width(), ui::ThemeManager::instance().theme().metric(ui::ThemeMetric::ModeBarMinimumWidth));

    bar.resize(bar.sizeHint().width(), 400);
    bar.show();
    QApplication::processEvents();
    const auto buttons = bar.findChildren<QWidget*>();
    QWidget* interactiveButton = nullptr;

    for (auto* candidate : buttons) {
        if (candidate->toolTip() == QStringLiteral("Terminal")) {
            interactiveButton = candidate;
            break;
        }
    }

    ASSERT_NE(interactiveButton, nullptr);

    QSignalSpy requested(&bar, &ui::ModeBar::modeRequested);
    QTest::mouseClick(interactiveButton, Qt::LeftButton);
    ASSERT_EQ(requested.count(), 1);
    EXPECT_EQ(requested.first().first().toString(), QStringLiteral("terminal/main"));
}

TEST(SettingsViewTest, LetsTheDividerAndTheGridReachTheEdgeAndInsetsEverythingElse) {
    plugins::PluginManager manager;
    // clang-format off
    const ui::SettingsSectionFactory createSection = [](const QString&, const QString&, QWidget* parent) {
        const auto [page, layout] = ui::Components::settingsSectionPage(parent);
        auto* form = ui::Components::settingsForm();
        ui::Components::addSettingsRow(form, QStringLiteral("Caption"), new QLineEdit(page));
        layout->addLayout(form);
        layout->addWidget(ui::Components::dataGrid({QStringLiteral("Column")}, page));
        return page;
    };
    // clang-format on

    const plugins::SettingsSection first{QStringLiteral("first"), QStringLiteral("workpane.settings.title"), {QStringLiteral("workpane.settings.search")}};
    const plugins::SettingsSection second{QStringLiteral("second"), QStringLiteral("workpane.settings.title"), {QStringLiteral("workpane.settings.search")}};
    const plugins::SettingsGroup group{QStringLiteral("application"), QStringLiteral("workpane.settings.title"), {first, second}};

    ui::SettingsView view(manager, {{group, createSection}});
    view.resize(900, 700);
    view.show();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&view]() { return view.findChild<QTableWidget*>() != nullptr && view.findChild<QTableWidget*>()->width() > 0; }));
    // clang-format on

    auto* page = view.findChild<QScrollArea*>()->widget();
    ASSERT_NE(page, nullptr);
    const int inset = manager.theme().metric(ui::ThemeMetric::SettingsHorizontalPadding);
    EXPECT_GT(inset, 0);

    // The divider between two sections and the grid of a section reach both edges of the page.
    auto* divider = page->findChild<QWidget*>(QStringLiteral("sharedDivider"));
    ASSERT_NE(divider, nullptr);
    EXPECT_EQ(divider->mapTo(page, QPoint(0, 0)).x(), 0);
    EXPECT_EQ(divider->width(), page->width());

    auto* grid = page->findChild<QTableWidget*>();
    ASSERT_NE(grid, nullptr);
    EXPECT_EQ(grid->mapTo(page, QPoint(0, 0)).x(), 0);
    EXPECT_EQ(grid->width(), page->width());

    // The title and the caption of a row keep the inset the page gave up.
    auto* title = page->findChild<QLabel*>(QStringLiteral("settingsSectionTitle"));
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->contentsMargins().left(), inset);

    QLabel* caption = nullptr;

    for (auto* candidate : page->findChildren<QLabel*>()) {
        if (candidate->text() == QStringLiteral("Caption")) {
            caption = candidate;
        }
    }

    ASSERT_NE(caption, nullptr);
    EXPECT_EQ(caption->mapTo(page, QPoint(0, 0)).x(), inset);
}

TEST(SettingsViewTest, WritesEverySectionTitleInUpperCase) {
    auto* title = ui::Components::sectionTitleLabel(QStringLiteral("Model Connections"), nullptr);
    // A title that shares the case and the colour of every caption below it does not read as a title.
    EXPECT_EQ(title->text(), QStringLiteral("MODEL CONNECTIONS"));
    delete title;
}

TEST(DataGridTest, KeepsTheActionsOfASelectedRowReadableThroughEveryRebuild) {
    plugins::PluginManager manager;
    const ui::Theme& theme = manager.theme();
    QTableWidget* grid = ui::Components::dataGrid({QStringLiteral("Name"), QStringLiteral("Actions")}, nullptr);
    // clang-format off
    const auto fillRow = [grid, &theme]() {
        grid->setRowCount(1);
        grid->setItem(0, 0, new QTableWidgetItem(QStringLiteral("Preview")));
        auto* actions = new QWidget(grid);
        auto* layout = new QHBoxLayout(actions);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(ui::Components::rowActionButton(ui::IconName::Start, ui::ThemeColor::Accent, theme, QStringLiteral("Start"), actions));
        layout->addStretch(1);
        grid->setCellWidget(0, 1, actions);
    };
    const auto glyph = [grid]() { return grid->cellWidget(0, 1)->findChild<QToolButton*>()->icon().pixmap(32, 32).toImage(); };
    // clang-format on

    fillRow();
    grid->resize(400, 120);
    grid->show();

    // An action that is not selected keeps the colour its own role gives it.
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([grid]() { return grid->cellWidget(0, 1) != nullptr; }));
    // clang-format on
    EXPECT_EQ(glyph(), ui::IconCatalog::icon(ui::IconName::Start, theme.color(ui::ThemeColor::Accent)).pixmap(32, 32).toImage());

    // A selected row is painted in the accent, so its actions switch to the ink that reads on it.
    grid->selectRow(0);
    QCoreApplication::processEvents();
    EXPECT_EQ(glyph(), ui::IconCatalog::icon(ui::IconName::Start, theme.color(ui::ThemeColor::OnAccent)).pixmap(32, 32).toImage());

    // A rebuilt row carries new actions and keeps the selection it had, so they are readable without a second click.
    fillRow();
    grid->selectRow(0);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&glyph, &theme]() { return glyph() == ui::IconCatalog::icon(ui::IconName::Start, theme.color(ui::ThemeColor::OnAccent)).pixmap(32, 32).toImage(); }));
    // clang-format on

    delete grid;
}

TEST(ComponentsTest, OpensASurfaceOverTheApplicationAsAModalWindowOfThePlatform) {
    QDialog surface;
    ui::Components::showDialogWindow(&surface, QStringLiteral("Preview"));

    // A dialog modal to its parent is a sheet with none of the buttons a window carries, so it is modal to the application and it is named.
    EXPECT_EQ(surface.windowTitle(), QStringLiteral("Preview"));
    EXPECT_EQ(surface.windowModality(), Qt::ApplicationModal);
    EXPECT_TRUE(surface.isVisible());
    surface.close();
}

TEST(MarkdownViewTest, ReadsAChatMessageWithItsLineBreaksAndACompleteMarkdownDocument) {
    plugins::PluginManager manager;
    ui::MarkdownView view(manager.theme(), nullptr);

    // A chat message is written with the return key, so a single newline is a line break rather than a space.
    view.setChatMarkdown(QStringLiteral("first line\nsecond line"));
    EXPECT_TRUE(view.toPlainText().contains(QStringLiteral("first line\nsecond line")));

    // A fenced block keeps the lines it declares and never becomes prose.
    view.setChatMarkdown(QStringLiteral("before\n\n```cpp\nint a = 1;\nint b = 2;\n```\n\nafter"));
    const QString fenced = view.toPlainText();
    EXPECT_FALSE(fenced.contains(QStringLiteral("```")));
    EXPECT_TRUE(fenced.contains(QStringLiteral("int a = 1;")));
    EXPECT_TRUE(fenced.contains(QStringLiteral("int b = 2;")));

    // The document is complete Markdown, so a heading, a list, a quote, a link, a table and emphasis all arrive.
    const QString smiling = QString::fromUtf8("\xF0\x9F\x98\x80");
    view.setDocumentMarkdown(QStringLiteral("# Title\n\n- one\n- two\n\n> quoted\n\n**bold** and `code` and [link](https://example.com)\n\n| a | b |\n| --- | --- |\n| 1 | 2 |\n\n") + smiling);
    const QString rich = view.toPlainText();
    EXPECT_FALSE(rich.contains(QStringLiteral("**")));
    EXPECT_FALSE(rich.contains(QStringLiteral("| --- |")));
    EXPECT_TRUE(rich.contains(QStringLiteral("Title")));
    EXPECT_TRUE(rich.contains(QStringLiteral("quoted")));
    EXPECT_TRUE(rich.contains(QStringLiteral("bold")));
    EXPECT_TRUE(rich.contains(smiling));
    EXPECT_TRUE(view.toHtml().contains(QStringLiteral("https://example.com")));
    EXPECT_TRUE(view.toHtml().contains(QStringLiteral("<table")));

    // The Markdown reader marks code with a generic family no platform installs, so what a code run really carries is read back from the document.
    view.setChatMarkdown(QStringLiteral("prose `span` prose\n\n```\nint a = 1;\n```"));
    const QString monospace = manager.theme().font(ui::ThemeFont::Monospace).family();
    int codeRuns = 0;

    for (QTextBlock block = view.document()->begin(); block.isValid(); block = block.next()) {
        for (auto entry = block.begin(); entry != block.end(); ++entry) {
            const QTextFragment fragment = entry.fragment();

            if (!fragment.isValid()) {
                continue;
            }

            const QTextCharFormat format = fragment.charFormat();

            if (fragment.text().contains(QStringLiteral("span")) || fragment.text().contains(QStringLiteral("int a"))) {
                ++codeRuns;
                EXPECT_EQ(format.fontFamilies().toStringList(), QStringList{monospace}) << fragment.text().toStdString() << " does not carry the monospace family";
                EXPECT_TRUE(QFontDatabase::isFixedPitch(monospace)) << monospace.toStdString() << " is not a monospaced family";
            } else {
                EXPECT_FALSE(format.hasProperty(QTextFormat::FontFamilies)) << "prose is given a family of its own";
            }
        }
    }

    EXPECT_EQ(codeRuns, 2) << "the document does not carry a code span and a fenced block";

    // The reading size is the one it was given, because a content surface owns its own.
    view.setContentFontSize(20);
    EXPECT_EQ(view.document()->defaultFont().pointSize(), 20);
}

TEST(SettingsViewTest, GivesACaptionTheWidthItsOwnWordsNeed) {
    const QString shortCaption = QStringLiteral("Provider");
    const QString longCaption = QStringLiteral("Maximum requests at the same time");
    const QString unreadableCaption = QStringLiteral("A caption nobody would write because it carries a complete sentence of its own inside one settings row");

    auto* page = new QWidget();
    auto* pageLayout = new QVBoxLayout(page);
    auto* form = ui::Components::settingsForm();
    ui::Components::addSettingsRow(form, shortCaption, new QLineEdit(page));
    ui::Components::addSettingsRow(form, longCaption, new QLineEdit(page));
    ui::Components::addSettingsRow(form, unreadableCaption, new QLineEdit(page));
    pageLayout->addLayout(form);
    page->resize(1100, 400);
    page->show();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([page]() { return page->findChild<QLineEdit*>() != nullptr && page->findChild<QLineEdit*>()->width() > 0; }));
    // clang-format on

    QLabel* shortLabel = nullptr;
    QLabel* longLabel = nullptr;
    QLabel* unreadableLabel = nullptr;

    for (auto* candidate : page->findChildren<QLabel*>()) {
        if (candidate->text() == shortCaption) {
            shortLabel = candidate;
        }
        if (candidate->text() == longCaption) {
            longLabel = candidate;
        }
        if (candidate->text() == unreadableCaption) {
            unreadableLabel = candidate;
        }
    }

    ASSERT_NE(shortLabel, nullptr);
    ASSERT_NE(longLabel, nullptr);
    ASSERT_NE(unreadableLabel, nullptr);

    // A caption asks for exactly the width its own words occupy, measured the way the label renders them.
    const int cap = ui::ThemeManager::instance().theme().metric(ui::ThemeMetric::SettingsLabelMaximumWidth);
    const QLabel longReference(longCaption);
    EXPECT_LT(longReference.sizeHint().width(), cap);
    EXPECT_EQ(longLabel->minimumWidth(), longReference.sizeHint().width());

    // A caption longer than the readable bound stops there, so one row never takes the width of the page.
    const QLabel unreadableReference(unreadableCaption);
    EXPECT_GT(unreadableReference.sizeHint().width(), cap);
    EXPECT_EQ(unreadableLabel->minimumWidth(), cap);

    // The page has room for both, so only the one past the bound wraps.
    const int oneLine = shortLabel->heightForWidth(shortLabel->width());
    EXPECT_EQ(longLabel->heightForWidth(longLabel->width()), oneLine);
    EXPECT_GT(unreadableLabel->heightForWidth(unreadableLabel->width()), oneLine);

    delete page;
}

TEST(SettingsViewTest, CombinesCoreGroupsWithMultipleSectionLayouts) {
    plugins::PluginManager manager;
    int createdSections = 0;
    // clang-format off
    const ui::SettingsSectionFactory createSection = [&createdSections](const QString& groupId, const QString& sectionId, QWidget* parent) {
        ++createdSections;
        return new QLabel(groupId + QLatin1Char('/') + sectionId, parent);
    };
    // clang-format on

    const plugins::SettingsSection general{QStringLiteral("general"), QStringLiteral("workpane.settings.title"), {QStringLiteral("workpane.settings.search")}};
    const plugins::SettingsSection interfaceSection{QStringLiteral("interface"), QStringLiteral("workpane.settings.title"), {QStringLiteral("workpane.settings.search")}};
    const plugins::SettingsSection notificationsSection{QStringLiteral("notifications"), QStringLiteral("workpane.settings.title"), {QStringLiteral("workpane.settings.search")}};
    const plugins::SettingsGroup application{QStringLiteral("application"), QStringLiteral("workpane.settings.title"), {general}};
    const plugins::SettingsGroup experience{QStringLiteral("experience"), QStringLiteral("workpane.settings.title"), {interfaceSection, notificationsSection}};

    ui::SettingsView view(manager, {{application, createSection}, {experience, createSection}});
    auto* categories = view.findChild<QListWidget*>(QStringLiteral("settingsCategories"));
    ASSERT_NE(categories, nullptr);
    EXPECT_EQ(categories->count(), 2);
    EXPECT_EQ(createdSections, 3);

    auto* search = view.findChild<QLineEdit*>(QStringLiteral("settingsSearch"));
    ASSERT_NE(search, nullptr);
    auto* noResults = view.findChild<QLabel*>(QStringLiteral("emptyState"));
    ASSERT_NE(noResults, nullptr);
    auto* pages = view.findChild<QStackedWidget*>();
    ASSERT_NE(pages, nullptr);
    view.show();

    search->setText(QStringLiteral("a term that matches no settings category"));
    EXPECT_TRUE(noResults->isVisible());
    EXPECT_FALSE(pages->isVisible());

    search->clear();
    EXPECT_FALSE(noResults->isVisible());
    EXPECT_TRUE(pages->isVisible());

    // One divider separates every pair of sections, and a group with a single section carries none.
    ASSERT_EQ(pages->count(), 2);
    EXPECT_EQ(pages->widget(0)->findChildren<QWidget*>(QStringLiteral("sharedDivider")).size(), 0);
    EXPECT_EQ(pages->widget(1)->findChildren<QWidget*>(QStringLiteral("sharedDivider")).size(), 1);
}

TEST(SharedComponentsTest, SortsASelectableListAlphabeticallyAndKeepsWhatWasSelected) {
    ui::ComboBox box(ui::ThemeManager::instance().theme(), nullptr);
    box.addItem(QStringLiteral("Zulu"), QStringLiteral("z"));
    box.addItem(QStringLiteral("alpha"), QStringLiteral("a"));
    box.addItem(QStringLiteral("Mike"), QStringLiteral("m"));
    box.setCurrentIndex(box.findData(QStringLiteral("z")));

    // The order is the same on every machine, because it never depends on the locale the system happens to declare.
    ui::Components::sortComboBoxItems(&box);
    ASSERT_EQ(box.count(), 3);
    EXPECT_EQ(box.itemText(0), QStringLiteral("alpha"));
    EXPECT_EQ(box.itemText(1), QStringLiteral("Mike"));
    EXPECT_EQ(box.itemText(2), QStringLiteral("Zulu"));
    EXPECT_EQ(box.currentData().toString(), QStringLiteral("z"));

    // Two entries differing only in case keep a stable order instead of depending on which one was added first.
    ui::ComboBox cased(ui::ThemeManager::instance().theme(), nullptr);
    cased.addItem(QStringLiteral("beta"), QStringLiteral("lower"));
    cased.addItem(QStringLiteral("Beta"), QStringLiteral("upper"));
    ui::Components::sortComboBoxItems(&cased);
    EXPECT_EQ(cased.itemData(0).toString(), QStringLiteral("upper"));
    EXPECT_EQ(cased.itemData(1).toString(), QStringLiteral("lower"));

    // An editable list keeps the text that was typed, because it may name something the list does not carry.
    ui::ComboBox editable(ui::ThemeManager::instance().theme(), nullptr);
    editable.setEditable(true);
    editable.addItem(QStringLiteral("gamma"), QStringLiteral("g"));
    editable.addItem(QStringLiteral("beta"), QStringLiteral("b"));
    editable.setCurrentText(QStringLiteral("typed-by-hand"));

    ui::Components::sortComboBoxItems(&editable);
    EXPECT_EQ(editable.itemText(0), QStringLiteral("beta"));
    EXPECT_EQ(editable.currentText(), QStringLiteral("typed-by-hand"));

    // An empty list is left empty instead of selecting a row that is not there.
    ui::ComboBox empty(ui::ThemeManager::instance().theme(), nullptr);
    ui::Components::sortComboBoxItems(&empty);
    EXPECT_EQ(empty.count(), 0);
    EXPECT_EQ(empty.currentIndex(), -1);
}

TEST(ConfirmationDialogTest, ConfiguresSafeAndDestructiveActions) {
    ui::ConfirmationDialog safe(QStringLiteral("Title"), QStringLiteral("Message"), QStringLiteral("Cancel"), QStringLiteral("Continue"), false);
    EXPECT_TRUE(safe.isModal());
    EXPECT_GE(safe.minimumWidth(), 440);
    EXPECT_NE(safe.findChild<QLabel*>(QStringLiteral("confirmationTitle")), nullptr);
    auto* safeAction = safe.findChild<QPushButton*>(QStringLiteral("primaryButton"));
    ASSERT_NE(safeAction, nullptr);
    EXPECT_EQ(safeAction->text(), QStringLiteral("Continue"));

    ui::ConfirmationDialog destructive(QStringLiteral("Title"), QStringLiteral("Message"), QStringLiteral("Cancel"), QStringLiteral("Delete"), true);
    auto* destructiveAction = destructive.findChild<QPushButton*>(QStringLiteral("destructiveButton"));
    ASSERT_NE(destructiveAction, nullptr);
    EXPECT_EQ(destructiveAction->text(), QStringLiteral("Delete"));
}

TEST(PluginManagerIntegrationTest, RejectsInvalidInitializationContextAndUnavailableDatabase) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("workpane.sqlite3"));
    persistence::StateStore store(databasePath);
    persistence::DatabaseExecutor databaseExecutor(databasePath);
    ASSERT_TRUE(store.initialize().hasValue());
    plugins::PluginManager emptyPath;
    const auto emptyPathLoad = emptyPath.loadPlugins();
    ASSERT_TRUE(emptyPathLoad.hasValue()) << emptyPathLoad.error().code.toStdString() << " " << emptyPathLoad.error().detail.toStdString();
    EXPECT_EQ(emptyPath.initialize({}, store, databaseExecutor).error().code, QStringLiteral("plugin_data_path_invalid"));

    const QString unavailablePath = directory.filePath(QStringLiteral("unavailable.sqlite3"));
    persistence::StateStore unavailable(unavailablePath);
    persistence::DatabaseExecutor unavailableExecutor(unavailablePath);
    plugins::PluginManager unavailableDatabase;
    ASSERT_TRUE(unavailableDatabase.loadPlugins().hasValue());
    EXPECT_FALSE(unavailableDatabase.initialize(directory.path(), unavailable, unavailableExecutor).hasValue());
}

TEST(PluginManagerIntegrationTest, CancelsQueuedMessagesBeforeUnloadingPluginLibraries) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("workpane.sqlite3"));
    persistence::StateStore store(databasePath);
    persistence::DatabaseExecutor databaseExecutor(databasePath);
    ASSERT_TRUE(store.initialize().hasValue());
    plugins::PluginManager manager;
    ASSERT_TRUE(manager.loadPlugins().hasValue());
    ASSERT_TRUE(manager.initialize(directory.path(), store, databaseExecutor).hasValue());

    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    manager.unloadPlugins();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);

    EXPECT_TRUE(manager.navigationItems().isEmpty());
    EXPECT_TRUE(manager.settings().isEmpty());
    ASSERT_TRUE(manager.loadPlugins().hasValue());
    manager.unloadPlugins();
}

class CoreTestsHelper final {
  public:
    static plugins::TranslationCatalog validCatalog();
    static void executeSqliteStatements(const QString& path, const QStringList& statements);
    static QString createValidDatabase(const QTemporaryDir& directory, const QString& name);
    static void answerNextConfirmation(bool confirmed);
};

TEST(LocalizationServiceTest, ResolvesNormalizedLocaleBaseLanguageEnglishAndKey) {
    plugins::LocalizationService regional(QStringLiteral("PT_BR"));
    ASSERT_TRUE(regional.registerCatalog(QStringLiteral("sample"), CoreTestsHelper::validCatalog()).hasValue());
    EXPECT_EQ(regional.localeName(), QStringLiteral("pt-br"));
    EXPECT_EQ(regional.translate(QStringLiteral("sample.general.title")), QStringLiteral("Português brasileiro"));
    EXPECT_EQ(regional.translate(QStringLiteral("sample.general.secondary")), QStringLiteral("Secondary"));

    plugins::LocalizationService base(QStringLiteral("pt-PT"));
    ASSERT_TRUE(base.registerCatalog(QStringLiteral("sample"), CoreTestsHelper::validCatalog()).hasValue());
    EXPECT_EQ(base.translate(QStringLiteral("sample.general.title")), QStringLiteral("Português"));

    plugins::LocalizationService unknown(QStringLiteral("de-DE"));
    ASSERT_TRUE(unknown.registerCatalog(QStringLiteral("sample"), CoreTestsHelper::validCatalog()).hasValue());
    EXPECT_EQ(unknown.translate(QStringLiteral("sample.general.title")), QStringLiteral("English"));
    EXPECT_EQ(unknown.translate(QStringLiteral("sample.general.missing")), QStringLiteral("sample.general.missing"));
    EXPECT_EQ(unknown.translate(QStringLiteral("missing.general.title")), QStringLiteral("missing.general.title"));
    EXPECT_EQ(unknown.translate(QStringLiteral("invalid")), QStringLiteral("invalid"));

    ASSERT_TRUE(unknown.setLocale(QStringLiteral("PT_BR")).hasValue());
    EXPECT_EQ(unknown.localeName(), QStringLiteral("pt-br"));
    EXPECT_EQ(unknown.translate(QStringLiteral("sample.general.title")), QStringLiteral("Português brasileiro"));
    EXPECT_EQ(unknown.setLocale(QStringLiteral("invalid locale")).error().code, QStringLiteral("application_locale_invalid"));
    EXPECT_EQ(unknown.localeName(), QStringLiteral("pt-br"));
}
TEST(LocalizationServiceTest, RejectsInvalidCatalogs) {
    plugins::LocalizationService service(QStringLiteral("en"));
    EXPECT_EQ(service.registerCatalog(QStringLiteral("Invalid"), CoreTestsHelper::validCatalog()).error().code, QStringLiteral("plugin_translation_catalog_invalid"));

    const plugins::TranslationCatalog missingEnglish{{QStringLiteral("pt"), {{QStringLiteral("sample.general.title"), QStringLiteral("Português")}}}};
    EXPECT_EQ(service.registerCatalog(QStringLiteral("sample"), missingEnglish).error().code, QStringLiteral("plugin_translation_english_missing"));

    const plugins::TranslationCatalog emptyEnglish{{QStringLiteral("en"), {}}};
    EXPECT_EQ(service.registerCatalog(QStringLiteral("sample"), emptyEnglish).error().code, QStringLiteral("plugin_translation_english_missing"));

    auto invalidLocale = CoreTestsHelper::validCatalog();
    invalidLocale.insert(QStringLiteral("pt_BR"), invalidLocale.value(QStringLiteral("pt")));
    EXPECT_EQ(service.registerCatalog(QStringLiteral("sample"), invalidLocale).error().code, QStringLiteral("plugin_translation_locale_invalid"));

    auto foreignKey = CoreTestsHelper::validCatalog();
    foreignKey[QStringLiteral("en")].insert(QStringLiteral("other.general.title"), QStringLiteral("Foreign"));
    EXPECT_EQ(service.registerCatalog(QStringLiteral("sample"), foreignKey).error().code, QStringLiteral("plugin_translation_entry_invalid"));

    auto emptyValue = CoreTestsHelper::validCatalog();
    emptyValue[QStringLiteral("en")][QStringLiteral("sample.general.title")].clear();
    EXPECT_EQ(service.registerCatalog(QStringLiteral("sample"), emptyValue).error().code, QStringLiteral("plugin_translation_entry_invalid"));

    auto untranslatedKey = CoreTestsHelper::validCatalog();
    untranslatedKey[QStringLiteral("pt")].insert(QStringLiteral("sample.general.extra"), QStringLiteral("Extra"));
    EXPECT_EQ(service.registerCatalog(QStringLiteral("sample"), untranslatedKey).error().code, QStringLiteral("plugin_translation_entry_invalid"));

    ASSERT_TRUE(service.registerCatalog(QStringLiteral("sample"), CoreTestsHelper::validCatalog()).hasValue());
    EXPECT_EQ(service.registerCatalog(QStringLiteral("sample"), CoreTestsHelper::validCatalog()).error().code, QStringLiteral("plugin_translation_catalog_invalid"));
}
TEST(IconsTest, PaintsEveryIconInTheColourItIsAskedFor) {
    const QColor asked(214, 92, 33);

    // An icon is stroked in the colour its caller passes, which is how every one of them follows the active theme.
    for (const auto name : ui::IconCatalog::allIconNames()) {
        const QImage image = ui::IconCatalog::icon(name, asked).pixmap(48, 48).toImage();
        ASSERT_FALSE(image.isNull()) << static_cast<int>(name);
        int opaque = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor painted = image.pixelColor(x, y);
                if (painted.alpha() < 250) {
                    continue;
                }
                ++opaque;
                // A stroke is antialiased even where it is opaque, so a channel is allowed to round by one.
                EXPECT_NEAR(painted.red(), asked.red(), 2) << static_cast<int>(name);
                EXPECT_NEAR(painted.green(), asked.green(), 2) << static_cast<int>(name);
                EXPECT_NEAR(painted.blue(), asked.blue(), 2) << static_cast<int>(name);
            }
        }
        EXPECT_GT(opaque, 0) << static_cast<int>(name);
    }

    // The same shape in another colour covers the same pixels, so the colour is the only thing a theme changes.
    const QImage first = ui::IconCatalog::icon(ui::IconName::Tool, asked).pixmap(48, 48).toImage();
    const QImage second = ui::IconCatalog::icon(ui::IconName::Tool, QColor(33, 92, 214)).pixmap(48, 48).toImage();
    ASSERT_EQ(first.size(), second.size());

    for (int y = 0; y < first.height(); ++y) {
        for (int x = 0; x < first.width(); ++x) {
            EXPECT_EQ(first.pixelColor(x, y).alpha(), second.pixelColor(x, y).alpha()) << x << "," << y;
        }
    }
}

TEST(CalendarPopupTest, OpensFromTheIndicatorAndPaintsEveryDayOfTheMonthItIsReading) {
    const ui::Theme& theme = ui::ThemeManager::instance().theme();
    QWidget host;
    host.resize(600, 400);
    host.setStyleSheet(ui::ApplicationStyleSheet::applicationStyleSheet(theme));
    host.show();

    auto* field = new ui::DateTimeField(theme, &host);
    field->setGeometry(20, 20, 300, 30);
    field->setDateTime(QDateTime(QDate(2026, 8, 21), QTime(14, 11)));
    field->show();
    QApplication::processEvents();

    // The line edit of a spin box covers the whole field, so the press on the indicator lands on it and not on the field.
    auto* editor = field->findChild<QLineEdit*>();
    ASSERT_NE(editor, nullptr);
    const QPoint indicator(field->width() - theme.metric(ui::ThemeMetric::ComboIndicatorWidth) / 2, field->height() / 2);
    ASSERT_TRUE(editor->geometry().contains(indicator)) << editor->geometry().width();
    QTest::mouseClick(editor, Qt::LeftButton, Qt::NoModifier, editor->mapFrom(field, indicator));
    QApplication::processEvents();

    auto* calendar = field->findChild<ui::CalendarPopup*>();
    ASSERT_NE(calendar, nullptr);
    EXPECT_TRUE(calendar->isVisible());

    // Every cell carries the day it stands for, so nothing in the grid is elided into dots.
    const auto cells = calendar->findChildren<QToolButton*>(QStringLiteral("calendarDay"));
    ASSERT_EQ(cells.size(), 42);
    int painted = 0;

    for (auto* cell : cells) {
        const QDate date = cell->property("date").toDate();
        ASSERT_TRUE(date.isValid());
        EXPECT_EQ(cell->text(), QString::number(date.day()));
        EXPECT_GE(cell->width(), cell->fontMetrics().horizontalAdvance(QStringLiteral("30"))) << cell->text().toStdString();
        painted += date.month() == 8 ? 1 : 0;
    }

    EXPECT_EQ(painted, 31);

    // clang-format off
    const auto chosen = std::find_if(cells.cbegin(), cells.cend(), [](const QToolButton* cell) { return cell->property("chosen").toBool(); });
    // clang-format on
    ASSERT_NE(chosen, cells.cend());
    EXPECT_EQ((*chosen)->property("date").toDate(), QDate(2026, 8, 21));

    // Choosing a day keeps the time already written and closes the calendar.
    // clang-format off
    const auto september = std::find_if(cells.cbegin(), cells.cend(), [](const QToolButton* cell) { return cell->property("date").toDate() == QDate(2026, 9, 3); });
    // clang-format on
    ASSERT_NE(september, cells.cend());
    QTest::mouseClick(*september, Qt::LeftButton);
    QApplication::processEvents();
    EXPECT_EQ(field->dateTime(), QDateTime(QDate(2026, 9, 3), QTime(14, 11)));
    EXPECT_FALSE(calendar->isVisible());

    // A press on the text is still a press on the text, so the field is written into as any other.
    QTest::mouseClick(editor, Qt::LeftButton, Qt::NoModifier, editor->mapFrom(field, QPoint(20, field->height() / 2)));
    QApplication::processEvents();
    EXPECT_FALSE(calendar->isVisible());
}

// A start that fails must leave nothing behind, because the reader retries it and the process lock is what keeps two writers apart.
TEST(ApplicationTest, TearsDownAfterAFailedStartAndKeepsTheLockOfTheOneThatHasIt) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());

    app::Application running(directory.path(), nullptr);
    ASSERT_TRUE(running.initialize().hasValue());

    {
        // A second instance on the same data must be refused rather than take the lock away from the first.
        app::Application second(directory.path(), nullptr);
        const auto refused = second.initialize();
        ASSERT_FALSE(refused.hasValue());
        EXPECT_EQ(refused.error().code, QStringLiteral("application_already_running"));
    }

    // The one that has the lock still has it, so the refusal took nothing from it.
    app::Application third(directory.path(), nullptr);
    EXPECT_EQ(third.initialize().error().code, QStringLiteral("application_already_running"));

    // A data directory that cannot be created is refused by name and destroyed without having built anything.
    const QString blocked = directory.filePath(QStringLiteral("occupied"));
    QFile occupant(blocked);
    ASSERT_TRUE(occupant.open(QIODevice::WriteOnly));
    occupant.write(QByteArrayLiteral("not a directory"));
    occupant.close();
    {
        app::Application unusable(QDir(blocked).filePath(QStringLiteral("data")), nullptr);
        const auto refused = unusable.initialize();
        ASSERT_FALSE(refused.hasValue());
        EXPECT_EQ(refused.error().code, QStringLiteral("application_data_directory_failed"));
    }

    running.shutdown();

    // Once it has gone the next instance opens, which is what makes the refusal a lock rather than a wall.
    app::Application after(directory.path(), nullptr);
    EXPECT_TRUE(after.initialize().hasValue());
    after.shutdown();
}

TEST(ApplicationTest, EndsQuietlyWhenTheWindowIsClosedWhileItIsStillLoading) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    app::Application application(directory.path(), nullptr);
    ASSERT_TRUE(application.initialize().hasValue());
    ASSERT_TRUE(application.loadInterface().hasValue());
    QApplication::processEvents();

    // The reader closed the window before anything it presents existed, so what was queued has nothing left to start.
    application.shutdown();
    const auto startup = application.completeStartup();
    EXPECT_TRUE(startup.hasValue()) << startup.error().code.toStdString();
}

TEST(MainWindowTest, OpensOnItsLoadingPageAndKeepsNothingWhenItIsClosedThere) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("workpane.sqlite3"));
    persistence::StateStore store(databasePath);
    persistence::DatabaseExecutor databaseExecutor(databasePath);
    app::ApplicationSettingsStore settings(store, databaseExecutor);
    ASSERT_TRUE(settings.initialize().hasValue());
    plugins::PluginManager manager;
    ASSERT_TRUE(manager.setLocale(QStringLiteral("en")).hasValue());

    ui::MainWindow window(manager, settings);
    window.show();
    QApplication::processEvents();

    // The window is on screen before anything it presents exists, so the reader sees the product instead of waiting for it.
    auto* loading = window.findChild<QWidget*>(QStringLiteral("startupLoading"));
    ASSERT_NE(loading, nullptr);
    EXPECT_NE(window.findChild<QLabel*>(QStringLiteral("startupCaption")), nullptr);
    EXPECT_NE(window.findChild<ui::BusyIndicator*>(QStringLiteral("startupIndicator")), nullptr);
    EXPECT_FALSE(window.ready());

    // Closing it there keeps nothing and asks nothing, because nothing was loaded to be kept.
    window.resize(900, 600);
    QApplication::processEvents();
    window.close();
    QApplication::processEvents();
    EXPECT_TRUE(settings.windowGeometry().isEmpty());
}

TEST(StateStoreTest, ReplacesACorruptedCoreDatabaseInsteadOfRefusingToOpen) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());

    const QString schemaPath = CoreTestsHelper::createValidDatabase(directory, QStringLiteral("schema.sqlite3"));
    CoreTestsHelper::executeSqliteStatements(schemaPath, {QStringLiteral("ALTER TABLE core_state RENAME TO core_state_old"), QStringLiteral("CREATE TABLE core_state(singleton INTEGER PRIMARY KEY, clean_shutdown INTEGER NOT NULL) STRICT"), QStringLiteral("INSERT INTO core_state SELECT * FROM core_state_old"), QStringLiteral("DROP TABLE core_state_old")});
    // Every shape the shell cannot read is set aside and replaced, so the application opens whatever the file held.
    persistence::StateStore invalidSchema(schemaPath);
    ASSERT_TRUE(invalidSchema.initialize().hasValue());
    EXPECT_FALSE(invalidSchema.replacedDatabasePath().isEmpty());
    EXPECT_TRUE(invalidSchema.wasCleanShutdown().hasValue());

    const QString statePath = CoreTestsHelper::createValidDatabase(directory, QStringLiteral("state.sqlite3"));
    CoreTestsHelper::executeSqliteStatements(statePath, {QStringLiteral("DELETE FROM core_state")});
    persistence::StateStore invalidState(statePath);
    ASSERT_TRUE(invalidState.initialize().hasValue());
    EXPECT_FALSE(invalidState.replacedDatabasePath().isEmpty());

    const QString settingsPath = CoreTestsHelper::createValidDatabase(directory, QStringLiteral("settings.sqlite3"));
    CoreTestsHelper::executeSqliteStatements(settingsPath, {QStringLiteral("INSERT INTO plugin_settings(owner_id, document) VALUES('', '{}')")});
    persistence::StateStore invalidSettings(settingsPath);
    ASSERT_TRUE(invalidSettings.initialize().hasValue());
    EXPECT_FALSE(invalidSettings.replacedDatabasePath().isEmpty());

    const QString pluginPath = CoreTestsHelper::createValidDatabase(directory, QStringLiteral("plugin.sqlite3"));
    CoreTestsHelper::executeSqliteStatements(pluginPath, {QStringLiteral("PRAGMA ignore_check_constraints = ON"), QStringLiteral("INSERT INTO plugin_schema_versions(plugin_id, version) VALUES('Invalid', -1)")});
    persistence::StateStore invalidPlugin(pluginPath);
    ASSERT_TRUE(invalidPlugin.initialize().hasValue());
    EXPECT_FALSE(invalidPlugin.replacedDatabasePath().isEmpty());
}
TEST(ConfigurationTransferTest, RejectsInvalidSourcesDestinationsAndPendingImports) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString invalidPath = directory.filePath(QStringLiteral("invalid.sqlite3"));
    QFile invalid(invalidPath);
    ASSERT_TRUE(invalid.open(QIODevice::WriteOnly));
    ASSERT_EQ(invalid.write("not a database"), 14);
    invalid.close();

    const auto invalidImport = test::TestFutures::awaitFuture(persistence::ConfigurationTransfer::stageImport(invalidPath, directory.filePath(QStringLiteral("pending.sqlite3")), {}));
    EXPECT_EQ(invalidImport.error().code, QStringLiteral("configuration_database_invalid"));
    const auto invalidExport = test::TestFutures::awaitFuture(persistence::ConfigurationTransfer::exportDatabase(invalidPath, directory.filePath(QStringLiteral("missing/export.sqlite3"))));
    EXPECT_EQ(invalidExport.error().code, QStringLiteral("configuration_destination_invalid"));

    ASSERT_TRUE(QFile::copy(invalidPath, directory.filePath(QStringLiteral("pending.sqlite3"))));
    EXPECT_FALSE(persistence::ConfigurationTransfer::beginPendingImport(directory.filePath(QStringLiteral("current.sqlite3")), directory.filePath(QStringLiteral("pending.sqlite3")), directory.filePath(QStringLiteral("backup.sqlite3")), {}).hasValue());
    EXPECT_TRUE(QFileInfo::exists(directory.filePath(QStringLiteral("pending.sqlite3"))));

    const QString pendingDirectory = directory.filePath(QStringLiteral("pending-directory"));
    const QString preservedBackup = directory.filePath(QStringLiteral("preserved-backup.sqlite3"));
    ASSERT_TRUE(QDir().mkpath(pendingDirectory));
    ASSERT_TRUE(QFile::copy(invalidPath, preservedBackup));
    EXPECT_EQ(persistence::ConfigurationTransfer::finalizePendingImport(pendingDirectory, preservedBackup).error().code, QStringLiteral("configuration_pending_remove_failed"));
    EXPECT_TRUE(QFileInfo::exists(preservedBackup));
    EXPECT_EQ(persistence::ConfigurationTransfer::finalizePendingImport(directory.filePath(QStringLiteral("missing-pending.sqlite3")), directory.filePath(QStringLiteral("missing-backup.sqlite3"))).error().code, QStringLiteral("configuration_backup_remove_failed"));

    const QString rollbackCurrent = directory.filePath(QStringLiteral("rollback-current.sqlite3"));
    const QString rollbackBackup = CoreTestsHelper::createValidDatabase(directory, QStringLiteral("rollback-backup.sqlite3"));
    const QString rollbackPending = directory.filePath(QStringLiteral("rollback-pending"));
    ASSERT_TRUE(QDir().mkpath(rollbackPending));
    EXPECT_EQ(persistence::ConfigurationTransfer::rollbackPendingImport(rollbackCurrent, rollbackPending, rollbackBackup).error().code, QStringLiteral("configuration_pending_remove_failed"));
    EXPECT_TRUE(QFileInfo::exists(rollbackBackup));
}
TEST(ConfigurationTransferTest, RejectsReadableDatabasesWithCorruptedCoreState) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());

    const QString schemaPath = CoreTestsHelper::createValidDatabase(directory, QStringLiteral("schema.sqlite3"));
    CoreTestsHelper::executeSqliteStatements(schemaPath, {QStringLiteral("ALTER TABLE core_state RENAME TO core_state_old"), QStringLiteral("CREATE TABLE core_state(singleton INTEGER PRIMARY KEY, clean_shutdown INTEGER NOT NULL) STRICT"), QStringLiteral("INSERT INTO core_state SELECT * FROM core_state_old"), QStringLiteral("DROP TABLE core_state_old")});
    const auto invalidSchema = test::TestFutures::awaitFuture(persistence::ConfigurationTransfer::stageImport(schemaPath, directory.filePath(QStringLiteral("schema-pending.sqlite3")), {}));
    EXPECT_EQ(invalidSchema.error().code, QStringLiteral("configuration_database_invalid"));

    const QString statePath = CoreTestsHelper::createValidDatabase(directory, QStringLiteral("state.sqlite3"));
    CoreTestsHelper::executeSqliteStatements(statePath, {QStringLiteral("DELETE FROM core_state")});
    const auto invalidState = test::TestFutures::awaitFuture(persistence::ConfigurationTransfer::stageImport(statePath, directory.filePath(QStringLiteral("preferences-pending.sqlite3")), {}));
    EXPECT_EQ(invalidState.error().code, QStringLiteral("configuration_database_invalid"));

    const QString versionsPath = CoreTestsHelper::createValidDatabase(directory, QStringLiteral("versions.sqlite3"));
    CoreTestsHelper::executeSqliteStatements(versionsPath, {QStringLiteral("PRAGMA ignore_check_constraints = ON"), QStringLiteral("INSERT INTO plugin_schema_versions(plugin_id, version) VALUES('Invalid', -1)")});
    const auto invalidVersions = test::TestFutures::awaitFuture(persistence::ConfigurationTransfer::stageImport(versionsPath, directory.filePath(QStringLiteral("versions-pending.sqlite3")), {}));
    EXPECT_EQ(invalidVersions.error().code, QStringLiteral("configuration_database_invalid"));
}
// A reader who wants the application to keep its data somewhere of their own says where, which is what makes a run against isolated state possible at all.
TEST(ApplicationTest, KeepsItsDataWhereThePlatformSaysUnlessTheReaderNamesADirectory) {
    const QString platform = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const auto byDefault = app::Application::resolveDataPath({QStringLiteral("Workpane")});
    ASSERT_TRUE(byDefault.hasValue());
    EXPECT_EQ(byDefault.value(), platform);

    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString chosen = QDir(directory.path()).filePath(QStringLiteral("profile"));
    const auto named = app::Application::resolveDataPath({QStringLiteral("Workpane"), QStringLiteral("--data-dir"), chosen});
    ASSERT_TRUE(named.hasValue()) << named.error().message.toStdString();
    EXPECT_EQ(named.value(), QDir(chosen).absolutePath());
    EXPECT_TRUE(QFileInfo(chosen).isDir());

    // A directory that is not absolute, one that is empty and an argument nobody declares are refused rather than answered with the platform location.
    const auto relative = app::Application::resolveDataPath({QStringLiteral("Workpane"), QStringLiteral("--data-dir"), QStringLiteral("profile")});
    ASSERT_FALSE(relative.hasValue());
    EXPECT_EQ(relative.error().code, QStringLiteral("application_data_path_invalid"));

    const auto empty = app::Application::resolveDataPath({QStringLiteral("Workpane"), QStringLiteral("--data-dir"), QString{}});
    ASSERT_FALSE(empty.hasValue());
    EXPECT_EQ(empty.error().code, QStringLiteral("application_data_path_invalid"));

    const auto unknown = app::Application::resolveDataPath({QStringLiteral("Workpane"), QStringLiteral("--nothing-declares-this")});
    ASSERT_FALSE(unknown.hasValue());
    EXPECT_EQ(unknown.error().code, QStringLiteral("application_arguments_invalid"));
}

TEST(ApplicationTest, ValidatesStartupLockInterfaceLifecycleAndRecovery) {
    app::Application invalid(QString{}, nullptr);
    EXPECT_EQ(invalid.loadInterface().error().code, QStringLiteral("application_not_initialized"));
    EXPECT_EQ(invalid.initialize().error().code, QStringLiteral("application_data_directory_failed"));

    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QString persistedLanguage;
    const QString persistedTheme = QStringLiteral("blue");
    {
        app::Application application(directory.path(), nullptr);
        EXPECT_EQ(application.loadInterface().error().code, QStringLiteral("application_not_initialized"));
        const auto initialization = application.initialize();
        ASSERT_TRUE(initialization.hasValue()) << initialization.error().code.toStdString() << " " << initialization.error().detail.toStdString();
        EXPECT_EQ(application.initialize().error().code, QStringLiteral("application_already_initialized"));

        app::Application concurrent(directory.path(), nullptr);
        EXPECT_EQ(concurrent.initialize().error().code, QStringLiteral("application_already_running"));

        // The window opens saying that it is loading, and only builds its interface once everything it presents is ready.
        ASSERT_TRUE(application.loadInterface().hasValue());
        EXPECT_EQ(application.loadInterface().error().code, QStringLiteral("application_interface_loaded"));
        QApplication::processEvents();
        QWidget* mainWindow = nullptr;

        for (auto* topLevel : QApplication::topLevelWidgets()) {
            mainWindow = topLevel->objectName() == QStringLiteral("mainWindow") ? topLevel : mainWindow;
        }

        ASSERT_NE(mainWindow, nullptr);
        EXPECT_NE(mainWindow->findChild<QWidget*>(QStringLiteral("startupLoading")), nullptr);
        EXPECT_EQ(mainWindow->findChild<QWidget*>(QStringLiteral("workpane/settings")), nullptr);

        const auto startup = application.completeStartup();
        ASSERT_TRUE(startup.hasValue()) << startup.error().code.toStdString() << " " << startup.error().detail.toStdString();
        QApplication::processEvents();
        EXPECT_EQ(mainWindow->findChild<QWidget*>(QStringLiteral("startupLoading")), nullptr);

        auto* settingsModeBefore = mainWindow->findChild<QWidget*>(QStringLiteral("workpane/settings"));
        ASSERT_NE(settingsModeBefore, nullptr);
        QTest::mouseClick(settingsModeBefore, Qt::LeftButton);
        QApplication::processEvents();
        auto* settingsViewBefore = mainWindow->findChild<ui::SettingsView*>();
        ASSERT_NE(settingsViewBefore, nullptr);
        EXPECT_TRUE(settingsViewBefore->isVisible());

        auto* language = mainWindow->findChild<QComboBox*>(QStringLiteral("applicationLanguage"));
        ASSERT_NE(language, nullptr);
        EXPECT_EQ(language->count(), domain::ApplicationLanguages::supportedApplicationLanguages().size());
        persistedLanguage = language->currentData().toString() == QStringLiteral("en") ? QStringLiteral("pt") : QStringLiteral("en");
        const int selectedIndex = language->findData(persistedLanguage);
        ASSERT_GE(selectedIndex, 0);
        language->setCurrentIndex(selectedIndex);
        QApplication::processEvents();
        auto* translatedLanguage = mainWindow->findChild<QComboBox*>(QStringLiteral("applicationLanguage"));
        ASSERT_NE(translatedLanguage, nullptr);
        EXPECT_EQ(translatedLanguage->currentData().toString(), persistedLanguage);
        auto* translatedSettingsView = mainWindow->findChild<ui::SettingsView*>();
        ASSERT_NE(translatedSettingsView, nullptr);
        EXPECT_TRUE(translatedSettingsView->isVisible());
        auto* settingsMode = mainWindow->findChild<QWidget*>(QStringLiteral("workpane/settings"));
        ASSERT_NE(settingsMode, nullptr);
        EXPECT_EQ(settingsMode->toolTip(), persistedLanguage == QStringLiteral("en") ? QStringLiteral("Settings") : QStringLiteral("Configurações"));

        auto* theme = mainWindow->findChild<QComboBox*>(QStringLiteral("applicationTheme"));
        ASSERT_NE(theme, nullptr);
        EXPECT_EQ(theme->count(), 3);
        const int themeIndex = theme->findData(persistedTheme);
        ASSERT_GE(themeIndex, 0);
        theme->setCurrentIndex(themeIndex);
        QApplication::processEvents();
        auto* selectedTheme = mainWindow->findChild<QComboBox*>(QStringLiteral("applicationTheme"));
        ASSERT_NE(selectedTheme, nullptr);
        EXPECT_EQ(selectedTheme->currentData().toString(), persistedTheme);
        EXPECT_EQ(ui::ThemeManager::instance().theme().id(), persistedTheme);
        EXPECT_TRUE(mainWindow->styleSheet().contains(ui::ThemeManager::instance().theme().color(ui::ThemeColor::Accent).name()));

        CoreTestsHelper::answerNextConfirmation(false);
        QEvent quitEvent(QEvent::Quit);
        EXPECT_TRUE(QCoreApplication::sendEvent(qApp, &quitEvent));
        EXPECT_TRUE(mainWindow->isVisible());

        application.shutdown();
        application.shutdown();
        EXPECT_EQ(application.initialize().error().code, QStringLiteral("application_shutdown_complete"));
    }

    const QString statePath = directory.filePath(QStringLiteral("workpane.sqlite3"));
    persistence::StateStore state(statePath);
    ASSERT_TRUE(state.initialize().hasValue());
    ASSERT_TRUE(state.wasCleanShutdown().hasValue());
    EXPECT_TRUE(state.wasCleanShutdown().value());
    const QJsonObject document = state.settings(QStringLiteral("workpane"));
    EXPECT_EQ(document.value(QStringLiteral("language")).toString(), persistedLanguage);
    EXPECT_EQ(document.value(QStringLiteral("themeId")).toString(), persistedTheme);
    ASSERT_TRUE(state.markShutdown(false).hasValue());

    app::Application recovered(directory.path(), nullptr);
    ASSERT_TRUE(recovered.initialize().hasValue());
    QApplication::processEvents();
    recovered.shutdown();
}
TEST(PluginManagerIntegrationTest, DiscoversInitializesAndBuildsThePluginDrivenInterface) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("workpane.sqlite3"));
    persistence::StateStore store(databasePath);
    persistence::DatabaseExecutor databaseExecutor(databasePath);
    app::ApplicationSettingsStore settings(store, databaseExecutor);
    ASSERT_TRUE(settings.initialize().hasValue());
    plugins::PluginManager manager;
    EXPECT_EQ(manager.setLocale(QStringLiteral("de")).error().code, QStringLiteral("application_language_invalid"));
    ASSERT_TRUE(manager.setLocale(settings.language()).hasValue());
    const auto loadResult = manager.loadPlugins();
    ASSERT_TRUE(loadResult.hasValue()) << loadResult.error().code.toStdString() << " " << loadResult.error().detail.toStdString();
    EXPECT_EQ(manager.loadPlugins().error().code, QStringLiteral("plugin_already_loaded"));
    ASSERT_TRUE(manager.initialize(directory.path(), store, databaseExecutor).hasValue());
    EXPECT_EQ(manager.initialize(directory.path(), store, databaseExecutor).error().code, QStringLiteral("plugin_already_initialized"));
    EXPECT_EQ(store.pluginSchemaVersion(QStringLiteral("logs")).value(), 1);
    EXPECT_EQ(store.pluginSchemaVersion(QStringLiteral("ai")).value(), 2);
    EXPECT_EQ(store.pluginSchemaVersion(QStringLiteral("terminal")).value(), 1);
    EXPECT_EQ(store.pluginSchemaVersion(QStringLiteral("web-server")).value(), 1);
    EXPECT_EQ(store.pluginSchemaVersion(QStringLiteral("browser")).value(), 1);
    EXPECT_EQ(store.pluginSchemaVersion(QStringLiteral("code-editor")).value(), 1);
    QApplication::processEvents();

    const auto navigation = manager.navigationItems();
    ASSERT_EQ(navigation.size(), 8);
    QSet<QString> navigationPlugins;

    for (const auto& contribution : navigation) {
        navigationPlugins.insert(contribution.pluginId);
    }

    EXPECT_EQ(navigationPlugins, QSet<QString>({QStringLiteral("browser"), QStringLiteral("code-editor"), QStringLiteral("donate"), QStringLiteral("logs"), QStringLiteral("ai"), QStringLiteral("system-information"), QStringLiteral("terminal"), QStringLiteral("web-server")}));
    EXPECT_EQ(navigation.at(0).pluginId, QStringLiteral("ai"));
    EXPECT_EQ(navigation.at(0).item.id, QStringLiteral("tasks"));
    EXPECT_EQ(navigation.constLast().pluginId, QStringLiteral("donate"));
    EXPECT_EQ(navigation.constLast().item.order, plugins::NavigationOrder::Support);

    // The bar reads in the order the destinations declare rather than in the order the filesystem listed their libraries.
    QVector<plugins::NavigationOrder> declared;

    for (const auto& contribution : navigation) {
        declared.append(contribution.item.order);
    }

    QVector<plugins::NavigationOrder> ascending = declared;
    std::sort(ascending.begin(), ascending.end());
    EXPECT_EQ(declared, ascending);
    EXPECT_EQ(navigation.first().item.order, plugins::NavigationOrder::Board);
    EXPECT_EQ(navigation.at(1).item.order, plugins::NavigationOrder::Terminal);
    const auto pluginSettings = manager.settings();
    ASSERT_EQ(pluginSettings.size(), 10);

    for (const auto& pluginSettingsEntry : pluginSettings) {
        if (pluginSettingsEntry.group.sections.size() == 1) {
            EXPECT_EQ(pluginSettingsEntry.group.sections.first().id, QStringLiteral("general"));
        }
        for (const auto& section : pluginSettingsEntry.group.sections) {
            const std::unique_ptr<QWidget> built(manager.createSettingsSection(pluginSettingsEntry.pluginId, pluginSettingsEntry.group.id, section.id, nullptr));
            ASSERT_NE(built, nullptr) << pluginSettingsEntry.group.id.toStdString() << '/' << section.id.toStdString();

            // Every numeric field steps through the shared stepper, so no surface of any plugin shows the stacked native arrows.
            for (const auto* box : built->findChildren<QAbstractSpinBox*>()) {
                EXPECT_EQ(box->buttonSymbols(), QAbstractSpinBox::NoButtons) << pluginSettingsEntry.group.id.toStdString() << '/' << section.id.toStdString();
            }
        }
    }

    EXPECT_EQ(manager.pluginTitle(QStringLiteral("missing")), QStringLiteral("missing"));
    const QHash<QString, int> expectedSchemaVersions{{QStringLiteral("logs"), 1}, {QStringLiteral("ai"), 2}, {QStringLiteral("terminal"), 1}, {QStringLiteral("web-server"), 1}, {QStringLiteral("browser"), 1}, {QStringLiteral("code-editor"), 1}};
    EXPECT_EQ(manager.databaseSchemaVersions(), expectedSchemaVersions);
    EXPECT_EQ(manager.translate(QStringLiteral("missing.general.key")), QStringLiteral("missing.general.key"));
    EXPECT_FALSE(manager.styleSheet().isEmpty());
    EXPECT_EQ(manager.createNavigationView(QStringLiteral("missing"), QStringLiteral("view"), nullptr), nullptr);
    EXPECT_EQ(manager.createSettingsSection(QStringLiteral("missing"), QStringLiteral("missing"), QStringLiteral("general"), nullptr), nullptr);

    {
        app::ConfigurationManager configurationManager(databaseExecutor, directory.filePath(QStringLiteral("workpane-import.sqlite3")), manager.databaseSchemaVersions());
        const auto coreSettings = ui::ApplicationSettingsContributions::applicationSettingsContributions(manager, settings, configurationManager);
        ASSERT_EQ(coreSettings.size(), 1);
        EXPECT_EQ(coreSettings.first().group.id, QStringLiteral("application"));
        EXPECT_EQ(coreSettings.first().group.sections.first().id, QStringLiteral("general"));
        EXPECT_EQ(coreSettings.first().createSection(QStringLiteral("missing"), QStringLiteral("general"), nullptr), nullptr);
        ui::MainWindow window(manager, settings, coreSettings);
        window.show();
        QApplication::processEvents();
        // The window opens on its loading page and builds the interface once the plugins it presents are ready.
        EXPECT_NE(window.findChild<QWidget*>(QStringLiteral("startupLoading")), nullptr);
        EXPECT_FALSE(window.ready());
        ASSERT_TRUE(window.buildInterface().hasValue());
        EXPECT_TRUE(window.ready());
        window.resize(1100, 700);
        QApplication::processEvents();
        EXPECT_EQ(window.windowTitle(), QStringLiteral("Workpane"));
        EXPECT_GE(window.minimumWidth(), 820);
        EXPECT_NE(window.findChild<ui::ModeBar*>(), nullptr);
        EXPECT_NE(window.findChild<QWidget*>(QStringLiteral("ai/tasks")), nullptr);
        EXPECT_NE(window.findChild<QWidget*>(QStringLiteral("systemInformationView")), nullptr);
        EXPECT_NE(window.findChild<ui::SettingsView*>(), nullptr);
        EXPECT_NE(window.findChild<QLineEdit*>(), nullptr);
        auto* categories = window.findChild<QListWidget*>(QStringLiteral("settingsCategories"));
        ASSERT_NE(categories, nullptr);
        EXPECT_EQ(categories->count(), 11);
        EXPECT_NE(window.findChild<QComboBox*>(QStringLiteral("applicationLanguage")), nullptr);
        auto* applicationVersion = window.findChild<QLabel*>(QStringLiteral("applicationVersion"));
        ASSERT_NE(applicationVersion, nullptr);
        EXPECT_EQ(applicationVersion->text(), QCoreApplication::applicationVersion());
        EXPECT_FALSE(QCoreApplication::applicationVersion().isEmpty());
        EXPECT_NE(window.findChild<QPushButton*>(QStringLiteral("importConfiguration")), nullptr);
        EXPECT_NE(window.findChild<QPushButton*>(QStringLiteral("exportConfiguration")), nullptr);

        // Every settings row of every owner keeps the inset the page gave up, so no owner writes its own margin over the shared form.
        auto* settingsView = window.findChild<ui::SettingsView*>();
        ASSERT_NE(settingsView, nullptr);
        const int settingsInset = manager.theme().metric(ui::ThemeMetric::SettingsHorizontalPadding);
        EXPECT_GT(settingsInset, 0);
        const auto settingsForms = settingsView->findChildren<QFormLayout*>();
        EXPECT_GT(settingsForms.size(), 0);

        for (auto* sectionForm : settingsForms) {
            EXPECT_EQ(sectionForm->contentsMargins().left(), settingsInset) << sectionForm->parent()->objectName().toStdString();
            EXPECT_EQ(sectionForm->contentsMargins().right(), settingsInset) << sectionForm->parent()->objectName().toStdString();
        }

        for (auto* actionRow : settingsView->findChildren<QWidget*>(QStringLiteral("settingsActionRow"))) {
            ASSERT_NE(actionRow->layout(), nullptr);
            EXPECT_EQ(actionRow->layout()->contentsMargins().left(), settingsInset);
        }

        auto* browserTabs = window.findChild<QTabWidget*>(QStringLiteral("browserTabs"));
        ASSERT_NE(browserTabs, nullptr);
        EXPECT_EQ(browserTabs->count(), 1);
        auto* browserCloseTab = window.findChild<QAction*>(QStringLiteral("browserCloseTab"));
        ASSERT_NE(browserCloseTab, nullptr);
        // Windows declares Ctrl+F4 first and Ctrl+W second for closing, so taking only the first leaves the key every reader presses doing nothing.
        EXPECT_EQ(browserCloseTab->shortcuts(), QKeySequence::keyBindings(QKeySequence::Close));
        EXPECT_GT(browserCloseTab->shortcuts().size(), 1);
        EXPECT_EQ(browserCloseTab->shortcutContext(), Qt::WidgetWithChildrenShortcut);
        auto* donateButton = window.findChild<QWidget*>(QStringLiteral("donate/support"));
        auto* settingsButton = window.findChild<QWidget*>(QStringLiteral("workpane/settings"));
        ASSERT_NE(donateButton, nullptr);
        ASSERT_NE(settingsButton, nullptr);
        EXPECT_LT(donateButton->geometry().top(), settingsButton->geometry().top());

        QSignalSpy notifications(&manager, &plugins::PluginManager::notificationRequested);
        manager.notify(QStringLiteral("Title"), QStringLiteral("Message"), plugins::AlertSeverity::Error);
        ASSERT_EQ(notifications.count(), 1);
        auto* overlay = window.findChild<QWidget*>(QStringLiteral("toastOverlay"));
        ASSERT_NE(overlay, nullptr);
        auto* toast = overlay->findChild<QWidget*>(QStringLiteral("toast"));
        ASSERT_NE(toast, nullptr);
        EXPECT_TRUE(toast->isVisible());
        auto* toastTitle = toast->findChild<QLabel*>(QStringLiteral("toastTitle"));
        ASSERT_NE(toastTitle, nullptr);
        EXPECT_EQ(toastTitle->text(), QStringLiteral("Title"));
        auto* toastMessage = toast->findChild<QLabel*>(QStringLiteral("toastMessage"));
        ASSERT_NE(toastMessage, nullptr);
        EXPECT_EQ(toastMessage->text(), QStringLiteral("Message"));

        auto* quitAction = window.findChild<QAction*>(QStringLiteral("applicationQuitAction"));
        ASSERT_NE(quitAction, nullptr);
        EXPECT_EQ(quitAction->shortcut(), ui::ApplicationShortcuts::quit());
        EXPECT_EQ(quitAction->shortcutContext(), Qt::ApplicationShortcut);
        EXPECT_EQ(quitAction->menuRole(), QAction::QuitRole);

        CoreTestsHelper::answerNextConfirmation(false);
        EXPECT_FALSE(window.close());
        EXPECT_TRUE(window.isVisible());

        CoreTestsHelper::answerNextConfirmation(true);
        EXPECT_TRUE(window.close());
        EXPECT_FALSE(window.isVisible());
    }

    manager.shutdown();
    manager.shutdown();
    ASSERT_TRUE(store.markShutdown(true).hasValue());
}

plugins::TranslationCatalog CoreTestsHelper::validCatalog() {
    return {{QStringLiteral("en"), {{QStringLiteral("sample.general.title"), QStringLiteral("English")}, {QStringLiteral("sample.general.secondary"), QStringLiteral("Secondary")}}}, {QStringLiteral("pt"), {{QStringLiteral("sample.general.title"), QStringLiteral("Português")}}}, {QStringLiteral("pt-br"), {{QStringLiteral("sample.general.title"), QStringLiteral("Português brasileiro")}}}};
}

void CoreTestsHelper::executeSqliteStatements(const QString& path, const QStringList& statements) {
    const QString connectionName = QStringLiteral("test-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(path);
        ASSERT_TRUE(database.open()) << database.lastError().text().toStdString();

        for (const auto& statement : statements) {
            QSqlQuery query(database);
            ASSERT_TRUE(query.exec(statement)) << query.lastError().text().toStdString();
        }

        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
}

QString CoreTestsHelper::createValidDatabase(const QTemporaryDir& directory, const QString& name) {
    const QString path = directory.filePath(name);
    persistence::StateStore store(path);
    EXPECT_TRUE(store.initialize().hasValue());
    return path;
}

void CoreTestsHelper::answerNextConfirmation(bool confirmed) {
    // clang-format off
    QTimer::singleShot(0, qApp, [confirmed]() {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dialog == nullptr) {
            qFatal("The confirmation dialog was not opened");
        }
        const QString buttonName = confirmed ? QStringLiteral("destructiveButton") : QStringLiteral("cancelButton");
        auto* button = dialog->findChild<QPushButton*>(buttonName);
        if (button == nullptr) {
            qFatal("The expected confirmation action was not found");
        }
        button->click();
    });
    // clang-format on
}
TEST(SettingsReaderTest, LeavesEveryDeclaredDefaultStandingForAnyHostileDocument) {
    const QList<QByteArray> hostile{
        QByteArrayLiteral("{}"), QByteArrayLiteral("[]"), QByteArrayLiteral("null"), QByteArrayLiteral("{\"flag\": null}"), QByteArrayLiteral("{\"flag\": \"true\"}"), QByteArrayLiteral("{\"flag\": 1}"), QByteArrayLiteral("{\"count\": \"12\"}"), QByteArrayLiteral("{\"count\": 1.5}"), QByteArrayLiteral("{\"count\": 1e309}"), QByteArrayLiteral("{\"count\": -9223372036854775808}"), QByteArrayLiteral("{\"name\": 12}"), QByteArrayLiteral("{\"name\": []}"), QByteArrayLiteral("{\"entries\": {}}"), QByteArrayLiteral("{\"entries\": [1, \"two\", null, {}]}"), QByteArrayLiteral("{\"nested\": []}"),
    };

    for (const auto& document : hostile) {
        const QJsonObject object = QJsonDocument::fromJson(document).object();
        plugins::SettingsReader reader(object);
        bool flag = true;
        int count = 7;
        QString name = QStringLiteral("declared");
        QJsonObject nested{{QStringLiteral("kept"), true}};
        QVector<QJsonObject> entries{QJsonObject{{QStringLiteral("kept"), true}}};
        QStringList names{QStringLiteral("kept")};
        std::ignore = plugins::SettingsReaders::readSettingsTextList(object, QStringLiteral("names"), names);
        reader.readBool(QStringLiteral("flag"), flag);
        reader.readInteger(QStringLiteral("count"), count);
        reader.readText(QStringLiteral("name"), name);
        reader.readObject(QStringLiteral("nested"), nested);
        reader.readObjectList(QStringLiteral("entries"), entries);

        // A value the owner cannot use leaves the declared default exactly as it was.
        const bool usableFlag = object.value(QStringLiteral("flag")).isBool();
        const bool usableCount = object.value(QStringLiteral("count")).isDouble() && object.value(QStringLiteral("count")).toDouble() == std::floor(object.value(QStringLiteral("count")).toDouble());
        const bool usableName = object.value(QStringLiteral("name")).isString();
        EXPECT_TRUE(usableFlag || flag) << document.toStdString();
        EXPECT_TRUE(usableCount || count == 7) << document.toStdString();
        EXPECT_TRUE(usableName || name == QStringLiteral("declared")) << document.toStdString();
        EXPECT_TRUE(object.value(QStringLiteral("nested")).isObject() || nested.value(QStringLiteral("kept")).toBool()) << document.toStdString();
        EXPECT_TRUE(object.value(QStringLiteral("names")).isArray() || names == QStringList{QStringLiteral("kept")}) << document.toStdString();
    }

    // A deeply nested document is read without reaching past what it carries.
    QByteArray deep;
    for (int level = 0; level < 4000; ++level) {
        deep.append(QByteArrayLiteral("{\"nested\":"));
    }
    deep.append(QByteArrayLiteral("1"));
    for (int level = 0; level < 4000; ++level) {
        deep.append(QByteArrayLiteral("}"));
    }
    const QJsonObject nestedDocument = QJsonDocument::fromJson(deep).object();
    plugins::SettingsReader nestedReader(nestedDocument);
    QJsonObject value;
    nestedReader.readObject(QStringLiteral("nested"), value);
    SUCCEED();
}

TEST(PluginManagerIntegrationTest, LeavesNoGuardOnTheContextOfARequestThatAlreadyAnswered) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("workpane.sqlite3"));
    persistence::StateStore store(databasePath);
    persistence::DatabaseExecutor databaseExecutor(databasePath);
    ASSERT_TRUE(store.initialize().hasValue());
    plugins::PluginManager manager;
    ASSERT_TRUE(manager.loadPlugins().hasValue());
    ASSERT_TRUE(manager.initialize(directory.path(), store, databaseExecutor).hasValue());

    // Qt hands back the instance the manager already loaded, so the plugin that asks the terminal for a snapshot can be named from here.
    QObject* webServer = nullptr;
    const QDir applicationDirectory(QCoreApplication::applicationDirPath());
    for (const auto& candidate : {QStringLiteral("plugins"), QStringLiteral("../PlugIns")}) {
        for (const auto& entry : QDir(applicationDirectory.filePath(candidate)).entryInfoList(QDir::Files)) {
            if (entry.fileName().contains(QStringLiteral("web-server"))) {
                QPluginLoader loader(entry.absoluteFilePath());
                webServer = loader.instance();
            }
        }
    }
    ASSERT_NE(webServer, nullptr);

    QSignalSpy synchronized(webServer, SIGNAL(webServerChanged(QString)));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return synchronized.count() > 0; }));
    // clang-format on

    // The request forgets itself one queued call after the answer reaches its caller, so that call is drained before the guard is looked for.
    for (int pass = 0; pass < 10; ++pass) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    }

    // The snapshot request answered, so the context it was given carries nothing of it any more.
    EXPECT_FALSE(QObject::disconnect(webServer, SIGNAL(destroyed()), &manager, nullptr));

    manager.unloadPlugins();
}

TEST(PluginManagerIntegrationTest, HoldsEveryPluginToTheContractTheStandardStatesOfAllOfThem) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("workpane.sqlite3"));
    persistence::StateStore store(databasePath);
    persistence::DatabaseExecutor databaseExecutor(databasePath);
    ASSERT_TRUE(store.initialize().hasValue());
    plugins::PluginManager manager;
    ASSERT_TRUE(manager.setLocale(QStringLiteral("en")).hasValue());
    ASSERT_TRUE(manager.loadPlugins().hasValue());
    ASSERT_TRUE(manager.initialize(directory.path(), store, databaseExecutor).hasValue());

    const QRegularExpression identifierPattern(QStringLiteral("^[a-z0-9]+(?:-[a-z0-9]+)*$"));
    const auto navigation = manager.navigationItems();
    ASSERT_FALSE(navigation.isEmpty());

    QSet<QString> owners;
    QHash<QString, QSet<QString>> itemsByOwner;
    QHash<int, QString> positionsByPlacement;
    for (const auto& contribution : navigation) {
        const QString owner = contribution.pluginId;
        owners.insert(owner);
        EXPECT_TRUE(identifierPattern.match(owner).hasMatch()) << owner.toStdString();
        EXPECT_TRUE(identifierPattern.match(contribution.item.id).hasMatch()) << contribution.item.id.toStdString();
        EXPECT_FALSE(itemsByOwner[owner].contains(contribution.item.id)) << owner.toStdString() << " declares " << contribution.item.id.toStdString() << " twice";
        itemsByOwner[owner].insert(contribution.item.id);

        // A navigation item carries an icon that is really painted and a title its own catalog spells.
        EXPECT_FALSE(contribution.item.icon.isNull()) << owner.toStdString();
        EXPECT_TRUE(contribution.item.titleKey.startsWith(owner + QLatin1Char('.'))) << contribution.item.titleKey.toStdString();
        EXPECT_NE(manager.translate(contribution.item.titleKey), contribution.item.titleKey) << contribution.item.titleKey.toStdString();

        // Two destinations never claim one position within one placement.
        const int placementKey = static_cast<int>(contribution.item.placement) * 100000 + static_cast<int>(contribution.item.order);
        EXPECT_FALSE(positionsByPlacement.contains(placementKey)) << owner.toStdString() << " shares a position with " << positionsByPlacement.value(placementKey).toStdString();
        positionsByPlacement.insert(placementKey, owner);
    }

    for (const auto& contribution : manager.settings()) {
        const QString owner = contribution.pluginId;
        EXPECT_TRUE(identifierPattern.match(contribution.group.id).hasMatch()) << contribution.group.id.toStdString();
        EXPECT_NE(manager.translate(contribution.group.titleKey), contribution.group.titleKey) << contribution.group.titleKey.toStdString();
        ASSERT_FALSE(contribution.group.sections.isEmpty()) << contribution.group.id.toStdString();

        // A group of one section identifies that section as general, and every section is searchable.
        if (contribution.group.sections.size() == 1) {
            EXPECT_EQ(contribution.group.sections.first().id, QStringLiteral("general")) << owner.toStdString() << " " << contribution.group.id.toStdString();
        }

        QSet<QString> sectionIds;
        for (const auto& section : contribution.group.sections) {
            EXPECT_TRUE(identifierPattern.match(section.id).hasMatch()) << section.id.toStdString();
            EXPECT_FALSE(sectionIds.contains(section.id)) << contribution.group.id.toStdString() << " declares " << section.id.toStdString() << " twice";
            sectionIds.insert(section.id);
            EXPECT_NE(manager.translate(section.titleKey), section.titleKey) << section.titleKey.toStdString();
            EXPECT_FALSE(section.searchKeys.isEmpty()) << contribution.group.id.toStdString() << " " << section.id.toStdString();
            for (const auto& searchKey : section.searchKeys) {
                EXPECT_NE(manager.translate(searchKey), searchKey) << searchKey.toStdString();
            }
        }
    }

    // One condition carries one code, so every plugin answers a topic it does not implement by the same name.
    // Qt hands back the instance the manager already loaded, so every plugin can be asked here.
    int asked = 0;
    const QDir applicationDirectory(QCoreApplication::applicationDirPath());

    for (const auto& candidate : {QStringLiteral("plugins"), QStringLiteral("../PlugIns")}) {
        for (const auto& entry : QDir(applicationDirectory.filePath(candidate)).entryInfoList(QDir::Files)) {
            QPluginLoader loader(entry.absoluteFilePath());
            auto* plugin = qobject_cast<plugins::PluginInterface*>(loader.instance());

            if (plugin == nullptr) {
                continue;
            }

            ++asked;
            QString reported;
            // clang-format off
            const auto record = [&reported](const Result<QJsonObject>& result) { reported = result.hasValue() ? QStringLiteral("answered") : result.error().code; };
            // clang-format on
            plugin->handleRequest(QStringLiteral("logs"), QStringLiteral("nobody.declares.this"), {}, record);
            EXPECT_EQ(reported, QStringLiteral("plugin_message_topic_unknown")) << plugin->id().toStdString() << " reports an unknown topic by a name of its own";
        }
    }

    EXPECT_EQ(asked, owners.size()) << "a plugin was never asked how it answers a topic it does not implement";

    // Every plugin that declares a schema owns the tables carrying its own prefix, and its version counts from the creating migration.
    const auto versions = manager.databaseSchemaVersions();
    for (auto version = versions.constBegin(); version != versions.constEnd(); ++version) {
        EXPECT_GE(version.value(), 1) << version.key().toStdString();
        EXPECT_TRUE(owners.contains(version.key())) << version.key().toStdString();
    }

    manager.unloadPlugins();
}

TEST(StateStoreTest, KeepsTheWriteAheadLogWithTheDatabaseItSetsAside) {
#ifdef Q_OS_WIN
    GTEST_SKIP() << "The platform does not refuse a read through file permissions";
#else
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("workpane.sqlite3"));
    QFile stored(databasePath);
    ASSERT_TRUE(stored.open(QIODevice::WriteOnly));
    ASSERT_GT(stored.write(QByteArrayLiteral("SQLite format 3")), 0);
    stored.close();
    QFile log(databasePath + QStringLiteral("-wal"));
    ASSERT_TRUE(log.open(QIODevice::WriteOnly));
    ASSERT_GT(log.write(QByteArrayLiteral("committed")), 0);
    log.close();

    // A database nobody may read is never opened, so the log beside it is still whatever was committed last.
    ASSERT_TRUE(stored.setPermissions({}));
    if (QFile(databasePath).open(QIODevice::ReadOnly)) {
        ASSERT_TRUE(stored.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        GTEST_SKIP() << "The account reads a file it has no permission for";
    }

    persistence::StateStore store(databasePath);
    const auto initialized = store.initialize();
    ASSERT_TRUE(stored.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    ASSERT_TRUE(initialized.hasValue()) << initialized.error().code.toStdString();
    ASSERT_FALSE(store.replacedDatabasePath().isEmpty());

    QFile keptLog(store.replacedDatabasePath() + QStringLiteral("-wal"));
    ASSERT_TRUE(keptLog.open(QIODevice::ReadOnly));
    EXPECT_EQ(keptLog.readAll(), QByteArrayLiteral("committed"));
    EXPECT_FALSE(QFileInfo::exists(databasePath + QStringLiteral("-wal-orphan")));
#endif
}

TEST(ApplicationTest, KeepsEverythingAliveWhileTheRestartItWasAskedForIsStillOnTheStack) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    app::Application application(directory.path(), nullptr);
    ASSERT_TRUE(application.initialize().hasValue());
    ASSERT_TRUE(application.loadInterface().hasValue());
    const qsizetype windowsBefore = QApplication::topLevelWidgets().size();
    ASSERT_GT(windowsBefore, 0);

    // The restart is asked for by a surface the teardown destroys, so nothing may be destroyed before that request returns.
    ASSERT_TRUE(QMetaObject::invokeMethod(&application, "restartAfterImport", Qt::DirectConnection));
    EXPECT_EQ(QApplication::topLevelWidgets().size(), windowsBefore);

    // The deferred call is bound to the application, so leaving this scope cancels it rather than starting a process.
    application.shutdown();
}

// A style sheet reads the family as a name and discards the hint, so the role has to resolve a family that really is monospaced.
TEST(ThemeFontTests, ResolvesTheMonospaceRoleToAFamilyThatReallyIsMonospaced) {
    ASSERT_FALSE(ui::Components::monospacedFontFamilies().isEmpty());

    const ui::ThemeCatalog catalog;

    for (const auto& theme : catalog.themes()) {
        const QFont monospace = theme->font(ui::ThemeFont::Monospace);
        EXPECT_TRUE(QFontDatabase::isFixedPitch(monospace.family())) << theme->id().toStdString() << " resolves " << monospace.family().toStdString() << ", which is not a monospaced family";

        const QFontMetricsF metrics{QFont(monospace.family())};
        EXPECT_DOUBLE_EQ(metrics.horizontalAdvance(QLatin1Char('i')), metrics.horizontalAdvance(QLatin1Char('W'))) << theme->id().toStdString() << " resolves a family whose glyphs do not share one advance";
        EXPECT_NE(monospace.family(), QApplication::font().family()) << theme->id().toStdString() << " leaves the monospace role on the interface family";
    }
}

TEST(CoreTranslationsTest, SpellsEveryKeyInEveryLanguageTheSelectorOffers) {
    workpane::test::TestCatalogs::expectCompleteCatalog(QStringLiteral("workpane"), workpane::plugins::coretranslations::CoreCatalog::catalog());
}

// A row that shrinks around a toggle reads as a different kind of row, so every settings row is the same height.
TEST(ComponentsTest, GivesEverySettingsRowTheSameHeightWhateverControlItCarries) {
    const workpane::ui::Theme& theme = workpane::ui::ThemeManager::instance().theme();
    QWidget host;
    host.setStyleSheet(workpane::ui::ApplicationStyleSheet::applicationStyleSheet(theme));
    auto* form = new QFormLayout(&host);
    auto* combo = new workpane::ui::ComboBox(theme, &host);
    combo->addItem(QStringLiteral("Vivid"));
    workpane::ui::Components::addSettingsRow(form, QStringLiteral("Color intensity"), combo);
    auto* toggle = new QCheckBox(&host);
    workpane::ui::Components::addSettingsRow(form, QStringLiteral("Confirm multiline paste"), toggle);
    auto* text = new QLineEdit(&host);
    workpane::ui::Components::addSettingsRow(form, QStringLiteral("Homepage"), text);
    auto* button = new QPushButton(QStringLiteral("Refresh"), &host);
    workpane::ui::Components::addSettingsRow(form, QStringLiteral("Language servers"), button);
    auto* number = new QSpinBox(&host);
    workpane::ui::Components::addSettingsRow(form, QStringLiteral("Font size"), workpane::ui::Components::stepperRow(number, theme, &host));
    host.show();
    host.resize(900, 420);
    QCoreApplication::processEvents();

    const int reference = form->itemAt(0, QFormLayout::FieldRole)->widget()->height();

    for (int row = 1; row < form->rowCount(); ++row) {
        EXPECT_EQ(form->itemAt(row, QFormLayout::FieldRole)->widget()->height(), reference) << "row " << row << " carries a different height";
    }
}

TEST(CapabilityRegistryTest, AcceptsOnlyTheDeclaredNameGrammar) {
    EXPECT_TRUE(plugins::CapabilityNames::validCapabilityName(QStringLiteral("workspace.folder.open")));
    EXPECT_TRUE(plugins::CapabilityNames::validCapabilityName(QStringLiteral("agent-tools.resource.list")));
    EXPECT_TRUE(plugins::CapabilityNames::validCapabilityName(QStringLiteral("a1.b2.c3")));

    EXPECT_FALSE(plugins::CapabilityNames::validCapabilityName(QStringLiteral("workspace.open")));
    EXPECT_FALSE(plugins::CapabilityNames::validCapabilityName(QStringLiteral("workspace.folder.open.now")));
    EXPECT_FALSE(plugins::CapabilityNames::validCapabilityName(QStringLiteral("Workspace.folder.open")));
    EXPECT_FALSE(plugins::CapabilityNames::validCapabilityName(QStringLiteral("workspace..open")));
    EXPECT_FALSE(plugins::CapabilityNames::validCapabilityName(QStringLiteral("workspace.-folder.open")));
    EXPECT_FALSE(plugins::CapabilityNames::validCapabilityName(QStringLiteral("workspace.folder-.open")));
    EXPECT_FALSE(plugins::CapabilityNames::validCapabilityName(QStringLiteral("workspace.fold_er.open")));
    EXPECT_FALSE(plugins::CapabilityNames::validCapabilityName(QString{}));
}

TEST(CapabilityRegistryTest, RefusesASecondProviderOfOneName) {
    plugins::CapabilityRegistry registry;
    const plugins::CapabilityDescriptor seam{QStringLiteral("workspace.folder.open")};

    ASSERT_TRUE(registry.provide(QStringLiteral("code-editor"), seam).hasValue());
    EXPECT_EQ(registry.provider(seam.name), QStringLiteral("code-editor"));

    // A capability is answered by exactly one provider, so a second one is refused rather than queued behind it.
    const auto taken = registry.provide(QStringLiteral("browser"), seam);
    ASSERT_FALSE(taken.hasValue());
    EXPECT_EQ(taken.error().code, QStringLiteral("capability_already_provided"));
    EXPECT_EQ(registry.provider(seam.name), QStringLiteral("code-editor"));

    const auto again = registry.provide(QStringLiteral("code-editor"), seam);
    ASSERT_FALSE(again.hasValue());
    EXPECT_EQ(again.error().code, QStringLiteral("capability_already_provided"));
}

TEST(CapabilityRegistryTest, RefusesAnInvalidDeclaration) {
    plugins::CapabilityRegistry registry;

    const auto badName = registry.provide(QStringLiteral("ai"), {QStringLiteral("nope")});
    ASSERT_FALSE(badName.hasValue());
    EXPECT_EQ(badName.error().code, QStringLiteral("capability_name_invalid"));

    const auto badProvider = registry.provide(QString{}, {QStringLiteral("workspace.folder.open")});
    ASSERT_FALSE(badProvider.hasValue());
    EXPECT_EQ(badProvider.error().code, QStringLiteral("capability_provider_invalid"));

    const auto badVersion = registry.provide(QStringLiteral("ai"), {QStringLiteral("workspace.folder.open"), 0});
    ASSERT_FALSE(badVersion.hasValue());
    EXPECT_EQ(badVersion.error().code, QStringLiteral("capability_version_invalid"));

    ASSERT_TRUE(registry.provide(QStringLiteral("ai"), {QStringLiteral("workspace.folder.open"), 2}).hasValue());
    EXPECT_EQ(registry.version(QStringLiteral("workspace.folder.open")), 2);
}

TEST(CapabilityRegistryTest, ReleasesEveryCapabilityWhenThePluginsShutDown) {
    plugins::CapabilityRegistry registry;

    ASSERT_TRUE(registry.provide(QStringLiteral("ai"), {QStringLiteral("workspace.folder.open")}).hasValue());
    ASSERT_TRUE(registry.provide(QStringLiteral("logs"), {QStringLiteral("logs.entries.page")}).hasValue());
    EXPECT_EQ(registry.names(), (QStringList{QStringLiteral("logs.entries.page"), QStringLiteral("workspace.folder.open")}));

    // A plugin is never unloaded on its own, so the capabilities are released together when the plugins shut down.
    registry.clear();

    EXPECT_TRUE(registry.names().isEmpty());
    EXPECT_FALSE(registry.contains(QStringLiteral("workspace.folder.open")));
    EXPECT_TRUE(registry.provider(QStringLiteral("workspace.folder.open")).isEmpty());
    EXPECT_FALSE(registry.version(QStringLiteral("logs.entries.page")).has_value());
}

} // namespace workpane
