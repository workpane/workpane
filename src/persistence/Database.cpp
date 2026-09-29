#include "persistence/Database.h"

#include "persistence/StatementGuard.h"
#include "platform/PathText.h"

#include <sqlite3.h>

#include <cmath>
#include <limits>
#include <set>
#include <string_view>
#include <utility>

namespace workpane::persistence {

bool Database::blank(const char* text) {
    for (; text != nullptr && *text != '\0'; ++text) {
        if (*text != ' ' && *text != '\n' && *text != '\r' && *text != '\t') {
            return false;
        }
    }

    return true;
}

Result<Value> Database::column(sqlite3_stmt* statement, int index) {
    switch (sqlite3_column_type(statement, index)) {
    case SQLITE_NULL:
        return Result<Value>::success(nullptr);
    case SQLITE_INTEGER:
        return Result<Value>::success(static_cast<std::int64_t>(sqlite3_column_int64(statement, index)));
    case SQLITE_FLOAT: {
        const double number = sqlite3_column_double(statement, index);
        return std::isfinite(number) ? Result<Value>::success(number) : Result<Value>::failure({"database_value_invalid", "A column carries a number that is not finite", sqlite3_column_name(statement, index)});
    }
    case SQLITE_TEXT: {
        const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, index));
        return Result<Value>::success(std::string(text, static_cast<std::size_t>(sqlite3_column_bytes(statement, index))));
    }
    default:
        return Result<Value>::failure({"database_value_blob", "A column carries a binary value this product never stores", sqlite3_column_name(statement, index)});
    }
}

Result<Database> Database::open(const std::filesystem::path& file, bool readOnly) {
    sqlite3* handle = nullptr;
    const int flags = (readOnly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE) | SQLITE_OPEN_EXRESCODE;
    const int opened = sqlite3_open_v2(platform::PathText::utf8(file).c_str(), &handle, flags, nullptr);

    if (opened != SQLITE_OK) {
        const std::string message = handle == nullptr ? sqlite3_errstr(opened) : sqlite3_errmsg(handle);
        sqlite3_close_v2(handle);
        return Result<Database>::failure({"database_open_failed", "The database could not be opened", file.string() + ": " + message});
    }

    Database database(handle, file);
    sqlite3_busy_timeout(handle, busyTimeoutMilliseconds);
    const auto configured = database.executeScript(readOnly ? "PRAGMA foreign_keys = ON;" : "PRAGMA journal_mode = WAL; PRAGMA synchronous = NORMAL; PRAGMA foreign_keys = ON;");

    if (!configured.hasValue()) {
        return Result<Database>::failure(configured.error());
    }

    return Result<Database>::success(std::move(database));
}

Database::Database(sqlite3* handle, std::filesystem::path file) : m_handle(handle), m_file(std::move(file)), m_interrupting(std::make_unique<std::atomic<bool>>(false)) {}

Database::Database(Database&& other) noexcept : m_handle(std::exchange(other.m_handle, nullptr)), m_file(std::move(other.m_file)), m_interrupting(std::move(other.m_interrupting)) {}

Database& Database::operator=(Database&& other) noexcept {
    if (this != &other) {
        close();
        m_handle = std::exchange(other.m_handle, nullptr);
        m_file = std::move(other.m_file);
        m_interrupting = std::move(other.m_interrupting);
    }

    return *this;
}

Database::~Database() {
    close();
}

// Runs trusted core text that may hold several statements, and is never reached by plugin text.
Result<void> Database::executeScript(std::string_view sql) {
    char* message = nullptr;
    const std::string text(sql);

    if (sqlite3_exec(m_handle, text.c_str(), nullptr, nullptr, &message) != SQLITE_OK) {
        sqlite3_free(message);
        return Result<void>::failure(failure("database_script_failed", "A database script failed"));
    }

    return Result<void>::success();
}

Result<Rows> Database::query(std::string_view sql, const std::vector<Value>& bindings) {
    auto prepared = prepare(sql, bindings);

    if (!prepared.hasValue()) {
        return Result<Rows>::failure(prepared.error());
    }

    const StatementGuard statement(prepared.value());
    std::set<std::string_view> names;
    Rows rows;

    // A row keys its values by column name, so a result naming one column twice is refused rather than losing a value.
    for (int index = 0; index < sqlite3_column_count(statement.get()); ++index) {
        if (!names.insert(sqlite3_column_name(statement.get(), index)).second) {
            return Result<Rows>::failure({"database_column_duplicate", "A query names one column twice", sqlite3_column_name(statement.get(), index)});
        }
    }

    while (true) {
        const int stepped = sqlite3_step(statement.get());

        if (stepped == SQLITE_DONE) {
            break;
        }

        if (stepped != SQLITE_ROW) {
            return Result<Rows>::failure(failure("database_query_failed", "A database query failed"));
        }

        Row row = Row::object();

        for (int index = 0; index < sqlite3_column_count(statement.get()); ++index) {
            auto value = column(statement.get(), index);

            if (!value.hasValue()) {
                return Result<Rows>::failure(value.error());
            }

            row[sqlite3_column_name(statement.get(), index)] = std::move(value.value());
        }

        rows.push_back(std::move(row));
    }

    return Result<Rows>::success(std::move(rows));
}

