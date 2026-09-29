#include "ui/components/inputs/NumberField.h"

#include "ui/IconCatalog.h"
#include "ui/TextFieldOptions.h"
#include "ui/TextFieldResult.h"
#include "ui/Widgets.h"
#include "ui/components/inputs/NumericInput.h"
#include "ui/theme/Theme.h"

#include <algorithm>
#include <cmath>

namespace workpane::ui {

NumberField::NumberField(NodeId id) : Component(id) {}

std::string_view NumberField::kind() const {
    return "numberField";
}

void NumberField::readProperties(json::ObjectReader& reader) {
    reader.readNumber("value", m_value, -NumericInput::largestMagnitude, NumericInput::largestMagnitude, json::Presence::Optional);
    reader.readNumber("minimum", m_minimum, -NumericInput::largestMagnitude, NumericInput::largestMagnitude, json::Presence::Optional);
    reader.readNumber("maximum", m_maximum, -NumericInput::largestMagnitude, NumericInput::largestMagnitude, json::Presence::Optional);
    reader.readNumber("step", m_step, 0.0, NumericInput::largestMagnitude, json::Presence::Optional);
    reader.readInteger("decimals", m_decimals, 0, 8, json::Presence::Optional);
    m_editing = false;
}

Component::Restore NumberField::keep() {
    return kept(m_value, m_minimum, m_maximum, m_step, m_decimals, m_editing);
}

Result<void> NumberField::validate() const {
    return NumericInput::validateRange(m_value, m_minimum, m_maximum, m_step, m_decimals, kind());
}

ImVec2 NumberField::measureContent(RenderContext& context, float availableWidth) {
    return {std::min(availableWidth, context.metric(ThemeMetric::SettingsControlMinimumWidth)), Widgets::controlHeight(context)};
}

void NumberField::render(RenderContext& context, const ImRect& bounds) {
    const float button = Widgets::controlHeight(context);
    const float spacing = stepperSpacing * context.scale();
    const ImRect field(bounds.Min, ImVec2(bounds.Max.x - (button + spacing) * 2.0F, bounds.Max.y));
    const ImRect decrease(ImVec2(field.Max.x + spacing, bounds.Min.y), ImVec2(field.Max.x + spacing + button, bounds.Max.y));
    const ImRect increase(ImVec2(decrease.Max.x + spacing, bounds.Min.y), bounds.Max);

    if (!m_editing) {
        m_text = formatted();
    }

    const TextFieldOptions options{.placeholder = {}, .numeric = true};
    const TextFieldResult result = Widgets::textField(context, "##number", field, m_text, options);
    m_editing = result.active;

    // A typed value is committed when the field is left, and a value that is not a number puts the last committed one back.
    if (result.deactivated) {
        double typed = 0.0;

        if (NumericInput::parse(m_text, typed)) {
            commit(context, typed);
        }

        m_text = formatted();
    }

    if (Widgets::button(context, "##decrease", decrease, {}, Icon::Minus, ButtonVariant::Default)) {
        commit(context, m_value - m_step);
    }

    if (Widgets::button(context, "##increase", increase, {}, Icon::Add, ButtonVariant::Default)) {
        commit(context, m_value + m_step);
    }
}

std::string NumberField::formatted() const {
    return NumericInput::format(m_value, m_decimals);
}

void NumberField::commit(RenderContext& context, double value) {
    const double factor = std::pow(10.0, static_cast<double>(m_decimals));
    const double clamped = std::clamp(std::round(value * factor) / factor, m_minimum, m_maximum);

    if (clamped == m_value) {
        return;
    }

    m_value = clamped;
    m_text = formatted();
    context.emit(id(), "change", {{"value", m_value}}, {{"value", "value"}});
}

} // namespace workpane::ui
