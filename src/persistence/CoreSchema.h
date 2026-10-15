#pragma once

#include "Result.h"
#include "persistence/Database.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace workpane::persistence {

class CoreSchema final {
  public:
    static constexpr std::int64_t version{1};

    [[nodiscard]] static Result<void> ensure(Database& database);
    [[nodiscard]] static Result<void> validate(Database& database);

  private:
    static constexpr std::string_view coreSettingsTable{"CREATE TABLE core_settings(owner TEXT PRIMARY KEY NOT NULL, document TEXT NOT NULL) STRICT"};
    static constexpr std::string_view corePluginSchemasTable{"CREATE TABLE core_plugin_schemas(plugin_id TEXT PRIMARY KEY NOT NULL, version INTEGER NOT NULL CHECK(version >= 1)) STRICT"};

    [[nodiscard]] static Result<std::int64_t> userVersion(Database& database);
};

} // namespace workpane::persistence
