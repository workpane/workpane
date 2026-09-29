#include "Result.h"
#include "execution/MainThreadQueue.h"
#include "persistence/ConfigurationTransfer.h"
#include "persistence/CoreSchema.h"
#include "persistence/Database.h"
#include "persistence/DatabaseBootstrap.h"
#include "persistence/DatabaseExecutor.h"
#include "persistence/InstanceLock.h"
#include "persistence/PluginDatabase.h"
#include "persistence/PreferenceStore.h"
#include "persistence/Statement.h"
#include "support/MainThreadPump.h"
#include "support/ProductDatabase.h"
#include "support/TemporaryDirectory.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace workpane::persistence {

using tests::TemporaryDirectory;

TEST(Database, RunsOneStatementWithBoundValuesAndAnswersRows) {
    TemporaryDirectory directory;
    auto database = Database::open(directory.path() / "sample.sqlite3");
    ASSERT_TRUE(database.hasValue());
    ASSERT_TRUE(database.value().executeScript("CREATE TABLE items(id INTEGER PRIMARY KEY, name TEXT NOT NULL, score REAL, active INTEGER);").hasValue());

    const auto inserted = database.value().run("INSERT INTO items(name, score, active) VALUES(?, ?, ?)", {"first", 1.5, true});
    ASSERT_TRUE(inserted.hasValue());
    EXPECT_EQ(inserted.value().changes, 1);
    EXPECT_EQ(inserted.value().lastInsertRowId, 1);

    const auto rows = database.value().query("SELECT id, name, score, active FROM items WHERE name = ?", {"first"});
    ASSERT_TRUE(rows.hasValue());
    ASSERT_EQ(rows.value().size(), 1U);
    EXPECT_EQ(rows.value()[0]["name"], "first");
    EXPECT_DOUBLE_EQ(rows.value()[0]["score"].get<double>(), 1.5);
    EXPECT_EQ(rows.value()[0]["active"], 1);
}

TEST(Database, RefusesMultipleStatementsWrongBindingsAndEmptyText) {
    TemporaryDirectory directory;
    auto database = Database::open(directory.path() / "sample.sqlite3");
    ASSERT_TRUE(database.hasValue());
    ASSERT_TRUE(database.value().executeScript("CREATE TABLE items(name TEXT);").hasValue());

    EXPECT_EQ(database.value().run("INSERT INTO items VALUES('a'); DELETE FROM items").error().code, "database_statement_multiple");
    EXPECT_EQ(database.value().run("INSERT INTO items VALUES(?)", {}).error().code, "database_binding_count");
    EXPECT_EQ(database.value().run("INSERT INTO items VALUES(?)", {nlohmann::json::object()}).error().code, "database_binding_type");
    EXPECT_EQ(database.value().run("   ").error().code, "database_statement_empty");
    EXPECT_EQ(database.value().query("SELECT 1e999 AS infinite").error().code, "database_value_invalid");
}

TEST(DatabaseBootstrap, CreatesTheCoreSchemaAndSetsAsideAnUnreadableFile) {
    TemporaryDirectory directory;
    {
        auto opened = DatabaseBootstrap::open(directory.path());
        ASSERT_TRUE(opened.hasValue());
        EXPECT_FALSE(opened.value().setAside.has_value());
        EXPECT_TRUE(CoreSchema::validate(opened.value().database).hasValue());
    }

    std::ofstream(directory.path() / DatabaseBootstrap::databaseName, std::ios::binary | std::ios::trunc) << "this is not a database";
    auto reopened = DatabaseBootstrap::open(directory.path());

    ASSERT_TRUE(reopened.hasValue());
    ASSERT_TRUE(reopened.value().setAside.has_value());
    EXPECT_TRUE(std::filesystem::exists(*reopened.value().setAside));
    EXPECT_TRUE(CoreSchema::validate(reopened.value().database).hasValue());
}

// A staged import takes the place of the database at the next start, and one that does not validate leaves the previous database where it was with a notice.
TEST(DatabaseBootstrap, AppliesAStagedImportAndKeepsThePreviousDatabaseWhenItIsRefused) {
    TemporaryDirectory directory;
    TemporaryDirectory source;
    {
        auto database = tests::ProductDatabase::open(directory);
        ASSERT_TRUE(PreferenceStore::store(database, "workpane", {{"language", "en"}}).hasValue());
        auto exported = tests::ProductDatabase::open(source);
        ASSERT_TRUE(PreferenceStore::store(exported, "workpane", {{"language", "pt"}}).hasValue());
        ASSERT_TRUE(ConfigurationTransfer::exportTo(exported, directory.path() / DatabaseBootstrap::stagedImportName).hasValue());
    }

    {
        auto opened = DatabaseBootstrap::open(directory.path());
        ASSERT_TRUE(opened.hasValue()) << opened.error().code;
        EXPECT_FALSE(opened.value().importFailure.has_value());
        EXPECT_EQ(PreferenceStore::load(opened.value().database).value().documents.at("workpane")["language"], "pt");
    }

    EXPECT_FALSE(std::filesystem::exists(directory.path() / DatabaseBootstrap::stagedImportName));
    EXPECT_FALSE(std::filesystem::exists(directory.path() / DatabaseBootstrap::importBackupName));
    std::ofstream(directory.path() / DatabaseBootstrap::stagedImportName, std::ios::binary) << "not a database";
    auto refused = DatabaseBootstrap::open(directory.path());

    ASSERT_TRUE(refused.hasValue()) << refused.error().code;
    ASSERT_TRUE(refused.value().importFailure.has_value());
    EXPECT_EQ(refused.value().importFailure->code, "configuration_import_rejected");
    EXPECT_EQ(PreferenceStore::load(refused.value().database).value().documents.at("workpane")["language"], "pt");
    EXPECT_FALSE(std::filesystem::exists(directory.path() / DatabaseBootstrap::importBackupName));
}

