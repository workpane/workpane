#include "scripting/PluginRegistry.h"

#include "scripting/Identifier.h"
#include "ui/IconCatalog.h"
#include "ui/shell/KeyChords.h"

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <system_error>
#include <utility>

namespace workpane::scripting {

// Reads a manifest strictly and proves every title and search key it names is spelled by the catalog its plugin installed when it was discovered.
Result<PluginManifest> PluginRegistry::parse(const json::Json& manifest, const std::vector<std::filesystem::path>& folders, const localization::Localization& localization) {
    PluginManifest parsed;
    std::string directory;
    const json::Json* navigation = &json::ObjectReader::emptyList();
    const json::Json* bands = &json::ObjectReader::emptyList();
    const json::Json* settings = &json::ObjectReader::emptyList();
    json::ObjectReader reader(manifest, "plugin");
    reader.readText("id", parsed.id).readText("titleKey", parsed.titleKey).readText("descriptionKey", parsed.descriptionKey, json::Presence::Optional).readText("directory", directory).read("dependencies", parsed.dependencies, json::Presence::Optional).readArray("navigation", navigation, json::Presence::Optional).readArray("bands", bands, json::Presence::Optional).readArray("settings", settings, json::Presence::Optional);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return Result<PluginManifest>::failure(finished.error());
    }

    if (!Identifier::valid(parsed.id)) {
        return Result<PluginManifest>::failure({"plugin_identifier_invalid", "A plugin identifier is lowercase letters, numbers and hyphens", parsed.id});
    }

    // A plugin lives in the directory named after it inside one of the plugin folders, so its assets and its identity can never point at another plugin.
    std::error_code error;
    const auto declared = std::filesystem::weakly_canonical(std::filesystem::path(directory), error);
    // clang-format off
    const auto inside = std::ranges::any_of(folders, [&declared, &parsed](const std::filesystem::path& folder) {
        std::error_code canonical;
        return std::filesystem::weakly_canonical(folder / parsed.id, canonical) == declared && !canonical;
    });
    // clang-format on

    if (error || !inside) {
        return Result<PluginManifest>::failure({"plugin_directory_invalid", "A plugin is not loaded from the directory named after it in a plugin folder", directory});
    }

    for (const auto& dependency : parsed.dependencies) {
        if (!Identifier::valid(dependency) || dependency == parsed.id) {
            return Result<PluginManifest>::failure({"plugin_dependency_invalid", "A plugin names an invalid dependency", parsed.id + ":" + dependency});
        }
    }

    const std::string prefix = parsed.id + ".";
    // clang-format off
    const KeyCheck translated = [&prefix, &localization](std::string_view key) { return key.starts_with(prefix) && localization.contains(key); };
    // clang-format on

    if (!translated(parsed.titleKey) || (!parsed.descriptionKey.empty() && !translated(parsed.descriptionKey))) {
        return Result<PluginManifest>::failure({"plugin_title_untranslated", "A plugin title or description is not spelled by its own catalog", parsed.titleKey});
    }

    auto items = parseNavigation(*navigation, parsed.id, translated);

    if (!items.hasValue()) {
        return Result<PluginManifest>::failure(items.error());
    }

    auto declaredBands = parseBands(*bands, parsed.id, translated);

    if (!declaredBands.hasValue()) {
        return Result<PluginManifest>::failure(declaredBands.error());
    }

    auto groups = parseSettings(*settings, parsed.id, translated);

    if (!groups.hasValue()) {
        return Result<PluginManifest>::failure(groups.error());
    }

    parsed.directory = declared;
    parsed.navigation = std::move(items.value());
    parsed.bands = std::move(declaredBands.value());
    parsed.settings = std::move(groups.value());

    return Result<PluginManifest>::success(std::move(parsed));
}

// The contributions are accepted together or not at all, so a refused plugin leaves no trace behind.
Result<void> PluginRegistry::add(PluginManifest manifest) {
    if (find(manifest.id) != nullptr) {
        return Result<void>::failure({"plugin_duplicate", "Two plugins declare one identifier", manifest.id});
    }

    // Two destinations or two bands claiming one position refuse the plugin declaring the second one, even when it declares both, so the window never falls back to load order.
    std::vector<ui::NavigationItem> claimed = navigation();

    for (const auto& item : manifest.navigation) {
        for (const auto& existing : claimed) {
            if (existing.placement == item.placement && existing.order == item.order) {
                return Result<void>::failure({"plugin_navigation_order_taken", "A navigation position is already claimed by another destination", manifest.id + ":" + item.id + " and " + existing.destination()});
            }
        }

        claimed.push_back(item);
    }

    std::vector<ui::BandItem> taken = bands();

    for (const auto& band : manifest.bands) {
        for (const auto& existing : taken) {
            if (existing.placement == band.placement && existing.order == band.order) {
                return Result<void>::failure({"plugin_band_order_taken", "A band position is already claimed by another band", manifest.id + ":" + band.id + " and " + existing.plugin + ":" + existing.id});
            }
        }

        taken.push_back(band);
    }

    m_plugins.push_back(std::move(manifest));

    return Result<void>::success();
}

