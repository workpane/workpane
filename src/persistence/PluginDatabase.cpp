#include "persistence/PluginDatabase.h"

#include "persistence/PluginAuthorizer.h"
#include "persistence/PreferenceStore.h"
#include "persistence/SchemaTextHelper.h"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <utility>

namespace workpane::persistence {

// SQLite rolls a transaction back by itself after some failures, such as a full disk or a statement that asks for it, so only an open transaction is rolled back and the caller learns what failed.
Result<void> PluginDatabase::rollback(Database& database, const Error& error) {
    if (sqlite3_get_autocommit(database.handle()) != 0) {
        return Result<void>::failure(error);
    }

    const auto rolledBack = database.executeScript("ROLLBACK;");
    return rolledBack.hasValue() ? Result<void>::failure(error) : Result<void>::failure(rolledBack.error());
}

// The prefix closes with two underscores, which no identifier writes, so the prefix of one plugin never starts the prefix of another.
std::string PluginDatabase::tablePrefix(std::string_view pluginId) {
    std::string prefix(pluginId);
    std::ranges::replace(prefix, '-', '_');

    return prefix + "__";
}

Result<Rows> PluginDatabase::query(Database& database, std::string_view pluginId, std::string_view sql, const std::vector<Value>& bindings) {
    const PluginAuthorizer authorizer(database, tablePrefix(pluginId), PluginAuthorizer::Purpose::Statement);
    return database.query(sql, bindings);
}

Result<Execution> PluginDatabase::run(Database& database, std::string_view pluginId, std::string_view sql, const std::vector<Value>& bindings) {
    const PluginAuthorizer authorizer(database, tablePrefix(pluginId), PluginAuthorizer::Purpose::Statement);
    return database.run(sql, bindings);
}

Result<Execution> PluginDatabase::change(Database& database, std::string_view pluginId, std::string_view sql) {
    const PluginAuthorizer authorizer(database, tablePrefix(pluginId), PluginAuthorizer::Purpose::Migration);
    return database.run(sql, {});
}

// A table of a plugin refers only to tables of the same plugin, so no plugin can keep another from deleting or updating its own rows.
Result<void> PluginDatabase::ownParents(Database& database, std::string_view pluginId) {
    const std::string prefix = tablePrefix(pluginId);
    auto rows = database.query("SELECT m.name AS owner, f.\"table\" AS parent FROM sqlite_master AS m JOIN pragma_foreign_key_list(m.name) AS f WHERE m.type = 'table' AND substr(m.name, 1, length(?)) = ?", {prefix, prefix});

    if (!rows.hasValue()) {
        return Result<void>::failure(rows.error());
    }

    for (const auto& row : rows.value()) {
        std::string parent = row["parent"].get<std::string>();
        // clang-format off
        std::ranges::transform(parent, parent.begin(), [](char character) { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); });
        // clang-format on

        if (!parent.starts_with(prefix)) {
            return Result<void>::failure({"database_foreign_key_foreign", "A table of a plugin refers to a table of another owner", row["owner"].get<std::string>() + " -> " + row["parent"].get<std::string>()});
        }
    }

    return Result<void>::success();
}

// Several statements apply together or not at all, because a plugin never opens a transaction of its own.
Result<std::vector<Execution>> PluginDatabase::transaction(Database& database, std::string_view pluginId, const std::vector<Statement>& statements) {
    if (const auto begun = database.executeScript("BEGIN IMMEDIATE;"); !begun.hasValue()) {
        return Result<std::vector<Execution>>::failure(begun.error());
    }

    std::vector<Execution> executions;

    for (const auto& statement : statements) {
        auto executed = run(database, pluginId, statement.sql, statement.bindings);

        if (!executed.hasValue()) {
            const auto rolledBack = rollback(database, executed.error());
            return Result<std::vector<Execution>>::failure(rolledBack.error());
        }

        executions.push_back(executed.value());
    }

    if (const auto committed = database.executeScript("COMMIT;"); !committed.hasValue()) {
        const auto rolledBack = rollback(database, committed.error());
        return Result<std::vector<Execution>>::failure(rolledBack.error());
    }

    return Result<std::vector<Execution>>::success(std::move(executions));
}

// Applies every declared migration after the stored version atomically and answers the version the schema reached.
Result<std::int64_t> PluginDatabase::migrate(Database& database, std::string_view pluginId, const std::vector<std::vector<std::string>>& migrations) {
    auto stored = database.query("SELECT version FROM core_plugin_schemas WHERE plugin_id = ?", {std::string(pluginId)});

    if (!stored.hasValue()) {
        return Result<std::int64_t>::failure(stored.error());
    }

    const std::int64_t current = stored.value().empty() ? 0 : stored.value().front()["version"].get<std::int64_t>();
    const auto declared = static_cast<std::int64_t>(migrations.size());

    // A schema written by a later version of the product is refused by name, so going back to an older build leaves it where it is.
    if (current > declared) {
        return Result<std::int64_t>::failure({"database_schema_newer", "A plugin schema was written by a newer version of the plugin", std::string(pluginId)});
    }

    for (std::int64_t version = current + 1; version <= declared; ++version) {
        if (const auto begun = database.executeScript("BEGIN IMMEDIATE;"); !begun.hasValue()) {
            return Result<std::int64_t>::failure(begun.error());
        }

        for (const auto& sql : migrations[static_cast<std::size_t>(version - 1)]) {
            if (const auto executed = change(database, pluginId, sql); !executed.hasValue()) {
                const auto rolledBack = rollback(database, executed.error());
                return Result<std::int64_t>::failure(rolledBack.error());
            }
        }

        if (const auto parents = ownParents(database, pluginId); !parents.hasValue()) {
            const auto rolledBack = rollback(database, parents.error());
            return Result<std::int64_t>::failure(rolledBack.error());
        }

        const auto recorded = database.run("INSERT INTO core_plugin_schemas(plugin_id, version) VALUES(?, ?) ON CONFLICT(plugin_id) DO UPDATE SET version = excluded.version", {std::string(pluginId), version});

        if (!recorded.hasValue()) {
            const auto rolledBack = rollback(database, recorded.error());
            return Result<std::int64_t>::failure(rolledBack.error());
        }

        if (const auto committed = database.executeScript("COMMIT;"); !committed.hasValue()) {
            const auto rolledBack = rollback(database, committed.error());
            return Result<std::int64_t>::failure(rolledBack.error());
        }
    }

    if (const auto verified = verify(database, pluginId, migrations); !verified.hasValue()) {
        return Result<std::int64_t>::failure(verified.error());
    }

    return Result<std::int64_t>::success(declared);
}