// An import interrupted after it moved the previous database aside keeps the imported one only when it validates, and otherwise puts the previous database back, even when no database is left at all.
TEST(DatabaseBootstrap, RestoresThePreviousDatabaseOfAnInterruptedImport) {
    TemporaryDirectory directory;
    const std::filesystem::path file = directory.path() / DatabaseBootstrap::databaseName;
    const std::filesystem::path backup = directory.path() / DatabaseBootstrap::importBackupName;
    {
        auto database = tests::ProductDatabase::open(directory);
        ASSERT_TRUE(PreferenceStore::store(database, "workpane", {{"language", "en"}}).hasValue());
        ASSERT_TRUE(ConfigurationTransfer::exportTo(database, backup).hasValue());
    }

    std::filesystem::remove(file);
    {
        auto opened = DatabaseBootstrap::open(directory.path());
        ASSERT_TRUE(opened.hasValue()) << opened.error().code;
        ASSERT_TRUE(opened.value().importFailure.has_value());
        EXPECT_EQ(opened.value().importFailure->code, "configuration_import_interrupted");
        EXPECT_EQ(PreferenceStore::load(opened.value().database).value().documents.at("workpane")["language"], "en");
        ASSERT_TRUE(PreferenceStore::store(opened.value().database, "workpane", {{"language", "pt"}}).hasValue());
        ASSERT_TRUE(ConfigurationTransfer::exportTo(opened.value().database, backup).hasValue());
        ASSERT_TRUE(PreferenceStore::store(opened.value().database, "workpane", {{"language", "es"}}).hasValue());
    }

    auto kept = DatabaseBootstrap::open(directory.path());
    ASSERT_TRUE(kept.hasValue()) << kept.error().code;
    EXPECT_FALSE(kept.value().importFailure.has_value());
    EXPECT_EQ(PreferenceStore::load(kept.value().database).value().documents.at("workpane")["language"], "es");
    EXPECT_FALSE(std::filesystem::exists(backup));
}

// A staged import cut short while it was written, empty or half written, is dropped before the database of the reader moves, even after that database was already moved aside, so an import never replaces it with an empty one.
TEST(DatabaseBootstrap, DropsAStagedImportThatIsNotWhole) {
    TemporaryDirectory directory;
    TemporaryDirectory source;
    const std::filesystem::path staged = directory.path() / DatabaseBootstrap::stagedImportName;
    const std::filesystem::path backup = directory.path() / DatabaseBootstrap::importBackupName;
    const std::filesystem::path whole = source.path() / "whole.sqlite3";
    {
        auto database = tests::ProductDatabase::open(directory);
        ASSERT_TRUE(PreferenceStore::store(database, "workpane", {{"language", "en"}}).hasValue());
        auto exported = tests::ProductDatabase::open(source);
        ASSERT_TRUE(PreferenceStore::store(exported, "workpane", {{"language", "pt"}}).hasValue());
        ASSERT_TRUE(ConfigurationTransfer::exportTo(exported, whole).hasValue());
    }

    std::ofstream(staged, std::ios::binary | std::ios::trunc).flush();
    {
        auto empty = DatabaseBootstrap::open(directory.path());
        ASSERT_TRUE(empty.hasValue()) << empty.error().code;
        ASSERT_TRUE(empty.value().importFailure.has_value());
        EXPECT_EQ(empty.value().importFailure->code, "configuration_import_rejected");
        EXPECT_EQ(PreferenceStore::load(empty.value().database).value().documents.at("workpane")["language"], "en");
        EXPECT_FALSE(std::filesystem::exists(staged));
        EXPECT_FALSE(std::filesystem::exists(backup));
    }

    std::ifstream reading(whole, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(reading)), std::istreambuf_iterator<char>());
    std::ofstream(staged, std::ios::binary | std::ios::trunc) << bytes.substr(0, bytes.size() / 2);
    std::filesystem::rename(directory.path() / DatabaseBootstrap::databaseName, backup);
    auto half = DatabaseBootstrap::open(directory.path());

    ASSERT_TRUE(half.hasValue()) << half.error().code;
    ASSERT_TRUE(half.value().importFailure.has_value());
    EXPECT_EQ(half.value().importFailure->code, "configuration_import_rejected");
    EXPECT_EQ(PreferenceStore::load(half.value().database).value().documents.at("workpane")["language"], "en");
    EXPECT_FALSE(std::filesystem::exists(staged));
    EXPECT_FALSE(std::filesystem::exists(backup));
}

// A database file the machine could not open is left where it is and stops the start, since only a file that is not a database this version reads is set aside.
TEST(DatabaseBootstrap, StopsTheStartInsteadOfSettingAsideAFileItCouldNotOpen) {
    TemporaryDirectory directory;
    const std::filesystem::path file = directory.path() / DatabaseBootstrap::databaseName;
    std::filesystem::create_directories(file / "inside");
    const auto opened = DatabaseBootstrap::open(directory.path());
    // clang-format off
    const auto setAside = [](const std::filesystem::directory_entry& entry) { return entry.path().filename().string().find("unreadable") != std::string::npos; };
    // clang-format on

    EXPECT_FALSE(opened.hasValue());
    EXPECT_TRUE(std::filesystem::is_directory(file / "inside"));
    EXPECT_TRUE(std::ranges::none_of(std::filesystem::directory_iterator(directory.path()), setAside));
}

