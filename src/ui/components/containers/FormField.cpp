#include "ui/components/containers/FormField.h"

#include "ui/WidgetHelper.h"
#include "ui/model/CommonProperties.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace workpane::ui {

FormField::FormField(NodeId id) : Component(id) {}

std::string_view FormField::kind() const {
    return "formField";
}

std::size_t FormField::childLimit() const {
    return 1;
}

Result<void> FormField::validateChildren() const {
    if (children().size() != 1) {
        return Result<void>::failure({"ui_form_field_control", "A form field carries exactly one control", std::to_string(children().size())});
    }

    return Result<void>::success();
}

void FormField::readProperties(json::ObjectReader& reader) {
    readText(reader, "label", m_label);
    readText(reader, "hint", m_hint);
}

Component::Restore FormField::keep() {
    return kept(m_label, m_hint);
}

std::string FormField::caption(RenderContext& context) const {
    return context.text(m_label) + ":";
}

// A field is as wide as its control, or as its label when that is wider, so a field beside another in a row leaves it the rest of the room.
ImVec2 FormField::measureContent(RenderContext& context, float availableWidth) {
    const Geometry shape = geometry(context, availableWidth);
    const float label = std::min(std::ceil(WidgetHelper::textSize(context, context.font(ThemeFont::Interface), caption(context)).x), availableWidth);
    return {std::max(shape.control.x, label), shape.label + context.metric(ThemeMetric::ControlVerticalPadding) + shape.control.y + shape.hint};
}

void FormField::render(RenderContext& context, const ImRect& bounds) {
    const Geometry shape = geometry(context, bounds.GetWidth());
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const float gap = context.metric(ThemeMetric::ControlVerticalPadding);
    WidgetHelper::paragraph(context, list, context.font(ThemeFont::Interface), ImRect(bounds.Min.x, bounds.Min.y, bounds.Max.x, bounds.Min.y + shape.label), context.color(WidgetHelper::disabled() ? ThemeColor::TextMuted : ThemeColor::Text), caption(context), TextAlign::Start);

    const float top = std::floor(bounds.Min.y + shape.label + gap);
    children().front()->draw(context, ImRect(bounds.Min.x, top, bounds.Min.x + shape.control.x, top + shape.control.y));

    if (!m_hint.empty()) {
        const float hintTop = top + shape.control.y + gap;
        WidgetHelper::paragraph(context, list, context.font(ThemeFont::Caption), ImRect(bounds.Min.x, hintTop, bounds.Max.x, bounds.Max.y), context.color(ThemeColor::TextMuted), context.text(m_hint), TextAlign::Start);
    }
}

// A control that stretches takes the whole width under its label, and any other keeps the width it measures.
FormField::Geometry FormField::geometry(RenderContext& context, float availableWidth) {
    Geometry shape;
    Component& control = *children().front();
    shape.label = WidgetHelper::paragraphSize(context, context.font(ThemeFont::Interface), caption(context), availableWidth).y;
    shape.control = control.measure(context, availableWidth);
    shape.control.x = control.columnAlignment() == Alignment::Stretch ? availableWidth : std::min(shape.control.x, availableWidth);
    shape.control.y = std::max(shape.control.y, WidgetHelper::controlHeight(context));

    if (!m_hint.empty()) {
        shape.hint = context.metric(ThemeMetric::ControlVerticalPadding) + WidgetHelper::paragraphSize(context, context.font(ThemeFont::Caption), context.text(m_hint), availableWidth).y;
    }

    return shape;
}

} // namespace workpane::ui
