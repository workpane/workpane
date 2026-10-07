#include "persistence/DatabaseBootstrap.h"

#include "persistence/CoreSchema.h"
#include "time/TimestampHelper.h"

#include <algorithm>
#include <array>
#include <string>
#include <system_error>

namespace workpane::persistence {

// The suffix is appended to the native path, so a folder name outside the code page of Windows is never converted on the way.
std::filesystem::path DatabaseBootstrap::companion(const std::filesystem::path& file, const char* suffix) {
    std::filesystem::path named = file;
    named += suffix;

    return named;
}

// Only a refused or interrupted import that put the previous database back is a notice, and any other failure stops the start before a database opens.
Result<OpenedDatabase> DatabaseBootstrap::open(const std::filesystem::path& dataDirectory) {
    std::error_code error;
    std::filesystem::create_directories(dataDirectory, error);

    if (error) {
        return Result<OpenedDatabase>::failure({"data_directory_unwritable", "The data directory could not be created", dataDirectory.string() + ": " + error.message()});
    }

    const auto imported = applyStagedImport(dataDirectory);

    if (!imported.hasValue() && std::ranges::find(importNotices, imported.error().code) == importNotices.end()) {
        return Result<OpenedDatabase>::failure(imported.error());
    }

    const std::optional<Error> importFailure = imported.hasValue() ? std::nullopt : std::optional<Error>(imported.error());
    const std::filesystem::path file = dataDirectory / databaseName;
    auto opened = openValidated(file);

    if (opened.hasValue()) {
        return Result<OpenedDatabase>::success({std::move(opened.value()), std::nullopt, importFailure});
    }

    // Only a file that is not a database this version reads is set aside, while a failure of the machine stops the start so the next one reads the same file again.
    if (std::ranges::find(unreadableCodes, opened.error().code) == unreadableCodes.end()) {
        return Result<OpenedDatabase>::failure(opened.error());
    }

    std::string stamp = time::TimestampHelper::storedTimestamp(time::TimestampHelper::now());
    std::ranges::replace(stamp, ':', '-');
    const std::filesystem::path setAside = dataDirectory / (std::string(databaseName) + ".unreadable-" + stamp);

    if (const auto moved = moveDatabase(file, setAside); !moved.hasValue()) {
        return Result<OpenedDatabase>::failure(moved.error());
    }

    auto fresh = openValidated(file);

    if (!fresh.hasValue()) {
        return Result<OpenedDatabase>::failure(fresh.error());
    }

    return Result<OpenedDatabase>::success({std::move(fresh.value()), setAside, importFailure});
}

Result<Database> DatabaseBootstrap::openValidated(const std::filesystem::path& file) {
    auto opened = Database::open(file);

    if (!opened.hasValue()) {
        return opened;
    }

    if (const auto ensured = CoreSchema::ensure(opened.value()); !ensured.hasValue()) {
        return Result<Database>::failure(ensured.error());
    }

    return opened;
}

// A staged import replaces the database before anything opens it, and a replacement that does not validate puts the previous database back.
// A backup left without a staged import is an import interrupted after it took the place of the previous database, which stays only when it validates.
Result<void> DatabaseBootstrap::applyStagedImport(const std::filesystem::path& dataDirectory) {
    const std::filesystem::path file = dataDirectory / databaseName;
    const std::filesystem::path staged = dataDirectory / stagedImportName;
    const std::filesystem::path backup = dataDirectory / importBackupName;
    std::error_code error;
    const bool hasStaged = std::filesystem::exists(staged, error);
    const bool hasBackup = std::filesystem::exists(backup, error);

    if (hasBackup && !hasStaged) {
        if (std::filesystem::exists(file, error) && openValidated(file).hasValue()) {
            return removeDatabase(backup);
        }

        return restore(file, backup, {"configuration_import_interrupted", "An interrupted import was rolled back to the previous database", file.string()});
    }

    if (!hasStaged) {
        return Result<void>::success();
    }

    // A staged file that is not a whole configuration of this version, as one cut short while it was written, is dropped before the database of the reader moves.
    if (!whole(staged)) {
        if (const auto removed = removeDatabase(staged); !removed.hasValue()) {
            return removed;
        }

        return hasBackup ? restore(file, backup, {"configuration_import_rejected", rejectedMessage, staged.string()}) : Result<void>::failure({"configuration_import_rejected", rejectedMessage, staged.string()});
    }

    if (std::filesystem::exists(file, error) && !hasBackup) {
        if (const auto moved = moveDatabase(file, backup); !moved.hasValue()) {
            return moved;
        }
    }

    if (const auto placed = moveDatabase(staged, file); !placed.hasValue()) {
        return placed;
    }

    if (openValidated(file).hasValue()) {
        return removeDatabase(backup);
    }

    return restore(file, backup, {"configuration_import_rejected", rejectedMessage, file.string()});
}

// A staged configuration is whole when it opens without being created, carries the schema version and the core tables of this version and passes its quick check.
bool DatabaseBootstrap::whole(const std::filesystem::path& staged) {
    auto opened = Database::open(staged, true);

    if (!opened.hasValue()) {
        return false;
    }

    Database& database = opened.value();
    const auto version = database.query("PRAGMA user_version");

    if (!version.hasValue() || version.value().size() != 1 || version.value().front()["user_version"] != CoreSchema::version || !CoreSchema::validate(database).hasValue()) {
        return false;
    }

    const auto checked = database.query("PRAGMA quick_check");

    return checked.hasValue() && checked.value().size() == 1 && checked.value().front()["quick_check"] == "ok";
}

// The rejected database is removed before the backup is restored, so a failure in between never applies it again on the next start, and the notice is answered only once the previous database is back.
Result<void> DatabaseBootstrap::restore(const std::filesystem::path& file, const std::filesystem::path& backup, Error notice) {
    if (const auto removed = removeDatabase(file); !removed.hasValue()) {
        return removed;
    }

    if (const auto restored = moveDatabase(backup, file); !restored.hasValue()) {
        return restored;
    }

    return Result<void>::failure(std::move(notice));
}

// A database and its write-ahead companions move together, because what was committed last may only be in the log.
Result<void> DatabaseBootstrap::moveDatabase(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code error;

    if (const auto removed = removeDatabase(to); !removed.hasValue()) {
        return removed;
    }

    std::filesystem::rename(from, to, error);

    if (error) {
        return Result<void>::failure({"database_move_failed", "A database file could not be moved", from.string() + ": " + error.message()});
    }

    for (const char* suffix : companionSuffixes) {
        const std::filesystem::path source = companion(from, suffix);

        if (!std::filesystem::exists(source, error)) {
            continue;
        }

        std::filesystem::rename(source, companion(to, suffix), error);

        if (error) {
            return Result<void>::failure({"database_move_failed", "A database log could not be moved", source.string() + ": " + error.message()});
        }
    }

    return Result<void>::success();
}

Result<void> DatabaseBootstrap::removeDatabase(const std::filesystem::path& file) {
    std::error_code error;

    for (const auto& target : {file, companion(file, companionSuffixes[0]), companion(file, companionSuffixes[1])}) {
        std::filesystem::remove(target, error);

        if (error) {
            return Result<void>::failure({"database_remove_failed", "A database file could not be removed", target.string() + ": " + error.message()});
        }
    }

    return Result<void>::success();
}

} // namespace workpane::persistence
