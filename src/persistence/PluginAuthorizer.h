#pragma once

#include "persistence/Database.h"

#include <chrono>
#include <string>
#include <string_view>

namespace workpane::persistence {

// Restricts the connection to the tables of one plugin for as long as one of its statements runs, lets only a migration change the schema and interrupts a statement past its bound.
class PluginAuthorizer final {
  public:
    enum class Purpose { Statement, Migration };

    PluginAuthorizer(Database& database, std::string prefix, Purpose purpose);
    ~PluginAuthorizer();

    PluginAuthorizer(const PluginAuthorizer&) = delete;
    PluginAuthorizer& operator=(const PluginAuthorizer&) = delete;

  private:
    static constexpr std::chrono::seconds statementBound{10};
    static constexpr std::chrono::seconds migrationBound{120};
    static constexpr int progressSteps{10000};

    [[nodiscard]] static int authorize(void* context, int action, const char* first, const char* second, const char* database, const char* trigger);
    [[nodiscard]] static bool internalRead(const PluginAuthorizer& state, const char* table);
    [[nodiscard]] static int changeSchema(PluginAuthorizer& state, bool permitted);
    [[nodiscard]] static bool owned(const char* name, const std::string& prefix);
    [[nodiscard]] static bool schemaTable(const char* name);
    [[nodiscard]] static bool internalTable(const char* name);
    [[nodiscard]] static bool named(const char* name, std::string_view expected);
    [[nodiscard]] static bool automaticIndex(const char* name);
    [[nodiscard]] static int progress(void* context);

    Database& m_database;
    std::string m_prefix;
    bool m_migrating;
    std::chrono::steady_clock::time_point m_deadline;
    bool m_changingSchema{false};
    bool m_altering{false};
    bool m_querying{false};
};

} // namespace workpane::persistence
