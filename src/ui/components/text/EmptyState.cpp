#include "ui/components/text/EmptyState.h"

#include "ui/WidgetHelper.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

#include <string>

namespace workpane::ui {

EmptyState::EmptyState(NodeId id) : Component(id) {}

std::string_view EmptyState::kind() const {
    return "emptyState";
}

void EmptyState::readProperties(json::ObjectReader& reader) {
    readText(reader, "text", m_text);
    readIcon(reader, "icon", m_icon);
}

Component::Restore EmptyState::keep() {
    return kept(m_text, m_icon);
}

ImVec2 EmptyState::measureContent(RenderContext& context, float availableWidth) {
    const ImVec2 paragraph = WidgetHelper::paragraphSize(context, context.font(ThemeFont::Interface), context.text(m_text), availableWidth);
    const float icon = m_icon.has_value() ? (emptyStateIconSize + emptyStateIconSpacing) * context.scale() : 0.0F;
    return {availableWidth, paragraph.y + icon};
}

void EmptyState::render(RenderContext& context, const ImRect& bounds) {
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const FontRole font = context.font(ThemeFont::Interface);
    const std::string& text = context.text(m_text);
    const ImVec2 paragraph = WidgetHelper::paragraphSize(context, font, text, bounds.GetWidth());
    const float scale = context.scale();
    const float icon = m_icon.has_value() ? emptyStateIconSize * scale : 0.0F;
    const float gap = m_icon.has_value() ? emptyStateIconSpacing * scale : 0.0F;
    float y = bounds.Min.y + (bounds.GetHeight() - paragraph.y - icon - gap) / 2.0F;

    if (m_icon.has_value()) {
        IconCatalog::draw(context.fonts(), list, *m_icon, ImFloor(ImVec2(bounds.GetCenter().x - icon / 2.0F, y)), icon, context.color(ThemeColor::TextMuted));
        y += icon + gap;
    }

    WidgetHelper::paragraph(context, list, font, ImRect(bounds.Min.x, y, bounds.Max.x, y + paragraph.y), context.color(ThemeColor::TextMuted), text, TextAlign::Center);
}

} // namespace workpane::ui