TEST(PreferenceStore, WritesDocumentsInTheBackgroundAndReadsThemBack) {
    TemporaryDirectory directory;
    execution::MainThreadQueue queue;
    {
        auto database = tests::ProductDatabase::open(directory);
        auto documents = PreferenceStore::load(database);
        ASSERT_TRUE(documents.hasValue());
        DatabaseExecutor executor(std::move(database), queue);
        PreferenceStore store(executor, std::move(documents.value().documents));
        bool finished = false;
        std::optional<Result<void>> outcome;
        // clang-format off
        store.write("sample", {{"theme", "blue"}}, [&](Result<void> result) { outcome = std::move(result); finished = true; });
        // clang-format on

        EXPECT_EQ(store.document("sample")["theme"], "blue");
        tests::MainThreadPump::until(queue, finished);
        ASSERT_TRUE(outcome.has_value());
        EXPECT_TRUE(outcome->hasValue());
        executor.shutdown();

        // Shutting down closes the connection, which folds its log back into the database, so another process may take the file at once.
        std::filesystem::path journal = directory.path() / DatabaseBootstrap::databaseName;
        journal += "-wal";
        EXPECT_FALSE(std::filesystem::exists(journal));
    }

    auto database = tests::ProductDatabase::open(directory);
    auto documents = PreferenceStore::load(database);
    ASSERT_TRUE(documents.hasValue());
    EXPECT_EQ(documents.value().documents.at("sample")["theme"], "blue");
}

// A first document whose write fails leaves its owner with an empty object, which every reader of a settings document expects.
TEST(PreferenceStore, GoesBackToAnEmptyDocumentWhenTheFirstWriteFails) {
    TemporaryDirectory directory;
    execution::MainThreadQueue queue;
    auto database = tests::ProductDatabase::open(directory);
    auto documents = PreferenceStore::load(database);
    ASSERT_TRUE(documents.hasValue());
    ASSERT_TRUE(database.executeScript("DROP TABLE core_settings;").hasValue());
    DatabaseExecutor executor(std::move(database), queue);
    PreferenceStore store(executor, std::move(documents.value().documents));
    bool finished = false;
    std::optional<Result<void>> outcome;
    // clang-format off
    store.write("fresh", {{"theme", "blue"}}, [&](Result<void> result) { outcome = std::move(result); finished = true; });
    // clang-format on
    tests::MainThreadPump::until(queue, finished);

    ASSERT_TRUE(outcome.has_value());
    EXPECT_FALSE(outcome->hasValue());
    EXPECT_EQ(store.document("fresh"), nlohmann::json::object());
    executor.shutdown();
}

TEST(PreferenceStore, RefusesADocumentThatIsNotAnObject) {
    TemporaryDirectory directory;
    execution::MainThreadQueue queue;
    auto database = tests::ProductDatabase::open(directory);
    DatabaseExecutor executor(std::move(database), queue);
    PreferenceStore store(executor, {});
    std::optional<Result<void>> outcome;
    // clang-format off
    store.write("sample", nlohmann::json::array(), [&outcome](Result<void> result) { outcome = std::move(result); });
    // clang-format on

    ASSERT_TRUE(outcome.has_value());
    EXPECT_EQ(outcome->error().code, "preferences_document_invalid");
    executor.shutdown();
}

// A plugin reaches only its own tables, changes its schema only through its migrations and never reaches another plugin, the core tables or the schema itself.
TEST(PluginDatabase, ScopesEveryStatementToTheTablesOfItsPlugin) {
    TemporaryDirectory directory;
    auto database = tests::ProductDatabase::open(directory);
    const std::vector<std::vector<std::string>> logs{{"CREATE TABLE logs__entries(sequence INTEGER PRIMARY KEY AUTOINCREMENT, message TEXT NOT NULL) STRICT", "CREATE INDEX logs__entries_message_index ON logs__entries(message)"}, {"ALTER TABLE logs__entries ADD COLUMN source TEXT", "ALTER TABLE logs__entries ADD COLUMN level TEXT NOT NULL DEFAULT 'info' CHECK(level <> '')"}};

    EXPECT_EQ(PluginDatabase::tablePrefix("code-editor"), "code_editor__");
    ASSERT_TRUE(PluginDatabase::migrate(database, "logs", logs).hasValue());
    ASSERT_TRUE(PluginDatabase::migrate(database, "other", {{"CREATE TABLE other__items(name TEXT)"}}).hasValue());
    ASSERT_TRUE(PluginDatabase::run(database, "logs", "INSERT INTO logs__entries(message) VALUES(?)", {"stored"}).hasValue());

    const auto rows = PluginDatabase::query(database, "logs", "SELECT message FROM logs__entries", {});
    ASSERT_TRUE(rows.hasValue());
    EXPECT_EQ(rows.value().at(0)["message"], "stored");

    // Another plugin, the core tables, the schema itself even through a table created from a query, a rename out of the namespace and a change of the schema outside a migration are all refused.
    EXPECT_FALSE(PluginDatabase::query(database, "logs", "SELECT name FROM other__items", {}).hasValue());
    EXPECT_FALSE(PluginDatabase::query(database, "logs", "SELECT owner FROM core_settings", {}).hasValue());
    EXPECT_FALSE(PluginDatabase::query(database, "logs", "SELECT name FROM sqlite_master", {}).hasValue());
    EXPECT_FALSE(PluginDatabase::run(database, "logs", "DELETE FROM sqlite_sequence", {}).hasValue());
    EXPECT_FALSE(PluginDatabase::migrate(database, "renamer", {{"CREATE TABLE renamer__items(name TEXT)", "ALTER TABLE renamer__items RENAME TO other__items_copy"}}).hasValue());
    EXPECT_FALSE(PluginDatabase::migrate(database, "stealer", {{"CREATE TABLE other__stolen(name TEXT)"}}).hasValue());
    EXPECT_FALSE(PluginDatabase::run(database, "logs", "PRAGMA journal_mode = DELETE", {}).hasValue());
    EXPECT_FALSE(PluginDatabase::run(database, "logs", "ATTACH DATABASE 'x.sqlite3' AS x", {}).hasValue());
    EXPECT_FALSE(PluginDatabase::migrate(database, "copier", {{"CREATE TABLE copier__schema AS SELECT name, sql FROM sqlite_master"}}).hasValue());
    EXPECT_FALSE(PluginDatabase::migrate(database, "counter", {{"CREATE TABLE counter__counters AS SELECT name, seq FROM sqlite_sequence"}}).hasValue());
    EXPECT_EQ(PluginDatabase::run(database, "logs", "CREATE TABLE logs__later(name TEXT)", {}).error().code, "database_statement_invalid");
    EXPECT_EQ(PluginDatabase::run(database, "logs", "DROP TABLE logs__entries", {}).error().code, "database_statement_invalid");

    // A result that names one column twice is refused instead of keeping only one of the values.
    EXPECT_EQ(PluginDatabase::query(database, "logs", "SELECT message, source AS message FROM logs__entries", {}).error().code, "database_column_duplicate");
}

