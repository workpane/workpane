#include "ui/components/indicators/Progress.h"

#include "ui/WidgetHelper.h"
#include "ui/components/indicators/Tones.h"
#include "ui/theme/Theme.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace workpane::ui {

Progress::Progress(NodeId id) : Component(id) {}

std::string_view Progress::kind() const {
    return "progress";
}

void Progress::readProperties(json::ObjectReader& reader) {
    reader.readNumber("value", m_value, 0.0, 1.0, json::Presence::Optional);
    readText(reader, "text", m_text);
    Tones::read(reader, "tone", m_tone);
    reader.read("showText", m_showText, json::Presence::Optional);
}

Component::Restore Progress::keep() {
    return kept(m_value, m_text, m_tone, m_showText);
}

ImVec2 Progress::measureContent(RenderContext& context, float availableWidth) {
    return {availableWidth, progressHeight * context.scale()};
}

void Progress::render(RenderContext& context, const ImRect& bounds) {
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const float radius = context.metric(ThemeMetric::ControlRadius);
    list.AddRectFilled(bounds.Min, bounds.Max, WidgetHelper::ink(context.color(ThemeColor::Raised)), radius);

    const Tone tone = m_tone == Tone::Neutral ? Tone::Accent : m_tone;
    const float filled = bounds.GetWidth() * static_cast<float>(m_value);

    if (filled > 0.0F) {
        list.AddRectFilled(bounds.Min, ImVec2(bounds.Min.x + filled, bounds.Max.y), WidgetHelper::ink(context.color(Tones::color(tone))), radius);
    }

    if (!m_showText) {
        return;
    }

    std::string text = context.text(m_text);

    if (m_text.empty()) {
        std::array<char, 16> buffer{};
        std::snprintf(buffer.data(), buffer.size(), "%d%%", static_cast<int>(std::lround(m_value * 100.0)));
        text = buffer.data();
    }

    // The part of the text over the fill is written in the ink of the tone and the rest in the text color, so every letter stays readable.
    const float edge = std::floor(bounds.Min.x + filled);
    list.PushClipRect(bounds.Min, ImVec2(edge, bounds.Max.y), true);
    WidgetHelper::alignedText(context, list, context.font(ThemeFont::Interface), bounds, context.color(Tones::ink(tone)), text, TextAlign::Center);
    list.PopClipRect();
    list.PushClipRect(ImVec2(edge, bounds.Min.y), bounds.Max, true);
    WidgetHelper::alignedText(context, list, context.font(ThemeFont::Interface), bounds, context.color(ThemeColor::Text), text, TextAlign::Center);
    list.PopClipRect();
}

} // namespace workpane::ui
