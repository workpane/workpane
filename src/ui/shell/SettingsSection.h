#pragma once

#include <string>
#include <vector>

namespace workpane::ui {

struct SettingsSection final {
    std::string id;
    std::string titleKey;
    std::vector<std::string> searchKeys;
};

} // namespace workpane::ui
