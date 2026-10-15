#include "ui/components/indicators/Avatar.h"

#include "ui/WidgetHelper.h"
#include "ui/components/indicators/IndicatorSizes.h"

#include <algorithm>

namespace workpane::ui {

Avatar::Avatar(NodeId id) : Component(id) {}

std::string_view Avatar::kind() const {
    return "avatar";
}

Alignment Avatar::defaultColumnAlignment() const {
    return Alignment::Start;
}

void Avatar::readProperties(json::ObjectReader& reader) {
    readIcon(reader, "icon", m_icon);
    readColor(reader, "fill", m_fill);
    readColor(reader, "ink", m_ink);
    IndicatorSizes::read(reader, "size", m_size);
}

Component::Restore Avatar::keep() {
    return kept(m_icon, m_fill, m_ink, m_size);
}

Result<void> Avatar::validate() const {
    if (!m_icon.has_value()) {
        return Result<void>::failure({"ui_icon_missing", "An avatar names no icon", "avatar.icon"});
    }

    return Result<void>::success();
}

ImVec2 Avatar::measureContent(RenderContext& context, float) {
    const float size = m_size * context.scale();
    return {size, size};
}

void Avatar::render(RenderContext& context, const ImRect& bounds) {
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const float diameter = std::min(bounds.GetWidth(), bounds.GetHeight());
    const ImVec2 center = bounds.GetCenter();
    list.AddCircleFilled(center, diameter / 2.0F, WidgetHelper::ink(context.color(m_fill.value_or(ThemeColor::Accent))));

    const float icon = diameter * 0.5F;
    IconCatalog::draw(context.fonts(), list, *m_icon, ImFloor(ImVec2(center.x - icon / 2.0F, center.y - icon / 2.0F)), icon, context.color(m_ink.value_or(ThemeColor::OnAccent)));
}

} // namespace workpane::ui