Result<void> PluginRegistry::addCoreSettings(const json::Json& groups, const localization::Localization& localization) {
    // clang-format off
    const KeyCheck translated = [&localization](std::string_view key) { return key.starts_with(std::string(localization::Localization::coreOwner) + ".") && localization.contains(key); };
    // clang-format on
    auto parsed = parseSettings(groups, localization::Localization::coreOwner, translated);

    if (!parsed.hasValue()) {
        return Result<void>::failure(parsed.error());
    }

    for (const auto& group : parsed.value()) {
        // clang-format off
        const bool taken = std::ranges::any_of(m_coreSettings, [&group](const ui::SettingsGroup& existing) { return existing.id == group.id; });
        // clang-format on

        if (taken) {
            return Result<void>::failure({"settings_group_duplicate", "Two settings groups of one owner share an identifier", group.id});
        }
    }

    m_coreSettings.insert(m_coreSettings.end(), parsed.value().begin(), parsed.value().end());

    return Result<void>::success();
}

void PluginRegistry::remove(std::string_view pluginId) {
    // clang-format off
    std::erase_if(m_plugins, [pluginId](const PluginManifest& manifest) { return manifest.id == pluginId; });
    // clang-format on
}

const PluginManifest* PluginRegistry::find(std::string_view pluginId) const {
    // clang-format off
    const auto found = std::ranges::find_if(m_plugins, [pluginId](const PluginManifest& manifest) { return manifest.id == pluginId; });
    // clang-format on
    return found == m_plugins.end() ? nullptr : &*found;
}

std::vector<ui::NavigationItem> PluginRegistry::navigation() const {
    std::vector<ui::NavigationItem> items;

    for (const auto& plugin : m_plugins) {
        items.insert(items.end(), plugin.navigation.begin(), plugin.navigation.end());
    }

    return items;
}

std::vector<ui::BandItem> PluginRegistry::bands() const {
    std::vector<ui::BandItem> items;

    for (const auto& plugin : m_plugins) {
        items.insert(items.end(), plugin.bands.begin(), plugin.bands.end());
    }

    return items;
}

// The registry keeps the height a plugin gives its band at runtime, so a change of the other plugins never puts the declared one back.
Result<void> PluginRegistry::resizeBand(std::string_view pluginId, std::string_view item, std::int64_t height) {
    if (!validBandHeight(height)) {
        return Result<void>::failure({"shell_band_height_invalid", "A band is hidden with a height of zero or stands from 16 to 320 points tall", std::to_string(height)});
    }

    for (auto& plugin : m_plugins) {
        for (auto& band : plugin.bands) {
            if (plugin.id == pluginId && band.id == item) {
                band.height = height;
                return Result<void>::success();
            }
        }
    }

    return Result<void>::failure({"shell_band_unknown", "The plugin declares no band under that identity", std::string(pluginId) + ":" + std::string(item)});
}

bool PluginRegistry::validBandHeight(std::int64_t height) {
    return height == 0 || (height >= smallestBand && height <= largestBand);
}

std::vector<ui::SettingsGroup> PluginRegistry::settingsGroups() const {
    std::vector<ui::SettingsGroup> groups = m_coreSettings;

    for (const auto& plugin : m_plugins) {
        groups.insert(groups.end(), plugin.settings.begin(), plugin.settings.end());
    }

    return groups;
}

