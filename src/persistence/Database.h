#pragma once

#include "Result.h"
#include "persistence/Execution.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace workpane::persistence {

using Value = nlohmann::json;
using Row = nlohmann::json;
using Rows = std::vector<Row>;

// One SQLite connection with the durability and integrity settings every open of the product applies.
class Database final {
  public:
    [[nodiscard]] static Result<Database> open(const std::filesystem::path& file, bool readOnly = false);

    Database(Database&& other) noexcept;
    Database& operator=(Database&& other) noexcept;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    ~Database();

    [[nodiscard]] Result<void> executeScript(std::string_view sql);
    [[nodiscard]] Result<Rows> query(std::string_view sql, const std::vector<Value>& bindings = {});
    [[nodiscard]] Result<Execution> run(std::string_view sql, const std::vector<Value>& bindings = {});
    [[nodiscard]] sqlite3* handle() const;
    [[nodiscard]] const std::filesystem::path& file() const;
    [[nodiscard]] bool interrupting() const;
    void interrupt();
    void close();

  private:
    static constexpr int busyTimeoutMilliseconds{5000};

    [[nodiscard]] static bool blank(const char* text);
    [[nodiscard]] static Result<Value> column(sqlite3_stmt* statement, int index);

    Database(sqlite3* handle, std::filesystem::path file);

    [[nodiscard]] Result<sqlite3_stmt*> prepare(std::string_view sql, const std::vector<Value>& bindings);
    [[nodiscard]] Error failure(std::string code, std::string message) const;

    sqlite3* m_handle{nullptr};
    std::filesystem::path m_file;
    std::unique_ptr<std::atomic<bool>> m_interrupting;
};

} // namespace workpane::persistence
