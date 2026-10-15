#include "ui/components/inputs/Slider.h"

#include "ui/WidgetHelper.h"
#include "ui/components/inputs/NumericInput.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

#include <algorithm>

namespace workpane::ui {

Slider::Slider(NodeId id) : Component(id) {}

std::string_view Slider::kind() const {
    return "slider";
}

void Slider::readProperties(json::ObjectReader& reader) {
    reader.readNumber("value", m_value, -NumericInput::largestMagnitude, NumericInput::largestMagnitude, json::Presence::Optional);
    reader.readNumber("minimum", m_minimum, -NumericInput::largestMagnitude, NumericInput::largestMagnitude, json::Presence::Optional);
    reader.readNumber("maximum", m_maximum, -NumericInput::largestMagnitude, NumericInput::largestMagnitude, json::Presence::Optional);
    reader.readNumber("step", m_step, 0.0, NumericInput::largestMagnitude, json::Presence::Optional);
    reader.readInteger("decimals", m_decimals, 0, 8, json::Presence::Optional);
    reader.read("showValue", m_showValue, json::Presence::Optional);
}

Component::Restore Slider::keep() {
    return kept(m_value, m_minimum, m_maximum, m_step, m_decimals, m_showValue);
}

Result<void> Slider::validate() const {
    return NumericInput::validateRange(m_value, m_minimum, m_maximum, m_step, m_decimals, kind());
}

ImVec2 Slider::measureContent(RenderContext& context, float availableWidth) {
    return {std::min(availableWidth, context.metric(ThemeMetric::SettingsControlMinimumWidth)), WidgetHelper::controlHeight(context)};
}

void Slider::render(RenderContext& context, const ImRect& bounds) {
    const FontRole font = context.font(ThemeFont::Interface);
    const float valueWidth = m_showValue ? std::max(WidgetHelper::textSize(context, font, NumericInput::format(m_minimum, m_decimals)).x, WidgetHelper::textSize(context, font, NumericInput::format(m_maximum, m_decimals)).x) + sliderValueSpacing * context.scale() : 0.0F;
    const ImRect track(bounds.Min, ImVec2(bounds.Max.x - valueWidth, bounds.Max.y));

    if (WidgetHelper::slider(context, "##slider", track, m_value, m_minimum, m_maximum, m_step)) {
        m_value = NumericInput::rounded(m_value, m_decimals);
        context.emit(id(), "change", {{"value", m_value}}, {{"value", "value"}});
    }

    if (m_showValue) {
        WidgetHelper::alignedText(context, *ImGui::GetWindowDrawList(), font, ImRect(ImVec2(track.Max.x, bounds.Min.y), bounds.Max), context.color(ThemeColor::Text), NumericInput::format(m_value, m_decimals), TextAlign::End);
    }
}

} // namespace workpane::ui
