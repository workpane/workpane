#include "ui/components/choices/RadioGroup.h"

#include "ui/Widgets.h"
#include "ui/components/choices/ChoiceOptions.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace workpane::ui {

RadioGroup::RadioGroup(NodeId id) : Component(id) {}

std::string_view RadioGroup::kind() const {
    return "radioGroup";
}

Alignment RadioGroup::defaultColumnAlignment() const {
    return Alignment::Start;
}

void RadioGroup::readProperties(json::ObjectReader& reader) {
    reader.read("value", m_value, json::Presence::Optional);
    reader.readChoice("orientation", m_axis, {{"vertical", Axis::Vertical}, {"horizontal", Axis::Horizontal}}, json::Presence::Optional);

    if (!reader.contains("options")) {
        return;
    }

    const json::Json* options = &json::ObjectReader::absent();
    reader.readArray("options", options);
    auto parsed = ChoiceOptions::parse(*options, kind());

    if (!parsed.hasValue()) {
        fail(parsed.error());
        return;
    }

    m_options = std::move(parsed.value());
}

Component::Restore RadioGroup::keep() {
    return kept(m_value, m_axis, m_options);
}

Result<void> RadioGroup::validate() const {
    // clang-format off
    const bool known = m_value.empty() || std::ranges::any_of(m_options, [this](const ChoiceOption& option) { return option.value == m_value; });
    // clang-format on

    if (!known) {
        return Result<void>::failure({"ui_option_unknown", "A choice selects a value none of its options carries", m_value});
    }

    return Result<void>::success();
}

ImVec2 RadioGroup::measureContent(RenderContext& context, float) {
    const float height = Widgets::controlHeight(context);

    if (m_axis == Axis::Vertical) {
        float width = 0.0F;

        for (const auto& option : m_options) {
            width = std::max(width, optionWidth(context, option));
        }

        return {width, height * static_cast<float>(m_options.size())};
    }

    float width = 0.0F;

    for (const auto& option : m_options) {
        width += optionWidth(context, option) + radioSpacing * context.scale();
    }

    return {std::max(0.0F, width - radioSpacing * context.scale()), height};
}

void RadioGroup::render(RenderContext& context, const ImRect& bounds) {
    const float height = Widgets::controlHeight(context);
    ImVec2 cursor = bounds.Min;

    for (std::size_t index = 0; index < m_options.size(); ++index) {
        const ChoiceOption& option = m_options[index];
        const float width = optionWidth(context, option);
        const ImRect row(cursor, ImVec2(cursor.x + width, cursor.y + height));
        ImGui::PushID(option.value.data(), option.value.data() + option.value.size());

        if (Widgets::radio(context, "##radio", row, option.value == m_value, context.text(option.text)) && option.value != m_value) {
            m_value = option.value;
            context.emit(id(), "change", {{"value", m_value}}, {{"value", "value"}});
        }

        ImGui::PopID();

        if (m_axis == Axis::Vertical) {
            cursor.y += height;
        } else {
            cursor.x += width + radioSpacing * context.scale();
        }
    }
}

float RadioGroup::optionWidth(RenderContext& context, const ChoiceOption& option) const {
    return Widgets::checkboxSize(context, context.text(option.text)).x;
}

} // namespace workpane::ui
