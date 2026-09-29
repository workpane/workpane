#include "ui/components/buttons/Chip.h"

#include "ui/ButtonState.h"
#include "ui/Painter.h"
#include "ui/Widgets.h"
#include "ui/theme/Theme.h"

#include <cmath>

namespace workpane::ui {

Chip::Chip(NodeId id) : Component(id) {}

std::string_view Chip::kind() const {
    return "chip";
}

Alignment Chip::defaultColumnAlignment() const {
    return Alignment::Start;
}

void Chip::readProperties(json::ObjectReader& reader) {
    readText(reader, "text", m_text);
    reader.read("checked", m_checked, json::Presence::Optional);
}

Component::Restore Chip::keep() {
    return kept(m_text, m_checked);
}

ImVec2 Chip::measureContent(RenderContext& context, float) {
    const float padding = context.metric(ThemeMetric::BadgeHorizontalPadding);
    return {std::ceil(Widgets::textSize(context, context.font(ThemeFont::Interface), context.text(m_text)).x + padding * 2.0F), context.metric(ThemeMetric::BadgeRadius) * 2.0F};
}

// A chip is the small rounded choice with the strong border, filled with the strong accent while hovered or chosen.
void Chip::render(RenderContext& context, const ImRect& bounds) {
    const ButtonState state = Widgets::interact(ImGui::GetID("##chip"), bounds);
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const float radius = context.metric(ThemeMetric::BadgeRadius);
    const bool filled = state.hovered || m_checked;

    if (filled) {
        list.AddRectFilled(bounds.Min, bounds.Max, Widgets::ink(context.color(ThemeColor::AccentStrong)), radius);
    } else {
        Painter::rectBorder(list, bounds.Min, bounds.Max, radius, context.metric(ThemeMetric::LineWidth), context.color(ThemeColor::BorderStrong));
    }

    Widgets::alignedText(context, list, context.font(ThemeFont::Interface), bounds, context.color(filled ? ThemeColor::OnAccent : ThemeColor::Text), context.text(m_text), TextAlign::Center);

    if (state.pressed) {
        context.emit(id(), "click");
    }
}

} // namespace workpane::ui
