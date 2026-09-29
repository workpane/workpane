#include "ui/components/containers/Divider.h"

#include "ui/Painter.h"
#include "ui/theme/Theme.h"

namespace workpane::ui {

Divider::Divider(NodeId id) : Component(id) {}

std::string_view Divider::kind() const {
    return "divider";
}

Alignment Divider::defaultRowAlignment() const {
    return Alignment::Stretch;
}

void Divider::readProperties(json::ObjectReader& reader) {
    reader.readChoice("orientation", m_axis, {{"horizontal", Axis::Horizontal}, {"vertical", Axis::Vertical}}, json::Presence::Optional);
}

Component::Restore Divider::keep() {
    return kept(m_axis);
}

ImVec2 Divider::measureContent(RenderContext& context, float availableWidth) {
    const float line = context.metric(ThemeMetric::LineWidth);
    return m_axis == Axis::Horizontal ? ImVec2(availableWidth, line) : ImVec2(line, 0.0F);
}

void Divider::render(RenderContext& context, const ImRect& bounds) {
    ImDrawList& list = *ImGui::GetWindowDrawList();

    if (m_axis == Axis::Horizontal) {
        Painter::horizontalDivider(list, bounds.Min, bounds.GetWidth(), context.metric(ThemeMetric::LineWidth), context.color(ThemeColor::Border));
        return;
    }

    Painter::verticalDivider(list, bounds.Min, bounds.GetHeight(), context.metric(ThemeMetric::LineWidth), context.color(ThemeColor::Border));
}

} // namespace workpane::ui
