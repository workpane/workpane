#include "persistence/StatementGuard.h"

#include <sqlite3.h>

#include <utility>

namespace workpane::persistence {

StatementGuard::StatementGuard(sqlite3_stmt* statement) : m_statement(statement) {}

StatementGuard::~StatementGuard() {
    sqlite3_finalize(m_statement);
}

sqlite3_stmt* StatementGuard::get() const {
    return m_statement;
}

sqlite3_stmt* StatementGuard::release() {
    return std::exchange(m_statement, nullptr);
}

} // namespace workpane::persistence
