#include "ui/shell/SettingsGroup.h"

namespace workpane::ui {

std::string SettingsGroup::surface(const SettingsSection& section) const {
    return "settings:" + owner + ":" + id + ":" + section.id;
}

} // namespace workpane::ui
