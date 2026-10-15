#include "ui/components/settings/SettingsActions.h"

#include "ui/components/containers/Axis.h"

namespace workpane::ui {

SettingsActions::SettingsActions(NodeId id) : LinearContainer(id, Axis::Horizontal) {
    m_spacing = actionSpacing;
}

std::string_view SettingsActions::kind() const {
    return "settingsActions";
}

void SettingsActions::readProperties(json::ObjectReader&) {}

} // namespace workpane::ui
