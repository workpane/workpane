#pragma once

struct sqlite3_stmt;

namespace workpane::persistence {

// A prepared statement is finalized by whichever path leaves the scope that prepared it.
class StatementGuard final {
  public:
    explicit StatementGuard(sqlite3_stmt* statement);
    ~StatementGuard();

    StatementGuard(const StatementGuard&) = delete;
    StatementGuard& operator=(const StatementGuard&) = delete;

    [[nodiscard]] sqlite3_stmt* get() const;
    [[nodiscard]] sqlite3_stmt* release();

  private:
    sqlite3_stmt* m_statement;
};

} // namespace workpane::persistence
