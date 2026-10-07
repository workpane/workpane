#include "ui/components/containers/Card.h"

#include "ui/Painter.h"
#include "ui/WidgetHelper.h"
#include "ui/theme/Theme.h"

#include <algorithm>

namespace workpane::ui {

Card::Card(NodeId id) : LinearContainer(id, Axis::Vertical) {
    m_padding = {14.0F, 16.0F, 14.0F, 16.0F};
    m_spacing = 6.0F;
    m_background = ThemeColor::Panel;
}

std::string_view Card::kind() const {
    return "card";
}

void Card::readProperties(json::ObjectReader& reader) {
    LinearContainer::readProperties(reader);
    readText(reader, "title", m_title);
    readIcon(reader, "icon", m_icon);
    readColor(reader, "outline", m_outline);

    double radius = m_radius;
    reader.readNumber("radius", radius, 0.0, 64.0, json::Presence::Optional);
    m_radius = static_cast<float>(radius);
}

Component::Restore Card::keep() {
    // clang-format off
    return [container = LinearContainer::keep(), own = kept(m_title, m_icon, m_outline, m_radius)]() {
        container();
        own();
    };
    // clang-format on
}

ImVec2 Card::measureContent(RenderContext& context, float availableWidth) {
    const ImVec2 content = LinearContainer::measureContent(context, availableWidth);
    const float title = m_title.empty() ? 0.0F : WidgetHelper::textSize(context, context.font(ThemeFont::SectionTitle), context.text(m_title)).x + (m_icon.has_value() ? context.metric(ThemeMetric::SmallIconSize) + cardIconSpacing * context.scale() : 0.0F);
    return {std::max(content.x, title + contentInsets(context).horizontal()), content.y};
}

void Card::paintSurface(RenderContext& context, const ImRect& bounds) {
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const float radius = m_radius * context.scale();

    if (m_background.has_value()) {
        list.AddRectFilled(bounds.Min, bounds.Max, WidgetHelper::ink(context.color(*m_background)), radius);
    }

    if (m_outline.has_value()) {
        Painter::rectBorder(list, bounds.Min, bounds.Max, radius, context.metric(ThemeMetric::LineWidth), context.color(*m_outline));
    }

    paintBorders(context, bounds);

    if (m_title.empty()) {
        return;
    }

    const float scale = context.scale();
    const float height = headerHeight(context);
    float x = bounds.Min.x + m_padding.left * scale;
    const float top = bounds.Min.y + m_padding.top * scale;

    if (m_icon.has_value()) {
        const float icon = context.metric(ThemeMetric::SmallIconSize);
        IconCatalog::draw(context.fonts(), list, *m_icon, ImFloor(ImVec2(x, top + (height - icon) / 2.0F)), icon, context.color(ThemeColor::TextMuted));
        x += icon + cardIconSpacing * scale;
    }

    WidgetHelper::alignedText(context, list, context.font(ThemeFont::SectionTitle), ImRect(x, top, bounds.Max.x - m_padding.right * scale, top + height), context.color(ThemeColor::Text), context.text(m_title), TextAlign::Start);
}

// A double click on the card itself, outside every control it holds, reports activate from the innermost card under the pointer.
void Card::render(RenderContext& context, const ImRect& bounds) {
    LinearContainer::render(context, bounds);

    const bool doubled = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(bounds.Min, bounds.Max) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::IsAnyItemHovered();

    if (doubled && context.claimActivation()) {
        context.emit(id(), "activate");
    }
}

Insets Card::contentInsets(RenderContext& context) const {
    Insets insets = LinearContainer::contentInsets(context);

    if (!m_title.empty()) {
        insets.top += headerHeight(context) + cardHeaderSpacing * context.scale();
    }

    return insets;
}

float Card::headerHeight(RenderContext& context) const {
    return std::max(context.metric(ThemeMetric::SmallIconSize), WidgetHelper::fontSize(context, context.font(ThemeFont::SectionTitle)) * 1.3F);
}

} // namespace workpane::ui