Result<std::vector<ui::SettingsGroup>> PluginRegistry::parseSettings(const json::Json& groups, std::string_view owner, const KeyCheck& translated) {
    std::vector<ui::SettingsGroup> parsed;
    std::set<std::string, std::less<>> groupIds;

    for (const auto& entry : groups) {
        ui::SettingsGroup group;
        const json::Json* sections = nullptr;
        json::ObjectReader reader(entry, "settings");
        reader.readText("id", group.id).readText("titleKey", group.titleKey).readArray("sections", sections);

        if (const auto finished = reader.finish(); !finished.hasValue()) {
            return Result<std::vector<ui::SettingsGroup>>::failure(finished.error());
        }

        if (!Identifier::valid(group.id) || !groupIds.insert(group.id).second) {
            return Result<std::vector<ui::SettingsGroup>>::failure({"settings_group_invalid", "A settings group identifier is invalid or repeated", group.id});
        }

        if (!translated(group.titleKey)) {
            return Result<std::vector<ui::SettingsGroup>>::failure({"settings_title_untranslated", "A settings group title is not spelled by its owner catalog", group.titleKey});
        }

        if (sections->empty()) {
            return Result<std::vector<ui::SettingsGroup>>::failure({"settings_sections_missing", "A settings group declares no section", group.id});
        }

        std::set<std::string, std::less<>> sectionIds;

        for (const auto& sectionEntry : *sections) {
            ui::SettingsSection section;
            json::ObjectReader sectionReader(sectionEntry, "settings.sections");
            sectionReader.readText("id", section.id).readText("titleKey", section.titleKey).read("searchKeys", section.searchKeys);

            if (const auto finished = sectionReader.finish(); !finished.hasValue()) {
                return Result<std::vector<ui::SettingsGroup>>::failure(finished.error());
            }

            if (!Identifier::valid(section.id) || !sectionIds.insert(section.id).second) {
                return Result<std::vector<ui::SettingsGroup>>::failure({"settings_section_invalid", "A settings section identifier is invalid or repeated", section.id});
            }

            if (!translated(section.titleKey) || section.searchKeys.empty()) {
                return Result<std::vector<ui::SettingsGroup>>::failure({"settings_section_untranslated", "A settings section needs a translated title and search keys", section.id});
            }

            // clang-format off
            const bool searchable = std::ranges::all_of(section.searchKeys, [&translated](const std::string& key) { return translated(key); });
            // clang-format on

            if (!searchable) {
                return Result<std::vector<ui::SettingsGroup>>::failure({"settings_search_untranslated", "A settings search key is not spelled by its owner catalog", section.id});
            }

            group.sections.push_back(std::move(section));
        }

        // A group of exactly one section names it general, so every such group reads the same in every owner.
        if (group.sections.size() == 1 && group.sections.front().id != "general") {
            return Result<std::vector<ui::SettingsGroup>>::failure({"settings_single_section", "A group with one section names it general", group.id});
        }

        group.owner = std::string(owner);
        parsed.push_back(std::move(group));
    }

    return Result<std::vector<ui::SettingsGroup>>::success(std::move(parsed));
}

Result<std::vector<ui::NavigationItem>> PluginRegistry::parseNavigation(const json::Json& items, std::string_view owner, const KeyCheck& translated) {
    std::vector<ui::NavigationItem> parsed;
    std::set<std::string, std::less<>> ids;

    for (const auto& entry : items) {
        ui::NavigationItem item;
        std::string icon;
        const json::Json* shortcuts = &json::ObjectReader::emptyList();
        json::ObjectReader reader(entry, "navigation");
        reader.readText("id", item.id).readText("titleKey", item.titleKey).readText("icon", icon).readChoice("placement", item.placement, {{"primary", ui::NavigationPlacement::Primary}, {"secondary", ui::NavigationPlacement::Secondary}}).readInteger("order", item.order, 0, largestOrder).read("preload", item.preload, json::Presence::Optional).readArray("shortcuts", shortcuts, json::Presence::Optional);

        if (const auto finished = reader.finish(); !finished.hasValue()) {
            return Result<std::vector<ui::NavigationItem>>::failure(finished.error());
        }

        if (!Identifier::valid(item.id) || !ids.insert(item.id).second) {
            return Result<std::vector<ui::NavigationItem>>::failure({"navigation_identifier_invalid", "A navigation identifier is invalid or repeated inside its plugin", item.id});
        }

        if (!translated(item.titleKey)) {
            return Result<std::vector<ui::NavigationItem>>::failure({"navigation_title_untranslated", "A navigation title is not spelled by its own catalog", item.titleKey});
        }

        const auto parsedIcon = ui::IconCatalog::parse(icon);

        if (!parsedIcon.has_value()) {
            return Result<std::vector<ui::NavigationItem>>::failure({"navigation_icon_unknown", "A navigation icon names nothing in the painted set", icon});
        }

        auto declared = parseShortcuts(*shortcuts);

        if (!declared.hasValue()) {
            return Result<std::vector<ui::NavigationItem>>::failure(declared.error());
        }

        item.plugin = std::string(owner);
        item.icon = *parsedIcon;
        item.shortcuts = std::move(declared.value());
        parsed.push_back(std::move(item));
    }

    return Result<std::vector<ui::NavigationItem>>::success(std::move(parsed));
}

