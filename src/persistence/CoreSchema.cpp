#include "persistence/CoreSchema.h"

#include "persistence/SchemaTextHelper.h"
#include "time/TimestampHelper.h"

#include <algorithm>
#include <array>
#include <string>
#include <system_error>

namespace workpane::persistence {

Result<std::int64_t> CoreSchema::userVersion(Database& database) {
    auto rows = database.query("PRAGMA user_version");

    if (!rows.hasValue()) {
        return Result<std::int64_t>::failure(rows.error());
    }

    if (rows.value().size() != 1 || !rows.value().front().contains("user_version") || !rows.value().front()["user_version"].is_number_integer()) {
        return Result<std::int64_t>::failure({"database_version_unreadable", "The database version could not be read", database.file().string()});
    }

    return Result<std::int64_t>::success(rows.value().front()["user_version"].get<std::int64_t>());
}

// Creates the complete schema on an empty database and refuses one this version cannot read.
Result<void> CoreSchema::ensure(Database& database) {
    const auto stored = userVersion(database);

    if (!stored.hasValue()) {
        return Result<void>::failure(stored.error());
    }

    if (stored.value() > version) {
        return Result<void>::failure({"database_version_newer", "The database was written by a newer version of the product", std::to_string(stored.value())});
    }

    if (stored.value() != 0 && stored.value() != version) {
        return Result<void>::failure({"database_version_unknown", "The database carries a version this product never wrote", std::to_string(stored.value())});
    }

    if (stored.value() == version) {
        return validate(database);
    }

    // A database without a version is new only when it carries nothing at all, because tables without a version were not written by this product.
    auto objects = database.query("SELECT count(*) AS total FROM sqlite_master");

    if (!objects.hasValue()) {
        return Result<void>::failure(objects.error());
    }

    if (objects.value().front()["total"].get<std::int64_t>() != 0) {
        return Result<void>::failure({"database_schema_unknown", "The database carries tables without a version", database.file().string()});
    }

    const std::string script = "BEGIN IMMEDIATE; " + std::string(coreSettingsTable) + "; " + std::string(corePluginSchemasTable) + "; PRAGMA user_version = " + std::to_string(version) + "; COMMIT;";

    if (const auto created = database.executeScript(script); !created.hasValue()) {
        const auto rolledBack = database.executeScript("ROLLBACK;");
        return rolledBack.hasValue() ? created : Result<void>::failure(rolledBack.error());
    }

    return validate(database);
}

Result<void> CoreSchema::validate(Database& database) {
    auto rows = database.query("SELECT name, sql FROM sqlite_master WHERE type = 'table' AND name IN ('core_settings', 'core_plugin_schemas') ORDER BY name");

    if (!rows.hasValue()) {
        return Result<void>::failure(rows.error());
    }

    const std::array<std::pair<std::string_view, std::string_view>, 2> expected{{{"core_plugin_schemas", corePluginSchemasTable}, {"core_settings", coreSettingsTable}}};

    if (rows.value().size() != expected.size()) {
        return Result<void>::failure({"database_schema_invalid", "The core tables are missing", database.file().string()});
    }

    for (std::size_t index = 0; index < expected.size(); ++index) {
        const Row& row = rows.value()[index];

        if (!row["name"].is_string() || row["name"].get_ref<const std::string&>() != expected[index].first || !row["sql"].is_string() || SchemaTextHelper::normalized(row["sql"].get<std::string>()) != SchemaTextHelper::normalized(expected[index].second)) {
            return Result<void>::failure({"database_schema_invalid", "A core table differs from the one this version declares", std::string(expected[index].first)});
        }
    }

    return Result<void>::success();
}

} // namespace workpane::persistence
