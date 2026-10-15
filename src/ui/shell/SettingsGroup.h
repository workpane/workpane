#pragma once

#include "ui/shell/SettingsSection.h"

#include <string>
#include <vector>

namespace workpane::ui {

// A group is one category of the settings view, owned by the core or by one plugin and rendered section after section.
struct SettingsGroup final {
    std::string owner;
    std::string id;
    std::string titleKey;
    std::vector<SettingsSection> sections;

    [[nodiscard]] std::string surface(const SettingsSection& section) const;
};

} // namespace workpane::ui
