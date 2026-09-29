#include "ui/components/indicators/IconGraphic.h"

#include "ui/Widgets.h"
#include "ui/components/indicators/IndicatorSizes.h"

#include <algorithm>

namespace workpane::ui {

IconGraphic::IconGraphic(NodeId id) : Component(id) {}

std::string_view IconGraphic::kind() const {
    return "icon";
}

Alignment IconGraphic::defaultColumnAlignment() const {
    return Alignment::Start;
}

void IconGraphic::readProperties(json::ObjectReader& reader) {
    readIcon(reader, "name", m_icon);
    readColor(reader, "color", m_color);
    IndicatorSizes::read(reader, "size", m_size);
}

Component::Restore IconGraphic::keep() {
    return kept(m_icon, m_color, m_size);
}

Result<void> IconGraphic::validate() const {
    if (!m_icon.has_value()) {
        return Result<void>::failure({"ui_icon_missing", "An icon component names no icon", "icon.name"});
    }

    return Result<void>::success();
}

ImVec2 IconGraphic::measureContent(RenderContext& context, float) {
    const float size = m_size * context.scale();
    return {size, size};
}

void IconGraphic::render(RenderContext& context, const ImRect& bounds) {
    const float size = std::min(bounds.GetWidth(), bounds.GetHeight());
    const ImVec2 origin(bounds.GetCenter().x - size / 2.0F, bounds.GetCenter().y - size / 2.0F);
    IconCatalog::draw(context.fonts(), *ImGui::GetWindowDrawList(), *m_icon, ImFloor(origin), size, context.color(Widgets::disabled() ? ThemeColor::TextMuted : m_color.value_or(ThemeColor::TextMuted)));
}

} // namespace workpane::ui
