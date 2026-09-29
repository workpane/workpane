#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::app {

// The preferences of the core that C++ reads before Lua runs, each read by the rule the core settings declare, so a value it cannot use reads as empty.
class CorePreferences final {
  public:
    explicit CorePreferences(nlohmann::json document);

    [[nodiscard]] std::string language() const;
    [[nodiscard]] std::string theme() const;
    [[nodiscard]] std::vector<std::filesystem::path> pluginFolders() const;

  private:
    static constexpr std::string_view languageKey{"language"};
    static constexpr std::string_view themeKey{"theme"};
    static constexpr std::string_view pluginFoldersKey{"pluginFolders"};
    static constexpr std::size_t largestPluginFolders{32};

    [[nodiscard]] std::string text(std::string_view key) const;

    nlohmann::json m_document;
};

} // namespace workpane::app
