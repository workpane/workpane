#pragma once

#include "ui/shell/BandItem.h"
#include "ui/shell/NavigationItem.h"
#include "ui/shell/SettingsGroup.h"

#include <filesystem>
#include <string>
#include <vector>

namespace workpane::scripting {

struct PluginManifest final {
    std::string id;
    std::string titleKey;
    std::string descriptionKey;
    std::filesystem::path directory;
    std::vector<std::string> dependencies;
    std::vector<ui::NavigationItem> navigation;
    std::vector<ui::BandItem> bands;
    std::vector<ui::SettingsGroup> settings;
};

} // namespace workpane::scripting