Result<std::map<std::string, std::string>> PluginDatabase::schema(Database& database, std::string_view pluginId) {
    const std::string prefix = tablePrefix(pluginId);
    auto rows = database.query("SELECT name, sql FROM sqlite_master WHERE sql IS NOT NULL AND substr(name, 1, length(?)) = ?", {prefix, prefix});

    if (!rows.hasValue()) {
        return Result<std::map<std::string, std::string>>::failure(rows.error());
    }

    std::map<std::string, std::string> definitions;

    for (const auto& row : rows.value()) {
        definitions.emplace(row["name"].get<std::string>(), SchemaTextHelper::normalized(row["sql"].get<std::string>()));
    }

    return Result<std::map<std::string, std::string>>::success(std::move(definitions));
}

// The tables a plugin keeps are compared both ways with the ones its migrations create in a database of its own in memory, so tables an earlier build wrote are named at the start instead of failing a statement later.
Result<void> PluginDatabase::verify(Database& database, std::string_view pluginId, const std::vector<std::vector<std::string>>& migrations) {
    auto scratch = Database::open(":memory:");

    if (!scratch.hasValue()) {
        return Result<void>::failure(scratch.error());
    }

    for (const auto& migration : migrations) {
        for (const auto& sql : migration) {
            if (const auto executed = change(scratch.value(), pluginId, sql); !executed.hasValue()) {
                return Result<void>::failure(executed.error());
            }
        }
    }

    const auto expected = schema(scratch.value(), pluginId);
    const auto stored = schema(database, pluginId);

    if (!expected.hasValue() || !stored.hasValue()) {
        return Result<void>::failure(!expected.hasValue() ? expected.error() : stored.error());
    }

    for (const auto& [name, definition] : expected.value()) {
        const auto found = stored.value().find(name);

        if (found == stored.value().end() || found->second != definition) {
            return Result<void>::failure({"database_schema_mismatch", "The stored tables of the plugin differ from the ones its migrations create", name});
        }
    }

    for (const auto& [name, definition] : stored.value()) {
        if (!expected.value().contains(name)) {
            return Result<void>::failure({"database_schema_mismatch", "The plugin keeps a table, an index, a view or a trigger its migrations do not create", name});
        }
    }

    return Result<void>::success();
}

// Answers every plugin whose tables carry a schema version, installed or not.
Result<std::vector<std::string>> PluginDatabase::plugins(Database& database) {
    auto rows = database.query("SELECT plugin_id FROM core_plugin_schemas ORDER BY plugin_id");

    if (!rows.hasValue()) {
        return Result<std::vector<std::string>>::failure(rows.error());
    }

    std::vector<std::string> found;

    for (const auto& row : rows.value()) {
        found.push_back(row["plugin_id"].get<std::string>());
    }

    return Result<std::vector<std::string>>::success(std::move(found));
}

// Drops every view and table under the prefix of a plugin, with the indexes and triggers they carry, its schema version and its preference documents, all together or not at all.
Result<void> PluginDatabase::erase(Database& database, std::string_view pluginId) {
    const std::string prefix = tablePrefix(pluginId);
    auto objects = database.query("SELECT type, name FROM sqlite_master WHERE type IN ('view', 'table') AND substr(name, 1, length(?)) = ? ORDER BY type DESC", {prefix, prefix});

    if (!objects.hasValue()) {
        return Result<void>::failure(objects.error());
    }

    if (const auto begun = database.executeScript("BEGIN IMMEDIATE;"); !begun.hasValue()) {
        return begun;
    }

    for (const auto& object : objects.value()) {
        const std::string name = object["name"].get<std::string>();
        std::string quoted;

        for (const char character : name) {
            quoted += character == '"' ? std::string("\"\"") : std::string(1, character);
        }

        const std::string kind = object["type"].get<std::string>() == "view" ? "VIEW" : "TABLE";

        if (const auto dropped = database.executeScript("DROP " + kind + " \"" + quoted + "\";"); !dropped.hasValue()) {
            return rollback(database, dropped.error());
        }
    }

    if (const auto forgotten = database.run("DELETE FROM core_plugin_schemas WHERE plugin_id = ?", {std::string(pluginId)}); !forgotten.hasValue()) {
        return rollback(database, forgotten.error());
    }

    if (const auto erased = PreferenceStore::erase(database, pluginId); !erased.hasValue()) {
        return rollback(database, erased.error());
    }

    if (const auto committed = database.executeScript("COMMIT;"); !committed.hasValue()) {
        return rollback(database, committed.error());
    }

    return Result<void>::success();
}

} // namespace workpane::persistence