// A plugin whose identifier starts the identifier of another one, such as web and web-server, still reaches only its own tables.
TEST(PluginDatabase, KeepsThePluginsWhoseIdentifiersShareABeginningApart) {
    TemporaryDirectory directory;
    auto database = tests::ProductDatabase::open(directory);

    ASSERT_TRUE(PluginDatabase::migrate(database, "web-server", {{"CREATE TABLE web_server__items(name TEXT)"}}).hasValue());
    ASSERT_TRUE(PluginDatabase::migrate(database, "web", {{"CREATE TABLE web__server_notes(name TEXT)"}}).hasValue());

    EXPECT_FALSE(PluginDatabase::query(database, "web", "SELECT name FROM web_server__items", {}).hasValue());
    EXPECT_FALSE(PluginDatabase::migrate(database, "web", {{"CREATE TABLE web__server_notes(name TEXT)"}, {"CREATE TABLE web_server__stolen(name TEXT)"}}).hasValue());
    EXPECT_FALSE(PluginDatabase::query(database, "web-server", "SELECT name FROM web__server_notes", {}).hasValue());
    EXPECT_TRUE(PluginDatabase::query(database, "web", "SELECT name FROM web__server_notes", {}).hasValue());
}

// A text primary key and a unique column make SQLite create an index of its own, which belongs to the table it indexes.
TEST(PluginDatabase, AllowsTheIndexesSQLiteMakesForAnOwnedTable) {
    TemporaryDirectory directory;
    auto database = tests::ProductDatabase::open(directory);
    const std::vector<std::vector<std::string>> items{{"CREATE TABLE web_server__items(id TEXT PRIMARY KEY, name TEXT NOT NULL UNIQUE, link TEXT) STRICT", "CREATE UNIQUE INDEX web_server__items_link_index ON web_server__items(link) WHERE link IS NOT NULL"}};

    ASSERT_TRUE(PluginDatabase::migrate(database, "web-server", items).hasValue());
    ASSERT_TRUE(PluginDatabase::run(database, "web-server", "INSERT INTO web_server__items(id, name) VALUES(?, ?) ON CONFLICT(id) DO UPDATE SET name = excluded.name", {"first", "kept"}).hasValue());
    ASSERT_TRUE(PluginDatabase::run(database, "web-server", "INSERT INTO web_server__items(id, name) VALUES(?, ?) ON CONFLICT(id) DO UPDATE SET name = excluded.name", {"first", "changed"}).hasValue());
    EXPECT_EQ(PluginDatabase::query(database, "web-server", "SELECT name FROM web_server__items", {}).value().at(0)["name"], "changed");

    // Another plugin still reaches neither the table nor an automatic index of its own name on that table, and the owner cannot drop an index SQLite made.
    EXPECT_FALSE(PluginDatabase::migrate(database, "other", {{"CREATE TABLE web_server__stolen(id TEXT PRIMARY KEY)"}}).hasValue());
    EXPECT_FALSE(PluginDatabase::migrate(database, "other", {{"CREATE INDEX sqlite_autoindex_web_server__items_9 ON web_server__items(name)"}}).hasValue());
    EXPECT_FALSE(PluginDatabase::migrate(database, "web-server", {items[0], {"DROP INDEX sqlite_autoindex_web_server__items_1"}}).hasValue());
}

// A table created in other letters than the lowercase prefix of its plugin is never owned, so erasing the plugin or comparing its schema never misses it.
TEST(PluginDatabase, RefusesATableNamedInOtherLetters) {
    TemporaryDirectory directory;
    auto database = tests::ProductDatabase::open(directory);

    EXPECT_FALSE(PluginDatabase::migrate(database, "sample", {{"CREATE TABLE SAMPLE__items(name TEXT)"}}).hasValue());
    EXPECT_FALSE(PluginDatabase::migrate(database, "sample", {{"CREATE TABLE Sample__items(name TEXT)"}}).hasValue());
    ASSERT_TRUE(PluginDatabase::migrate(database, "sample", {{"CREATE TABLE sample__items(name TEXT)"}}).hasValue());
    EXPECT_TRUE(PluginDatabase::query(database, "sample", "SELECT name FROM SAMPLE__ITEMS", {}).hasValue());
}

