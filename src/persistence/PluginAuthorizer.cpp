#include "persistence/PluginAuthorizer.h"

#include <sqlite3.h>

#include <utility>

namespace workpane::persistence {

PluginAuthorizer::PluginAuthorizer(Database& database, std::string prefix, Purpose purpose) : m_database(database), m_prefix(std::move(prefix)), m_migrating(purpose == Purpose::Migration), m_deadline(std::chrono::steady_clock::now() + (purpose == Purpose::Migration ? migrationBound : statementBound)) {
    sqlite3_set_authorizer(m_database.handle(), &PluginAuthorizer::authorize, this);
    sqlite3_progress_handler(m_database.handle(), progressSteps, &PluginAuthorizer::progress, this);
}

PluginAuthorizer::~PluginAuthorizer() {
    sqlite3_progress_handler(m_database.handle(), 0, nullptr, nullptr);
    sqlite3_set_authorizer(m_database.handle(), nullptr, nullptr);
}

// A statement of a plugin that runs past its bound, or while the product closes, is interrupted, so no plugin holds the only database thread.
int PluginAuthorizer::progress(void* context) {
    const PluginAuthorizer& state = *static_cast<PluginAuthorizer*>(context);

    return state.m_database.interrupting() || std::chrono::steady_clock::now() > state.m_deadline ? 1 : 0;
}

bool PluginAuthorizer::owned(const char* name, const std::string& prefix) {
    if (name == nullptr) {
        return false;
    }

    // SQLite hands the stored name of a table it reads or writes, so the prefix is matched exactly and a table created in other letters is never owned.
    const std::string_view candidate(name);

    return candidate.size() > prefix.size() && candidate.starts_with(prefix);
}

bool PluginAuthorizer::named(const char* name, std::string_view expected) {
    return name != nullptr && std::string_view(name) == expected;
}

// SQLite names the index it makes for a primary key or a unique column by itself, and refuses that name to every statement.
bool PluginAuthorizer::automaticIndex(const char* name) {
    return name != nullptr && std::string_view(name).starts_with("sqlite_autoindex_");
}

// SQLite refuses a direct write to the schema tables by itself, so allowing their writes only lets it record changes to owned objects.
bool PluginAuthorizer::schemaTable(const char* name) {
    return named(name, "sqlite_master") || named(name, "sqlite_schema") || named(name, "sqlite_temp_master") || named(name, "sqlite_temp_schema");
}

// The tables SQLite reads and writes by itself while it changes a schema, which a plugin never reaches with a statement of its own.
bool PluginAuthorizer::internalTable(const char* name) {
    return schemaTable(name) || named(name, "sqlite_sequence") || named(name, "pragma_quick_check");
}

// SQLite reads its own tables by itself while it changes a schema, but a query of the statement never does, which keeps the schema of other plugins out of reach of a table created from a query.
bool PluginAuthorizer::internalRead(const PluginAuthorizer& state, const char* table) {
    if (!state.m_changingSchema || !internalTable(table)) {
        return false;
    }

    return !state.m_querying || named(table, "pragma_quick_check");
}

// Only a migration changes the schema, where the foreign keys of what it creates are checked before it commits.
int PluginAuthorizer::changeSchema(PluginAuthorizer& state, bool permitted) {
    if (!permitted || !state.m_migrating) {
        return SQLITE_DENY;
    }

    state.m_changingSchema = true;

    return SQLITE_OK;
}

// A plugin reaches only the tables named with its prefix, and SQLite reaches its own tables only while it changes the schema of one of them.
int PluginAuthorizer::authorize(void* context, int action, const char* first, const char* second, const char*, const char*) {
    PluginAuthorizer& state = *static_cast<PluginAuthorizer*>(context);
    const std::string& prefix = state.m_prefix;

    switch (action) {
    case SQLITE_SELECT:
        // Changing a column makes SQLite read its schema with selects of its own, which are not a query of the plugin.
        state.m_querying = state.m_querying || !state.m_altering;
        return SQLITE_OK;
    case SQLITE_RECURSIVE:
        return SQLITE_OK;
    case SQLITE_FUNCTION:
        // Renaming a table names only the old table here, so a plugin could move its table into another namespace and is refused the rename.
        return named(second, "sqlite_rename_table") ? SQLITE_DENY : SQLITE_OK;
    case SQLITE_READ:
        return owned(first, prefix) || internalRead(state, first) ? SQLITE_OK : SQLITE_DENY;
    case SQLITE_INSERT:
    case SQLITE_UPDATE:
    case SQLITE_DELETE:
        return owned(first, prefix) || schemaTable(first) || (state.m_changingSchema && internalTable(first)) ? SQLITE_OK : SQLITE_DENY;
    case SQLITE_CREATE_TABLE:
        return changeSchema(state, owned(first, prefix) || (state.m_changingSchema && named(first, "sqlite_sequence")));
    case SQLITE_DROP_TABLE:
        return changeSchema(state, owned(first, prefix));
    case SQLITE_CREATE_INDEX:
    case SQLITE_DROP_INDEX:
        return changeSchema(state, (owned(first, prefix) || automaticIndex(first)) && owned(second, prefix));
    case SQLITE_CREATE_VIEW:
    case SQLITE_DROP_VIEW:
        return changeSchema(state, owned(first, prefix));
    case SQLITE_CREATE_TRIGGER:
    case SQLITE_DROP_TRIGGER:
        return changeSchema(state, owned(first, prefix) && owned(second, prefix));
    case SQLITE_ALTER_TABLE:
        state.m_altering = true;
        return changeSchema(state, owned(second, prefix));
    case SQLITE_REINDEX:
        return state.m_changingSchema && owned(first, prefix) ? SQLITE_OK : SQLITE_DENY;
    case SQLITE_PRAGMA:
        return state.m_changingSchema && named(first, "quick_check") && owned(second, prefix) ? SQLITE_OK : SQLITE_DENY;
    default:
        return SQLITE_DENY;
    }
}

} // namespace workpane::persistence
