#include "app/CorePreferences.h"

#include <algorithm>
#include <utility>

namespace workpane::app {

CorePreferences::CorePreferences(nlohmann::json document) : m_document(std::move(document)) {}

std::string CorePreferences::language() const {
    return text(languageKey);
}

std::string CorePreferences::theme() const {
    return text(themeKey);
}

// The folders are absolute, named once and at most thirty two, and a list that breaks the rule reads as no folder at all, as the core settings of Lua read it.
std::vector<std::filesystem::path> CorePreferences::pluginFolders() const {
    const auto stored = m_document.find(pluginFoldersKey);
    std::vector<std::filesystem::path> folders;

    if (stored == m_document.end() || !stored->is_array() || stored->size() > largestPluginFolders) {
        return folders;
    }

    for (const auto& entry : *stored) {
        const std::filesystem::path folder = entry.is_string() ? std::filesystem::path(entry.get<std::string>()) : std::filesystem::path();

        if (!folder.is_absolute() || std::ranges::find(folders, folder) != folders.end()) {
            return {};
        }

        folders.push_back(folder);
    }

    return folders;
}

std::string CorePreferences::text(std::string_view key) const {
    const auto found = m_document.find(key);
    return found != m_document.end() && found->is_string() ? found->get<std::string>() : std::string();
}

} // namespace workpane::app