// A foreign key of a plugin reaches only its own tables, so no plugin can keep another from deleting or updating its rows.
TEST(PluginDatabase, RefusesAForeignKeyToTheTableOfAnotherPlugin) {
    TemporaryDirectory directory;
    auto database = tests::ProductDatabase::open(directory);
    ASSERT_TRUE(PluginDatabase::migrate(database, "victim", {{"CREATE TABLE victim__items(id INTEGER PRIMARY KEY)"}}).hasValue());
    ASSERT_TRUE(PluginDatabase::run(database, "victim", "INSERT INTO victim__items(id) VALUES(1)", {}).hasValue());

    const auto refused = PluginDatabase::migrate(database, "hijacker", {{"CREATE TABLE hijacker__links(item INTEGER REFERENCES VICTIM__items(id) ON DELETE CASCADE)"}});
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.error().code, "database_foreign_key_foreign");
    EXPECT_TRUE(database.query("SELECT name FROM sqlite_master WHERE name = 'hijacker__links'").value().empty());
    EXPECT_TRUE(PluginDatabase::run(database, "victim", "DELETE FROM victim__items", {}).hasValue());
    ASSERT_TRUE(PluginDatabase::migrate(database, "owner", {{"CREATE TABLE owner__parents(id INTEGER PRIMARY KEY)", "CREATE TABLE owner__children(parent INTEGER REFERENCES owner__parents(id) ON DELETE CASCADE)"}}).hasValue());
}

// Changing a column in a migration reaches the schema only through SQLite itself, so a column can be renamed or dropped in place.
TEST(PluginDatabase, RenamesAndDropsColumnsInAMigration) {
    TemporaryDirectory directory;
    auto database = tests::ProductDatabase::open(directory);
    const std::vector<std::vector<std::string>> migrations{{"CREATE TABLE sample__items(name TEXT, extra TEXT, legacy TEXT)"}, {"ALTER TABLE sample__items RENAME COLUMN extra TO detail", "ALTER TABLE sample__items DROP COLUMN legacy"}};

    const auto migrated = PluginDatabase::migrate(database, "sample", migrations);
    ASSERT_TRUE(migrated.hasValue()) << migrated.error().code << " " << migrated.error().detail;
    EXPECT_EQ(migrated.value(), 2);
    EXPECT_TRUE(PluginDatabase::query(database, "sample", "SELECT name, detail FROM sample__items", {}).hasValue());
    EXPECT_FALSE(PluginDatabase::query(database, "sample", "SELECT legacy FROM sample__items", {}).hasValue());
}

// A statement that makes SQLite roll the transaction back by itself answers its own failure, not a failure of the rollback.
TEST(PluginDatabase, AnswersTheFailureSQLiteRolledBackItself) {
    TemporaryDirectory directory;
    auto database = tests::ProductDatabase::open(directory);
    ASSERT_TRUE(PluginDatabase::migrate(database, "sample", {{"CREATE TABLE sample__items(name TEXT UNIQUE)"}}).hasValue());

    const std::vector<Statement> statements{{"INSERT INTO sample__items(name) VALUES('twice')", {}}, {"INSERT OR ROLLBACK INTO sample__items(name) VALUES('twice')", {}}};
    const auto refused = PluginDatabase::transaction(database, "sample", statements);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.error().code, "database_statement_failed");
    EXPECT_NE(refused.error().detail.find("UNIQUE"), std::string::npos);
    EXPECT_TRUE(PluginDatabase::query(database, "sample", "SELECT name FROM sample__items", {}).value().empty());
    EXPECT_TRUE(PluginDatabase::run(database, "sample", "INSERT INTO sample__items(name) VALUES('after')", {}).hasValue());
}

// A statement of a plugin that runs past its bound is interrupted, and a statement still running as the executor shuts down is interrupted so quitting never waits for it.
TEST(PluginDatabase, InterruptsAStatementThatNeverEnds) {
    TemporaryDirectory directory;
    auto database = tests::ProductDatabase::open(directory);
    ASSERT_TRUE(PluginDatabase::migrate(database, "sample", {{"CREATE TABLE sample__items(name TEXT)"}}).hasValue());
    const std::string endless = "WITH RECURSIVE sample__counter(n) AS (SELECT 1 UNION ALL SELECT n + 1 FROM sample__counter) SELECT count(*) AS total FROM sample__counter";

    const auto started = std::chrono::steady_clock::now();
    const auto bounded = PluginDatabase::query(database, "sample", endless, {});
    ASSERT_FALSE(bounded.hasValue());
    EXPECT_EQ(bounded.error().code, "database_interrupted");
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(20));

    execution::MainThreadQueue mainThread;
    std::optional<Result<Rows>> answered;
    DatabaseExecutor executor(std::move(database), mainThread);
    // clang-format off
    executor.submit<Rows>([&endless](Database& connection) { return PluginDatabase::query(connection, "sample", endless, {}); }, [&answered](Result<Rows> result) { answered = std::move(result); });
    // clang-format on
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto closing = std::chrono::steady_clock::now();
    executor.shutdown();
    mainThread.drain();

    EXPECT_LT(std::chrono::steady_clock::now() - closing, std::chrono::seconds(5));
    ASSERT_TRUE(answered.has_value());
    EXPECT_EQ(answered->error().code, "database_interrupted");
}