Result<std::vector<ui::BandItem>> PluginRegistry::parseBands(const json::Json& items, std::string_view owner, const KeyCheck& translated) {
    std::vector<ui::BandItem> parsed;
    std::set<std::string, std::less<>> ids;

    for (const auto& entry : items) {
        ui::BandItem band;
        json::ObjectReader reader(entry, "bands");
        reader.readText("id", band.id).readText("titleKey", band.titleKey).readChoice("placement", band.placement, {{"top", ui::BandPlacement::Top}, {"bottom", ui::BandPlacement::Bottom}}).readInteger("order", band.order, 0, largestOrder).readInteger("height", band.height, std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max());

        if (const auto finished = reader.finish(); !finished.hasValue()) {
            return Result<std::vector<ui::BandItem>>::failure(finished.error());
        }

        if (!Identifier::valid(band.id) || !ids.insert(band.id).second) {
            return Result<std::vector<ui::BandItem>>::failure({"band_identifier_invalid", "A band identifier is invalid or repeated inside its plugin", band.id});
        }

        if (!translated(band.titleKey)) {
            return Result<std::vector<ui::BandItem>>::failure({"band_title_untranslated", "A band title is not spelled by its own catalog", band.titleKey});
        }

        if (!validBandHeight(band.height)) {
            return Result<std::vector<ui::BandItem>>::failure({"shell_band_height_invalid", "A band is hidden with a height of zero or stands from 16 to 320 points tall", band.id});
        }

        band.plugin = std::string(owner);
        parsed.push_back(std::move(band));
    }

    return Result<std::vector<ui::BandItem>>::success(std::move(parsed));
}

// A shortcut of a view is a combination the core does not keep for itself, and one view never declares the same identity or combination twice.
Result<std::vector<ui::NavigationShortcut>> PluginRegistry::parseShortcuts(const json::Json& shortcuts) {
    std::vector<ui::NavigationShortcut> parsed;
    std::set<std::string, std::less<>> ids;
    std::set<ImGuiKeyChord> chords;

    for (const auto& entry : shortcuts) {
        ui::NavigationShortcut shortcut;
        std::string keys;
        json::ObjectReader reader(entry, "navigation.shortcuts");
        reader.readText("id", shortcut.id).readText("keys", keys);

        if (const auto finished = reader.finish(); !finished.hasValue()) {
            return Result<std::vector<ui::NavigationShortcut>>::failure(finished.error());
        }

        const auto chord = ui::KeyChords::parse(keys);

        if (!Identifier::valid(shortcut.id) || !ids.insert(shortcut.id).second) {
            return Result<std::vector<ui::NavigationShortcut>>::failure({"navigation_shortcut_invalid", "A shortcut identifier is invalid or repeated inside its view", shortcut.id});
        }

        if (!chord.has_value() || ui::KeyChords::reserved(*chord) || !chords.insert(*chord).second) {
            return Result<std::vector<ui::NavigationShortcut>>::failure({"navigation_shortcut_invalid", "A shortcut combination is invalid, kept by the core or repeated inside its view", keys});
        }

        shortcut.chord = *chord;
        parsed.push_back(std::move(shortcut));
    }

    return Result<std::vector<ui::NavigationShortcut>>::success(std::move(parsed));
}

// Reads a catalog of a plugin as languages of sentences, which the localization then validates whole.
Result<localization::TranslationCatalog> PluginRegistry::parseCatalog(const json::Json& translations) {
    localization::TranslationCatalog catalog;

    for (const auto& [locale, entries] : translations.items()) {
        if (!entries.is_object()) {
            return Result<localization::TranslationCatalog>::failure({"translation_locale_invalid", "A catalog language is not an object of sentences", locale});
        }

        auto& parsed = catalog[locale];

        for (const auto& [key, text] : entries.items()) {
            if (!text.is_string()) {
                return Result<localization::TranslationCatalog>::failure({"translation_value_invalid", "A translation is not a text", locale + ":" + key});
            }

            parsed.emplace(key, text.get<std::string>());
        }
    }

    return Result<localization::TranslationCatalog>::success(std::move(catalog));
}

} // namespace workpane::scripting
