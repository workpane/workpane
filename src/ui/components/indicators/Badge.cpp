#include "ui/components/indicators/Badge.h"

#include "ui/Widgets.h"
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
    const ImVec2 text = Widgets::textSize(context, font, context.text(m_text));

    return {std::ceil(text.x + context.metric(ThemeMetric::BadgeHorizontalPadding) * 2.0F), std::ceil(Widgets::lineHeight(context, font) + context.metric(ThemeMetric::BadgeVerticalPadding) * 2.0F)};
}

// A neutral badge sits on the background of its tone in its text color, and any other badge is filled with its tone and written in the ink made for that fill.
void Badge::render(RenderContext& context, const ImRect& bounds) {
    ImDrawList& list = *ImGui::GetWindowDrawList();
    FontRole font = context.font(ThemeFont::Interface);
    font.face = FontFace::SemiBold;
    const bool neutral = m_tone == Tone::Neutral;
    list.AddRectFilled(bounds.Min, bounds.Max, Widgets::ink(context.color(neutral ? Tones::background(m_tone) : Tones::color(m_tone))), context.metric(ThemeMetric::ControlRadius));
    Widgets::alignedText(context, list, font, bounds, context.color(neutral ? Tones::text(m_tone) : Tones::ink(m_tone)), context.text(m_text), TextAlign::Center);
}

} // namespace workpane::ui
