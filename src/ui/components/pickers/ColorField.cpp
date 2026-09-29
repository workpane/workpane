#include "ui/components/pickers/ColorField.h"

#include "ui/ButtonState.h"
#include "ui/Painter.h"
#include "ui/Widgets.h"
#include "ui/theme/Theme.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <string>
#include <system_error>

namespace workpane::ui {

ColorField::ColorField(NodeId id) : Component(id) {}

std::string_view ColorField::kind() const {
    return "colorField";
}

void ColorField::readProperties(json::ObjectReader& reader) {
    if (!reader.contains("value")) {
        return;
    }

    std::string value;
    reader.read("value", value);
    const auto parsed = Color::parse(value);

    if (!parsed.has_value()) {
        fail({"ui_color_invalid", "A color is written as a number sign followed by six hexadecimal digits", "colorField.value"});
        return;
    }

    m_value = *parsed;
    m_given = true;
}

Component::Restore ColorField::keep() {
    return kept(m_value, m_given);
}

Result<void> ColorField::validate() const {
    if (!m_given) {
        return Result<void>::failure({"json_field_missing", "A color field shows the color its plugin gives", "colorField.value"});
    }

    return Result<void>::success();
}

ImVec2 ColorField::measureContent(RenderContext& context, float availableWidth) {
    return {std::min(availableWidth, context.metric(ThemeMetric::SettingsControlMinimumWidth)), Widgets::controlHeight(context)};
}

void ColorField::render(RenderContext& context, const ImRect& bounds) {
    const ButtonState state = Widgets::interact(ImGui::GetID("##color"), bounds);
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const float scale = context.scale();
    const float padding = context.metric(ThemeMetric::ControlHorizontalPadding);
    const float swatch = swatchSize * scale;
    const ImVec2 swatchMin(bounds.Min.x + padding, bounds.GetCenter().y - swatch / 2.0F);

    Widgets::frame(context, bounds, context.color(ThemeColor::Raised));
    list.AddRectFilled(swatchMin, ImVec2(swatchMin.x + swatch, swatchMin.y + swatch), Widgets::ink(m_value), context.metric(ThemeMetric::ControlRadius));
    Painter::rectBorder(list, swatchMin, ImVec2(swatchMin.x + swatch, swatchMin.y + swatch), context.metric(ThemeMetric::ControlRadius), context.metric(ThemeMetric::LineWidth), context.color(ThemeColor::BorderStrong));
    Widgets::alignedText(context, list, context.font(ThemeFont::Monospace), ImRect(swatchMin.x + swatch + swatchSpacing * scale, bounds.Min.y, bounds.Max.x - padding, bounds.Max.y), context.color(ThemeColor::Text), m_value.hex(), TextAlign::Start);

    if (ImGui::IsPopupOpen("##picker")) {
        Widgets::focusBorder(context, bounds);
    }

    if (state.pressed) {
        ImGui::OpenPopup("##picker");
    }

    Widgets::placePopup(context, "##picker", bounds);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pickerMargin * scale, pickerMargin * scale));

    if (ImGui::BeginPopup("##picker", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize)) {
        std::array<float, 3> channels{m_value.red / 255.0F, m_value.green / 255.0F, m_value.blue / 255.0F};
        ImGui::SetNextItemWidth(pickerWidth * scale);

        if (ImGui::ColorPicker3("##value", channels.data(), ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_PickerHueBar)) {
            m_value = Color::rgb(static_cast<int>(std::lround(channels[0] * 255.0F)), static_cast<int>(std::lround(channels[1] * 255.0F)), static_cast<int>(std::lround(channels[2] * 255.0F)));
            context.emit(id(), "change", {{"value", m_value.hex()}}, {{"value", "value"}});
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar();
}

} // namespace workpane::ui
