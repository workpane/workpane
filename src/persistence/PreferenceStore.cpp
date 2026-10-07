#include "persistence/PreferenceStore.h"

#include <algorithm>
#include <utility>

namespace workpane::persistence {

PreferenceStore::PreferenceStore(DatabaseExecutor& executor, Documents documents) : m_executor(executor) {
    for (auto& [owner, document] : documents) {
        m_entries.emplace(owner, Entry{document, document, 0});
    }
}

// A stored row that is not an owner with a JSON object is left out and named, so one damaged document costs only the preferences of its owner.
Result<PreferenceStore::Loaded> PreferenceStore::load(Database& database) {
    auto rows = database.query("SELECT owner, document FROM core_settings");

    if (!rows.hasValue()) {
        return Result<Loaded>::failure(rows.error());
    }

    Loaded loaded;

    for (const auto& row : rows.value()) {
        const std::string owner = row["owner"].is_string() ? row["owner"].get<std::string>() : row["owner"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        auto document = row["document"].is_string() ? nlohmann::json::parse(row["document"].get<std::string>(), nullptr, false) : nlohmann::json();

        if (!row["owner"].is_string() || !document.is_object()) {
            loaded.discarded.push_back(owner);
            continue;
        }

        loaded.documents.emplace(owner, std::move(document));
    }

    return Result<Loaded>::success(std::move(loaded));
}

Result<void> PreferenceStore::store(Database& database, std::string_view owner, const nlohmann::json& document) {
    const auto written = database.run("INSERT INTO core_settings(owner, document) VALUES(?, ?) ON CONFLICT(owner) DO UPDATE SET document = excluded.document", {std::string(owner), document.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)});
    return written.hasValue() ? Result<void>::success() : Result<void>::failure(written.error());
}

Result<void> PreferenceStore::erase(Database& database, std::string_view plugin) {
    const std::string owner(plugin);
    const auto removed = database.run("DELETE FROM core_settings WHERE owner = ? OR substr(owner, 1, length(?) + 1) = ? || ':'", {owner, owner, owner});
    return removed.hasValue() ? Result<void>::success() : Result<void>::failure(removed.error());
}

bool PreferenceStore::belongs(std::string_view owner, std::string_view plugin) {
    return owner == plugin || (owner.size() > plugin.size() && owner.starts_with(plugin) && owner[plugin.size()] == ':');
}

nlohmann::json PreferenceStore::document(std::string_view owner) const {
    const auto found = m_entries.find(owner);
    return found == m_entries.end() ? nlohmann::json::object() : found->second.current;
}

// Answers every plugin that keeps a preference document, once each, without the documents of the core.
std::vector<std::string> PreferenceStore::plugins() const {
    std::vector<std::string> found;

    for (const auto& [owner, entry] : m_entries) {
        const std::string plugin = owner.substr(0, owner.find(':'));

        if (std::ranges::find(found, plugin) == found.end()) {
            found.push_back(plugin);
        }
    }

    return found;
}

// The new document is what every reader sees at once, and the committed one comes back only if this is still the latest write when it fails.
void PreferenceStore::write(std::string owner, nlohmann::json document, Completion done) {
    if (!document.is_object()) {
        done(Result<void>::failure({"preferences_document_invalid", "A preference document must be a JSON object", owner}));
        return;
    }

    auto& entry = m_entries[owner];
    entry.current = document;
    const std::uint64_t revision = ++entry.revision;
    const std::weak_ptr<bool> alive = m_alive;

    // clang-format off
    std::function<Result<void>(Database&)> work = [owner, document](Database& database) { return store(database, owner, document); };
    std::function<void(Result<void>)> finished = [this, alive, owner, document, revision, done = std::move(done)](Result<void> result) {
        if (alive.expired()) {
            return;
        }

        auto& written = m_entries[owner];

        if (result.hasValue()) {
            written.committed = document;
        } else if (revision == written.revision) {
            written.current = written.committed;
        }

        done(std::move(result));
    };
    // clang-format on

    m_executor.submit<void>(std::move(work), std::move(finished));
}

void PreferenceStore::discard(std::string_view plugin) {
    // clang-format off
    std::erase_if(m_entries, [plugin](const auto& entry) { return belongs(entry.first, plugin); });
    // clang-format on
}

} // namespace workpane::persistence
