#include "ui/components/indicators/Badge.h"

#include "ui/WidgetHelper.h"
#include "ui/components/indicators/Tones.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

#include <cmath>

namespace workpane::ui {

Badge::Badge(NodeId id) : Component(id) {}

std::string_view Badge::kind() const {
    return "badge";
}

Alignment Badge::defaultColumnAlignment() const {
    return Alignment::Start;
}

void Badge::readProperties(json::ObjectReader& reader) {
    readText(reader, "text", m_text);
    Tones::read(reader, "tone", m_tone);
}

Component::Restore Badge::keep() {
    return kept(m_text, m_tone);
}

ImVec2 Badge::measureContent(RenderContext& context, float) {
    FontRole font = context.font(ThemeFont::Interface);
    font.face = FontFace::SemiBold;
    const ImVec2 text = WidgetHelper::textSize(context, font, context.text(m_text));

    return {std::ceil(text.x + context.metric(ThemeMetric::BadgeHorizontalPadding) * 2.0F), std::ceil(WidgetHelper::lineHeight(context, font) + context.metric(ThemeMetric::BadgeVerticalPadding) * 2.0F)};
}

void Badge::render(RenderContext& context, const ImRect& bounds) {
    ImDrawList& list = *ImGui::GetWindowDrawList();
    FontRole font = context.font(ThemeFont::Interface);
    font.face = FontFace::SemiBold;
    const bool neutral = m_tone == Tone::Neutral;
    list.AddRectFilled(bounds.Min, bounds.Max, WidgetHelper::ink(context.color(neutral ? Tones::background(m_tone) : Tones::color(m_tone))), context.metric(ThemeMetric::ControlRadius));
    WidgetHelper::alignedText(context, list, font, bounds, context.color(neutral ? Tones::text(m_tone) : Tones::ink(m_tone)), context.text(m_text), TextAlign::Center);
}

} // namespace workpane::ui