Result<Execution> Database::run(std::string_view sql, const std::vector<Value>& bindings) {
    auto prepared = prepare(sql, bindings);

    if (!prepared.hasValue()) {
        return Result<Execution>::failure(prepared.error());
    }

    const StatementGuard statement(prepared.value());
    int stepped = sqlite3_step(statement.get());

    while (stepped == SQLITE_ROW) {
        stepped = sqlite3_step(statement.get());
    }

    if (stepped != SQLITE_DONE) {
        return Result<Execution>::failure(failure("database_statement_failed", "A database statement failed"));
    }

    return Result<Execution>::success({sqlite3_changes64(m_handle), sqlite3_last_insert_rowid(m_handle)});
}

sqlite3* Database::handle() const {
    return m_handle;
}

const std::filesystem::path& Database::file() const {
    return m_file;
}

bool Database::interrupting() const {
    return m_interrupting->load();
}

// Called from another thread while the connection works, it stops the statement running now and every later statement of a plugin.
void Database::interrupt() {
    m_interrupting->store(true);
    sqlite3_interrupt(m_handle);
}

// A statement is exactly one command with exactly the bindings it declares, so trailing text or a missing value never runs.
Result<sqlite3_stmt*> Database::prepare(std::string_view sql, const std::vector<Value>& bindings) {
    if (sql.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return Result<sqlite3_stmt*>::failure({"database_statement_too_large", "A database statement is larger than SQLite accepts", std::to_string(sql.size())});
    }

    sqlite3_stmt* statement = nullptr;
    const char* tail = nullptr;

    if (sqlite3_prepare_v2(m_handle, sql.data(), static_cast<int>(sql.size()), &statement, &tail) != SQLITE_OK) {
        return Result<sqlite3_stmt*>::failure(failure("database_statement_invalid", "A database statement could not be prepared"));
    }

    if (statement == nullptr) {
        return Result<sqlite3_stmt*>::failure({"database_statement_empty", "A database statement is empty", std::string(sql)});
    }

    StatementGuard guard(statement);
    const std::string remainder = tail == nullptr ? std::string() : std::string(tail, static_cast<std::size_t>(sql.data() + sql.size() - tail));

    if (!blank(remainder.c_str())) {
        return Result<sqlite3_stmt*>::failure({"database_statement_multiple", "A database statement carries more than one command", std::string(sql)});
    }

    if (static_cast<std::size_t>(sqlite3_bind_parameter_count(statement)) != bindings.size()) {
        return Result<sqlite3_stmt*>::failure({"database_binding_count", "A database statement received a different number of values than it declares", std::string(sql)});
    }

    for (std::size_t index = 0; index < bindings.size(); ++index) {
        const Value& value = bindings[index];
        const int position = static_cast<int>(index + 1);
        int bound = SQLITE_OK;

        if (value.is_null()) {
            bound = sqlite3_bind_null(statement, position);
        } else if (value.is_boolean()) {
            bound = sqlite3_bind_int(statement, position, value.get<bool>() ? 1 : 0);
        } else if (value.is_number_integer()) {
            bound = sqlite3_bind_int64(statement, position, value.get<std::int64_t>());
        } else if (value.is_number_float()) {
            bound = sqlite3_bind_double(statement, position, value.get<double>());
        } else if (value.is_string()) {
            const auto& text = value.get_ref<const std::string&>();
            bound = sqlite3_bind_text64(statement, position, text.data(), text.size(), SQLITE_TRANSIENT, SQLITE_UTF8);
        } else {
            return Result<sqlite3_stmt*>::failure({"database_binding_type", "A database value is neither null, a boolean, a number nor a text", value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)});
        }

        if (bound != SQLITE_OK) {
            return Result<sqlite3_stmt*>::failure(failure("database_binding_failed", "A database value could not be bound"));
        }
    }

    // The guard gives the statement up only once every check passed, so each refusal above finalized it.
    return Result<sqlite3_stmt*>::success(guard.release());
}

// A file SQLite finds not to be a database, or corrupt, is told apart from a failure of the machine, since only such a file is ever set aside.
Error Database::failure(std::string code, std::string message) const {
    const int primary = sqlite3_errcode(m_handle) & 0xFF;

    if (primary == SQLITE_NOTADB || primary == SQLITE_CORRUPT) {
        return {"database_unreadable", "The database file is not a database or is corrupt", sqlite3_errmsg(m_handle)};
    }

    if (primary == SQLITE_INTERRUPT) {
        return {"database_interrupted", "A database statement was interrupted past its bound or as the product closed", sqlite3_errmsg(m_handle)};
    }

    return {std::move(code), std::move(message), sqlite3_errmsg(m_handle)};
}

void Database::close() {
    if (m_handle != nullptr) {
        sqlite3_close_v2(m_handle);
        m_handle = nullptr;
    }
}

} // namespace workpane::persistence
