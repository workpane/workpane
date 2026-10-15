#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "localization/Localization.h"
#include "scripting/PluginManifest.h"
#include "ui/shell/NavigationItem.h"
#include "ui/shell/NavigationShortcut.h"
#include "ui/shell/SettingsGroup.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string_view>
#include <vector>

namespace workpane::scripting {

// The validated contributions of every running plugin, from which the shell builds its destinations and its settings categories.
class PluginRegistry final {
  public:
    [[nodiscard]] static Result<PluginManifest> parse(const json::Json& manifest, const std::vector<std::filesystem::path>& folders, const localization::Localization& localization);
    [[nodiscard]] static Result<localization::TranslationCatalog> parseCatalog(const json::Json& translations);
    [[nodiscard]] Result<void> add(PluginManifest manifest);
    [[nodiscard]] Result<void> addCoreSettings(const json::Json& groups, const localization::Localization& localization);
    void remove(std::string_view pluginId);
    [[nodiscard]] const PluginManifest* find(std::string_view pluginId) const;
    [[nodiscard]] std::vector<ui::NavigationItem> navigation() const;
    [[nodiscard]] std::vector<ui::BandItem> bands() const;
    [[nodiscard]] Result<void> resizeBand(std::string_view pluginId, std::string_view item, std::int64_t height);
    [[nodiscard]] std::vector<ui::SettingsGroup> settingsGroups() const;

  private:
    static constexpr std::int64_t largestOrder{100000};
    static constexpr std::int64_t smallestBand{16};
    static constexpr std::int64_t largestBand{320};

    using KeyCheck = std::function<bool(std::string_view key)>;

    [[nodiscard]] static Result<std::vector<ui::SettingsGroup>> parseSettings(const json::Json& groups, std::string_view owner, const KeyCheck& translated);
    [[nodiscard]] static Result<std::vector<ui::NavigationItem>> parseNavigation(const json::Json& items, std::string_view owner, const KeyCheck& translated);
    [[nodiscard]] static Result<std::vector<ui::BandItem>> parseBands(const json::Json& items, std::string_view owner, const KeyCheck& translated);
    [[nodiscard]] static Result<std::vector<ui::NavigationShortcut>> parseShortcuts(const json::Json& shortcuts);
    [[nodiscard]] static bool validBandHeight(std::int64_t height);

    std::vector<PluginManifest> m_plugins;
    std::vector<ui::SettingsGroup> m_coreSettings;
};

} // namespace workpane::scripting
