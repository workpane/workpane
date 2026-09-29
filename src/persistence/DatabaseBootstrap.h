#pragma once

#include "Error.h"
#include "Result.h"
#include "persistence/Database.h"
#include "persistence/OpenedDatabase.h"

#include <array>
#include <filesystem>
#include <string_view>

namespace workpane::persistence {

// Opens the product database, applying a staged import first and setting aside a database this version cannot read.
class DatabaseBootstrap final {
  public:
    static constexpr const char* databaseName{"workpane.sqlite3"};
    static constexpr const char* stagedImportName{"workpane.sqlite3.import"};
    static constexpr const char* importBackupName{"workpane.sqlite3.backup"};

    [[nodiscard]] static Result<OpenedDatabase> open(const std::filesystem::path& dataDirectory);

  private:
    static constexpr std::array<const char*, 2> companionSuffixes{"-wal", "-shm"};
    static constexpr const char* rejectedMessage{"The imported configuration did not validate and the previous database was kept"};
    static constexpr std::array<std::string_view, 2> importNotices{"configuration_import_rejected", "configuration_import_interrupted"};
    static constexpr std::array<std::string_view, 6> unreadableCodes{"database_unreadable", "database_version_unreadable", "database_version_newer", "database_version_unknown", "database_schema_unknown", "database_schema_invalid"};

    [[nodiscard]] static Result<Database> openValidated(const std::filesystem::path& file);
    [[nodiscard]] static Result<void> applyStagedImport(const std::filesystem::path& dataDirectory);
    [[nodiscard]] static bool whole(const std::filesystem::path& staged);
    [[nodiscard]] static Result<void> restore(const std::filesystem::path& file, const std::filesystem::path& backup, Error notice);
    [[nodiscard]] static Result<void> moveDatabase(const std::filesystem::path& from, const std::filesystem::path& to);
    [[nodiscard]] static Result<void> removeDatabase(const std::filesystem::path& file);
    [[nodiscard]] static std::filesystem::path companion(const std::filesystem::path& file, const char* suffix);
};

} // namespace workpane::persistence
