#pragma once

#include "Result.h"
#include "persistence/Database.h"
#include "persistence/DatabaseExecutor.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::persistence {

using Documents = std::map<std::string, nlohmann::json, std::less<>>;

// One preference document per owner, read whole at startup and written whole in the background, rolling back a write that failed.
// An owner is a plugin identifier, or a plugin identifier and a document name joined by a colon for the further documents of that plugin.
class PreferenceStore final {
  public:
    using Completion = std::function<void(Result<void>)>;

    struct Loaded final {
        Documents documents;
        std::vector<std::string> discarded;
    };

    PreferenceStore(DatabaseExecutor& executor, Documents documents);

    PreferenceStore(const PreferenceStore&) = delete;
    PreferenceStore& operator=(const PreferenceStore&) = delete;

    [[nodiscard]] static Result<Loaded> load(Database& database);
    [[nodiscard]] static Result<void> store(Database& database, std::string_view owner, const nlohmann::json& document);
    [[nodiscard]] static Result<void> erase(Database& database, std::string_view plugin);

    [[nodiscard]] nlohmann::json document(std::string_view owner) const;
    [[nodiscard]] std::vector<std::string> plugins() const;
    void write(std::string owner, nlohmann::json document, Completion done);
    void discard(std::string_view plugin);

  private:
    [[nodiscard]] static bool belongs(std::string_view owner, std::string_view plugin);

    struct Entry final {
        nlohmann::json current = nlohmann::json::object();
        nlohmann::json committed = nlohmann::json::object();
        std::uint64_t revision{0};
    };

    DatabaseExecutor& m_executor;
    std::map<std::string, Entry, std::less<>> m_entries;
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};
};

} // namespace workpane::persistence