TEST(PluginDatabase, AppliesEachMigrationOnceAndRollsBackAFailedTransaction) {
    TemporaryDirectory directory;
    auto database = tests::ProductDatabase::open(directory);
    const std::vector<std::vector<std::string>> first{{"CREATE TABLE sample__items(name TEXT NOT NULL)"}};
    const std::vector<std::vector<std::string>> second{{"CREATE TABLE sample__items(name TEXT NOT NULL)"}, {"ALTER TABLE sample__items ADD COLUMN score INTEGER"}};

    EXPECT_EQ(PluginDatabase::migrate(database, "sample", first).value(), 1);
    EXPECT_EQ(PluginDatabase::migrate(database, "sample", first).value(), 1);
    EXPECT_EQ(PluginDatabase::migrate(database, "sample", second).value(), 2);
    EXPECT_EQ(PluginDatabase::migrate(database, "sample", first).error().code, "database_schema_newer");

    const std::vector<Statement> statements{{"INSERT INTO sample__items(name, score) VALUES(?, ?)", {"kept", 1}}, {"INSERT INTO sample__items(name) VALUES(NULL)", {}}};
    EXPECT_FALSE(PluginDatabase::transaction(database, "sample", statements).hasValue());
    EXPECT_TRUE(PluginDatabase::query(database, "sample", "SELECT name FROM sample__items", {}).value().empty());
}

// A table an earlier build of a plugin wrote is named at the start when the one migration creating it changed since, while a schema that matches passes however it was spelled.
TEST(PluginDatabase, RefusesTablesTheMigrationsOfThePluginNoLongerCreate) {
    TemporaryDirectory directory;
    auto database = tests::ProductDatabase::open(directory);
    const std::vector<std::vector<std::string>> written{{"CREATE TABLE sample__items(name TEXT NOT NULL)", "CREATE INDEX sample__items_name ON sample__items(name)"}};
    const std::vector<std::vector<std::string>> respelled{{"CREATE TABLE  sample__items( name TEXT NOT NULL )", "CREATE INDEX sample__items_name ON sample__items(name)"}};
    const std::vector<std::vector<std::string>> changed{{"CREATE TABLE sample__items(name TEXT NOT NULL, score INTEGER NOT NULL)", "CREATE INDEX sample__items_name ON sample__items(name)"}};
    const std::vector<std::vector<std::string>> indexed{{"CREATE TABLE sample__items(name TEXT NOT NULL)", "CREATE INDEX sample__items_name ON sample__items(name)", "CREATE INDEX sample__items_order ON sample__items(name DESC)"}};

    ASSERT_TRUE(PluginDatabase::migrate(database, "sample", written).hasValue());
    EXPECT_TRUE(PluginDatabase::migrate(database, "sample", respelled).hasValue());

    const auto refused = PluginDatabase::migrate(database, "sample", changed);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.error().code, "database_schema_mismatch");
    EXPECT_EQ(refused.error().detail, "sample__items");
    EXPECT_EQ(PluginDatabase::migrate(database, "sample", indexed).error().detail, "sample__items_order");
}

// A table, an index, a view or a trigger under the prefix of a plugin that its migrations do not create refuses the plugin by name.
TEST(PluginDatabase, RefusesStoredObjectsTheMigrationsDoNotCreate) {
    TemporaryDirectory directory;
    auto database = tests::ProductDatabase::open(directory);
    const std::vector<std::vector<std::string>> migrations{{"CREATE TABLE sample__items(id INTEGER PRIMARY KEY)"}};
    ASSERT_TRUE(PluginDatabase::migrate(database, "sample", migrations).hasValue());
    ASSERT_TRUE(database.executeScript("CREATE TABLE sample__leftover(id INTEGER);").hasValue());

    const auto refused = PluginDatabase::migrate(database, "sample", migrations);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.error().code, "database_schema_mismatch");
    EXPECT_EQ(refused.error().detail, "sample__leftover");
}

