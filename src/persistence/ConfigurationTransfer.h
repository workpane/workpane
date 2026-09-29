#pragma once

#include "Result.h"
#include "persistence/Database.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>

namespace workpane::persistence {

// The whole configuration travels as one consistent SQLite snapshot, and an import is proven against the plugins installed here before it is staged for the next start.
class ConfigurationTransfer final {
  public:
    using SchemaVersions = std::map<std::string, std::int64_t, std::less<>>;

    [[nodiscard]] static Result<void> exportTo(Database& database, const std::filesystem::path& destination);
    [[nodiscard]] static Result<void> validate(const std::filesystem::path& source, const SchemaVersions& installed);
    [[nodiscard]] static Result<void> stage(const std::filesystem::path& source, const std::filesystem::path& dataDirectory, const SchemaVersions& installed);

  private:
    [[nodiscard]] static bool touches(const Database& database, const std::filesystem::path& destination);
    [[nodiscard]] static Result<void> snapshot(Database& database, const std::filesystem::path& destination);
    [[nodiscard]] static Result<void> pluginsFit(Database& source, const SchemaVersions& installed);
};

} // namespace workpane::persistence
