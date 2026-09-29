#include "ui/components/settings/SettingsForm.h"

#include "ui/components/containers/Axis.h"
#include "ui/theme/Theme.h"

namespace workpane::ui {

SettingsForm::SettingsForm(NodeId id) : LinearContainer(id, Axis::Vertical) {
    m_spacing = formRowSpacing;
}

std::string_view SettingsForm::kind() const {
    return "settingsForm";
}

// The inset is the theme metric and never a property, so no owner can write margins over the shared shape.
void SettingsForm::readProperties(json::ObjectReader&) {}

Insets SettingsForm::contentInsets(RenderContext& context) const {
    const float inset = context.metric(ThemeMetric::SettingsHorizontalPadding);
    return {0.0F, inset, 0.0F, inset};
}

} // namespace workpane::ui