// A plugin keeps views and triggers of its own tables, and erasing it drops its views and tables with their indexes and triggers, its schema version and every preference document it keeps, and nothing of another plugin.
TEST(PluginDatabase, ErasesEveryStoredObjectOfOnePlugin) {
    TemporaryDirectory directory;
    auto database = tests::ProductDatabase::open(directory);
    const auto migrated = PluginDatabase::migrate(database, "sample", {{"CREATE TABLE sample__items(id INTEGER PRIMARY KEY, name TEXT)", "CREATE TABLE sample__log(name TEXT)", "CREATE INDEX sample__items_name ON sample__items(name)", "CREATE VIEW sample__names AS SELECT name FROM sample__items", "CREATE TRIGGER sample__logged AFTER INSERT ON sample__items BEGIN INSERT INTO sample__log(name) VALUES(new.name); END"}});
    ASSERT_TRUE(migrated.hasValue()) << migrated.error().code << " " << migrated.error().detail;
    ASSERT_TRUE(PluginDatabase::run(database, "sample", "INSERT INTO sample__items(name) VALUES('kept')", {}).hasValue());
    EXPECT_EQ(PluginDatabase::query(database, "sample", "SELECT name FROM sample__names", {}).value().size(), 1U);
    EXPECT_EQ(PluginDatabase::query(database, "sample", "SELECT name FROM sample__log", {}).value().size(), 1U);
    EXPECT_FALSE(PluginDatabase::run(database, "sample", "CREATE VIEW other__names AS SELECT name FROM sample__items", {}).hasValue());

    // A view and a trigger are authorized when a statement of their plugin reads or fires them, so one reaching beyond the plugin, as one left by another program, refuses that statement and reads or changes nothing.
    ASSERT_TRUE(database.executeScript("CREATE VIEW sample__foreign AS SELECT owner FROM core_settings;").hasValue());
    EXPECT_FALSE(PluginDatabase::query(database, "sample", "SELECT owner FROM sample__foreign", {}).hasValue());
    ASSERT_TRUE(database.executeScript("CREATE TRIGGER sample__leak AFTER UPDATE ON sample__items BEGIN DELETE FROM core_settings; END;").hasValue());
    ASSERT_TRUE(PreferenceStore::store(database, "guarded", {{"kept", true}}).hasValue());
    EXPECT_FALSE(PluginDatabase::run(database, "sample", "UPDATE sample__items SET name = 'changed'", {}).hasValue());
    EXPECT_TRUE(PreferenceStore::load(database).value().documents.contains("guarded"));
    ASSERT_TRUE(PluginDatabase::migrate(database, "sample-two", {{"CREATE TABLE sample_two__items(id INTEGER PRIMARY KEY)"}}).hasValue());

    for (const auto* owner : {"sample", "sample:window", "sample-two", "workpane"}) {
        ASSERT_TRUE(PreferenceStore::store(database, owner, {{"kept", true}}).hasValue());
    }

    EXPECT_EQ(PluginDatabase::plugins(database).value(), (std::vector<std::string>{"sample", "sample-two"}));
    ASSERT_TRUE(PluginDatabase::erase(database, "sample").hasValue());

    const auto objects = database.query("SELECT name FROM sqlite_master WHERE name LIKE 'sample%' ORDER BY name");
    ASSERT_TRUE(objects.hasValue());
    ASSERT_EQ(objects.value().size(), 1U);
    EXPECT_EQ(objects.value()[0]["name"], "sample_two__items");
    EXPECT_EQ(PluginDatabase::plugins(database).value(), (std::vector<std::string>{"sample-two"}));

    const auto loaded = PreferenceStore::load(database);
    ASSERT_TRUE(loaded.hasValue());
    EXPECT_FALSE(loaded.value().documents.contains("sample"));
    EXPECT_FALSE(loaded.value().documents.contains("sample:window"));
    EXPECT_TRUE(loaded.value().documents.contains("sample-two"));
    EXPECT_TRUE(loaded.value().documents.contains("workpane"));
    EXPECT_TRUE(PluginDatabase::migrate(database, "sample", {{"CREATE TABLE sample__items(id INTEGER PRIMARY KEY)"}}).hasValue());
}

// A stored document that is not a JSON object is left out and named, and every other owner keeps its preferences.
TEST(PreferenceStore, LeavesOutADamagedDocumentAndKeepsTheOthers) {
    TemporaryDirectory directory;
    auto database = tests::ProductDatabase::open(directory);
    ASSERT_TRUE(PreferenceStore::store(database, "sample", {{"kept", true}}).hasValue());
    ASSERT_TRUE(database.run("INSERT INTO core_settings(owner, document) VALUES('damaged', '[1, 2')", {}).hasValue());
    ASSERT_TRUE(database.run("INSERT INTO core_settings(owner, document) VALUES('listed', '[1, 2]')", {}).hasValue());

    const auto loaded = PreferenceStore::load(database);
    ASSERT_TRUE(loaded.hasValue());
    EXPECT_EQ(loaded.value().documents.size(), 1U);
    EXPECT_EQ(loaded.value().documents.at("sample")["kept"], true);
    EXPECT_EQ(loaded.value().discarded, (std::vector<std::string>{"damaged", "listed"}));
}

TEST(ConfigurationTransfer, ExportsValidatesAndStagesAConfiguration) {
    TemporaryDirectory directory;
    TemporaryDirectory target;
    const std::filesystem::path exported = directory.path() / "exported.sqlite3";
    {
        auto database = tests::ProductDatabase::open(directory);
        ASSERT_TRUE(PreferenceStore::store(database, "workpane", {{"language", "pt"}}).hasValue());
        ASSERT_TRUE(ConfigurationTransfer::exportTo(database, exported).hasValue());
    }

    EXPECT_TRUE(ConfigurationTransfer::validate(exported, {}).hasValue());
    ASSERT_TRUE(ConfigurationTransfer::stage(exported, target.path(), {}).hasValue());

    auto staged = Database::open(target.path() / DatabaseBootstrap::stagedImportName, true);
    ASSERT_TRUE(staged.hasValue());
    auto documents = PreferenceStore::load(staged.value());
    ASSERT_TRUE(documents.hasValue());
    EXPECT_EQ(documents.value().documents.at("workpane")["language"], "pt");

    const std::filesystem::path garbage = directory.path() / "garbage.sqlite3";
    std::ofstream(garbage, std::ios::binary) << "garbage";
    EXPECT_EQ(ConfigurationTransfer::validate(garbage, {}).error().code, "configuration_import_invalid");
}

// An export that cannot be written leaves the file it would replace as it was, and one that succeeds replaces it whole without leaving its unfinished copy behind.
TEST(ConfigurationTransfer, KeepsThePreviousExportWhenAnExportFails) {
    TemporaryDirectory directory;
    const std::filesystem::path destination = directory.path() / "exported.sqlite3";
    std::filesystem::path partial = destination;
    partial += ".partial";
    auto database = tests::ProductDatabase::open(directory);
    std::ofstream(destination, std::ios::binary) << "previous";
    std::filesystem::create_directories(partial / "held");

    EXPECT_EQ(ConfigurationTransfer::exportTo(database, destination).error().code, "configuration_export_failed");
    std::ifstream kept(destination, std::ios::binary);
    EXPECT_EQ(std::string((std::istreambuf_iterator<char>(kept)), std::istreambuf_iterator<char>()), "previous");

    std::filesystem::remove_all(partial);
    ASSERT_TRUE(ConfigurationTransfer::exportTo(database, destination).hasValue());
    EXPECT_FALSE(std::filesystem::exists(partial));
    EXPECT_TRUE(Database::open(destination, true).hasValue());
    EXPECT_TRUE(CoreSchema::validate(Database::open(destination, true).value()).hasValue());
}

