#include "persistence/ConfigurationTransfer.h"

#include "persistence/CoreSchema.h"
#include "persistence/DatabaseBootstrap.h"
#include "platform/FileReplacement.h"
#include "platform/PathText.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <system_error>

namespace workpane::persistence {

// A snapshot over the live database or one of its journals would delete the file the product is using, so such a destination is refused.
Result<void> ConfigurationTransfer::exportTo(Database& database, const std::filesystem::path& destination) {
    if (touches(database, destination)) {
        return Result<void>::failure({"configuration_export_live", "The configuration cannot be exported over the database the product is using", destination.string()});
    }

    return snapshot(database, destination);
}

Result<void> ConfigurationTransfer::validate(const std::filesystem::path& source, const SchemaVersions& installed) {
    auto opened = Database::open(source, true);

    if (!opened.hasValue()) {
        return Result<void>::failure({"configuration_import_invalid", "The chosen file is not a readable database", opened.error().detail});
    }

    Database& database = opened.value();
    auto integrity = database.query("PRAGMA integrity_check");

    if (!integrity.hasValue() || integrity.value().size() != 1 || integrity.value().front()["integrity_check"] != "ok") {
        return Result<void>::failure({"configuration_import_invalid", "The chosen database failed its integrity check", source.string()});
    }

    if (const auto schema = CoreSchema::validate(database); !schema.hasValue()) {
        return Result<void>::failure({"configuration_import_invalid", "The chosen database is not a configuration of this version", schema.error().detail});
    }

    auto version = database.query("PRAGMA user_version");

    if (!version.hasValue() || version.value().front()["user_version"] != CoreSchema::version) {
        return Result<void>::failure({"configuration_import_invalid", "The chosen database carries another schema version", source.string()});
    }

    return pluginsFit(database, installed);
}

// The staged file is a snapshot of the chosen one, so commits still waiting in its write ahead log travel with it.
Result<void> ConfigurationTransfer::stage(const std::filesystem::path& source, const std::filesystem::path& dataDirectory, const SchemaVersions& installed) {
    if (const auto proven = validate(source, installed); !proven.hasValue()) {
        return proven;
    }

    auto opened = Database::open(source, true);

    if (!opened.hasValue()) {
        return Result<void>::failure({"configuration_import_invalid", "The chosen file is not a readable database", opened.error().detail});
    }

    const auto staged = snapshot(opened.value(), dataDirectory / DatabaseBootstrap::stagedImportName);
    return staged.hasValue() ? staged : Result<void>::failure({"configuration_import_failed", "The configuration could not be staged for the next start", staged.error().detail});
}

// The live database is compared with the destination as a file, so another spelling of the same path is still recognised.
bool ConfigurationTransfer::touches(const Database& database, const std::filesystem::path& destination) {
    for (const std::string_view suffix : std::array<std::string_view, 4>{"", "-wal", "-shm", "-journal"}) {
        std::error_code error;
        std::filesystem::path file = database.file();
        file += suffix;

        if (std::filesystem::equivalent(destination, file, error)) {
            return true;
        }
    }

    return false;
}

// The snapshot is written beside the destination and takes its place only once it is whole, so a failed or interrupted snapshot never leaves half a file or loses the one it replaces.
Result<void> ConfigurationTransfer::snapshot(Database& database, const std::filesystem::path& destination) {
    std::filesystem::path partial = destination;
    partial += ".partial";
    std::error_code error;
    std::filesystem::remove(partial, error);

    if (error) {
        return Result<void>::failure({"configuration_export_failed", "An unfinished snapshot beside the destination could not be removed", partial.string() + ": " + error.message()});
    }

    const auto written = database.run("VACUUM INTO ?", {platform::PathText::utf8(partial)});

    if (!written.hasValue()) {
        std::filesystem::remove(partial, error);
        return Result<void>::failure({"configuration_export_failed", "The configuration could not be exported", written.error().detail});
    }

    if (const std::error_code replaced = platform::FileReplacement::replace(partial, destination); replaced) {
        std::filesystem::remove(partial, error);
        return Result<void>::failure({"configuration_export_failed", "The snapshot could not take the place of the destination", destination.string() + ": " + replaced.message()});
    }

    return Result<void>::success();
}

// A plugin schema written by a newer version of a plugin than the one installed here could not be read after the restart, so the import is refused by that plugin.
// The installed version of a plugin is the number of migrations it declares, whether or not it ever ran here, and a plugin that is not installed takes its tables as they are.
Result<void> ConfigurationTransfer::pluginsFit(Database& source, const SchemaVersions& installed) {
    auto carried = source.query("SELECT plugin_id, version FROM core_plugin_schemas");

    if (!carried.hasValue()) {
        return Result<void>::failure({"configuration_import_invalid", "The plugin schemas of the chosen database could not be read", carried.error().detail});
    }

    for (const auto& row : carried.value()) {
        const auto& plugin = row["plugin_id"].get_ref<const std::string&>();
        const auto found = installed.find(plugin);

        if (found != installed.end() && row["version"].get<std::int64_t>() > found->second) {
            return Result<void>::failure({"configuration_import_newer", "The chosen database carries a plugin schema newer than the plugin installed here", plugin});
        }
    }

    return Result<void>::success();
}

} // namespace workpane::persistence
