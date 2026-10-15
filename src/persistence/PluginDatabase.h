#pragma once

#include "Result.h"
#include "persistence/Database.h"
#include "persistence/Execution.h"
#include "persistence/Statement.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::persistence {

// A plugin reaches only the tables and indexes carrying its own prefix, which SQLite itself enforces while it prepares every statement.
class PluginDatabase final {
  public:
    [[nodiscard]] static std::string tablePrefix(std::string_view pluginId);
    [[nodiscard]] static Result<Rows> query(Database& database, std::string_view pluginId, std::string_view sql, const std::vector<Value>& bindings);
    [[nodiscard]] static Result<Execution> run(Database& database, std::string_view pluginId, std::string_view sql, const std::vector<Value>& bindings);
    [[nodiscard]] static Result<std::vector<Execution>> transaction(Database& database, std::string_view pluginId, const std::vector<Statement>& statements);
    [[nodiscard]] static Result<std::int64_t> migrate(Database& database, std::string_view pluginId, const std::vector<std::vector<std::string>>& migrations);
    [[nodiscard]] static Result<std::vector<std::string>> plugins(Database& database);
    [[nodiscard]] static Result<void> erase(Database& database, std::string_view pluginId);

  private:
    [[nodiscard]] static Result<void> rollback(Database& database, const Error& error);
    [[nodiscard]] static Result<Execution> change(Database& database, std::string_view pluginId, std::string_view sql);
    [[nodiscard]] static Result<void> ownParents(Database& database, std::string_view pluginId);
    [[nodiscard]] static Result<std::map<std::string, std::string>> schema(Database& database, std::string_view pluginId);
    [[nodiscard]] static Result<void> verify(Database& database, std::string_view pluginId, const std::vector<std::vector<std::string>>& migrations);
};

} // namespace workpane::persistence