// Exporting over the database the product is using, or over one of its journals, is refused and leaves the database where it was.
TEST(ConfigurationTransfer, RefusesToExportOverTheLiveDatabase) {
    TemporaryDirectory directory;
    auto database = tests::ProductDatabase::open(directory);
    ASSERT_TRUE(PreferenceStore::store(database, "workpane", {{"language", "pt"}}).hasValue());
    std::filesystem::path journal = database.file();
    journal += "-wal";

    EXPECT_EQ(ConfigurationTransfer::exportTo(database, database.file()).error().code, "configuration_export_live");
    EXPECT_EQ(ConfigurationTransfer::exportTo(database, directory.path() / "." / database.file().filename()).error().code, "configuration_export_live");
    EXPECT_EQ(ConfigurationTransfer::exportTo(database, journal).error().code, "configuration_export_live");
    EXPECT_TRUE(std::filesystem::exists(database.file()));
    EXPECT_EQ(PreferenceStore::load(database).value().documents.at("workpane")["language"], "pt");
}

// A source still holding commits in its write ahead log is staged with them, because the staged file is a snapshot rather than a copy of the main file.
TEST(ConfigurationTransfer, StagesTheCommitsStillInTheWriteAheadLog) {
    TemporaryDirectory source;
    TemporaryDirectory target;
    auto writing = tests::ProductDatabase::open(source);
    ASSERT_TRUE(writing.run("PRAGMA wal_autocheckpoint = 0").hasValue());
    ASSERT_TRUE(PreferenceStore::store(writing, "workpane", {{"language", "pt"}}).hasValue());

    ASSERT_TRUE(ConfigurationTransfer::stage(writing.file(), target.path(), {}).hasValue());

    auto staged = Database::open(target.path() / DatabaseBootstrap::stagedImportName, true);
    ASSERT_TRUE(staged.hasValue());
    EXPECT_EQ(PreferenceStore::load(staged.value()).value().documents.at("workpane")["language"], "pt");
}

// A plugin schema newer than the migrations the installed plugin declares could not be read after the restart, so the import is refused by the name of that plugin.
// The declared migrations decide, so a plugin that never ran here or one turned off and updated since takes a schema its code can read, and a plugin that is not installed takes its tables as they are.
TEST(ConfigurationTransfer, RefusesAPluginSchemaNewerThanTheInstalledPlugin) {
    TemporaryDirectory source;
    const std::vector<std::vector<std::string>> two{{"CREATE TABLE sample__items(name TEXT)"}, {"ALTER TABLE sample__items ADD COLUMN score INTEGER"}};
    auto newer = tests::ProductDatabase::open(source);
    ASSERT_TRUE(PluginDatabase::migrate(newer, "sample", two).hasValue());
    ASSERT_TRUE(PluginDatabase::migrate(newer, "other", {{"CREATE TABLE other__items(name TEXT)"}}).hasValue());

    const auto refused = ConfigurationTransfer::validate(newer.file(), {{"sample", 1}, {"other", 1}});

    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.error().code, "configuration_import_newer");
    EXPECT_EQ(refused.error().detail, "sample");
    EXPECT_TRUE(ConfigurationTransfer::validate(newer.file(), {{"sample", 2}, {"other", 1}}).hasValue());
    EXPECT_TRUE(ConfigurationTransfer::validate(newer.file(), {{"sample", 3}}).hasValue());
    EXPECT_TRUE(ConfigurationTransfer::validate(newer.file(), {}).hasValue());
}

TEST(InstanceLock, AllowsOneInstancePerDataDirectory) {
    TemporaryDirectory directory;
    {
        auto first = InstanceLock::acquire(directory.path());
        ASSERT_TRUE(first.hasValue());

        const auto second = InstanceLock::acquire(directory.path());
        ASSERT_FALSE(second.hasValue());
        EXPECT_EQ(second.error().code, "instance_already_running");
    }

    EXPECT_TRUE(InstanceLock::acquire(directory.path()).hasValue());
}

// A lock released cleanly leaves nothing to recover, and a lock file still marked running says the previous instance did not stop cleanly.
TEST(InstanceLock, KnowsWhetherThePreviousInstanceStoppedCleanly) {
    TemporaryDirectory directory;

    {
        const auto first = InstanceLock::acquire(directory.path());
        ASSERT_TRUE(first.hasValue());
        EXPECT_FALSE(first.value().recovered());
    }

    const auto clean = InstanceLock::acquire(directory.path());
    ASSERT_TRUE(clean.hasValue());
    EXPECT_FALSE(clean.value().recovered());
}

TEST(InstanceLock, RecoversFromALockLeftRunning) {
    TemporaryDirectory directory;
    std::ofstream(directory.path() / "workpane.lock") << "running";

    const auto recovered = InstanceLock::acquire(directory.path());
    ASSERT_TRUE(recovered.hasValue());
    EXPECT_TRUE(recovered.value().recovered());
}

} // namespace workpane::persistence
