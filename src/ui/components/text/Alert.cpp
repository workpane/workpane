#include "ui/components/text/Alert.h"

#include "ui/Widgets.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

#include <string>

namespace workpane::ui {

Alert::Alert(NodeId id) : Component(id) {}

std::string_view Alert::kind() const {
    return "alert";
}

// The text keeps the padding of a control away from the edges, and the rule adds its own width on the left.
ImVec2 Alert::inset(const RenderContext& context) {
    return {context.metric(ThemeMetric::ControlHorizontalPadding) + ruleWidth * context.scale(), context.metric(ThemeMetric::ControlVerticalPadding)};
}

// A problem shown or rewritten after the alert was mounted asks to be scrolled into view, while an alert mounted visible leaves the scroll where the reader has it.
void Alert::readProperties(json::ObjectReader& reader) {
    const bool reported = reader.contains("text") || reader.contains("visible");
    readText(reader, "text", m_text);
    Tones::read(reader, "tone", m_tone);
    m_reveal = m_reveal || (m_mounted && reported && common().visible);
    m_mounted = true;
}

Component::Restore Alert::keep() {
    return kept(m_text, m_tone, m_reveal, m_mounted);
}

ImVec2 Alert::measureContent(RenderContext& context, float availableWidth) {
    const ImVec2 padding = inset(context);
    const float room = availableWidth - padding.x - context.metric(ThemeMetric::ControlHorizontalPadding);
    const ImVec2 paragraph = Widgets::paragraphSize(context, context.font(ThemeFont::Interface), context.text(m_text), room);
    return {availableWidth, paragraph.y + padding.y * 2.0F};
}

bool Alert::revealing() const {
    return m_reveal;
}

void Alert::render(RenderContext& context, const ImRect& bounds) {
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const ImVec2 padding = inset(context);
    const ImRect text(bounds.Min.x + padding.x, bounds.Min.y + padding.y, bounds.Max.x - context.metric(ThemeMetric::ControlHorizontalPadding), bounds.Max.y - padding.y);
    list.AddRectFilled(bounds.Min, bounds.Max, Widgets::ink(context.color(Tones::background(m_tone))));
    list.AddRectFilled(bounds.Min, ImVec2(bounds.Min.x + ruleWidth * context.scale(), bounds.Max.y), Widgets::ink(context.color(Tones::color(m_tone))));
    Widgets::paragraph(context, list, context.font(ThemeFont::Interface), text, context.color(Tones::text(m_tone)), context.text(m_text), TextAlign::Start);

    if (m_reveal) {
        ImGui::ScrollToRect(ImGui::GetCurrentWindow(), bounds, ImGuiScrollFlags_KeepVisibleEdgeY);
        m_reveal = false;
    }
}

} // namespace workpane::ui
