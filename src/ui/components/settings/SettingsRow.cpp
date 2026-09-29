#include "ui/components/settings/SettingsRow.h"

#include "ui/Widgets.h"
#include "ui/model/CommonProperties.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace workpane::ui {

SettingsRow::SettingsRow(NodeId id) : Component(id) {}

std::string_view SettingsRow::kind() const {
    return "settingsRow";
}

std::size_t SettingsRow::childLimit() const {
    return 1;
}

Result<void> SettingsRow::validateChildren() const {
    if (children().size() != 1) {
        return Result<void>::failure({"ui_settings_row_control", "A settings row carries exactly one control", std::to_string(children().size())});
    }

    return Result<void>::success();
}

void SettingsRow::readProperties(json::ObjectReader& reader) {
    readText(reader, "label", m_label);
    readText(reader, "hint", m_hint);
}

Component::Restore SettingsRow::keep() {
    return kept(m_label, m_hint);
}

ImVec2 SettingsRow::measureContent(RenderContext& context, float availableWidth) {
    const Geometry shape = geometry(context, availableWidth);
    return {availableWidth, std::max(shape.captionHeight, shape.line + shape.hintHeight)};
}

void SettingsRow::render(RenderContext& context, const ImRect& bounds) {
    const Geometry shape = geometry(context, bounds.GetWidth());
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const FontRole caption = context.font(ThemeFont::Interface);
    const float captionTop = shape.captionHeight <= shape.line ? bounds.Min.y + (shape.line - shape.captionHeight) / 2.0F : bounds.Min.y;
    Widgets::paragraph(context, list, caption, ImRect(bounds.Min.x, captionTop, bounds.Min.x + shape.captionWidth, captionTop + shape.captionHeight), context.color(Widgets::disabled() ? ThemeColor::TextMuted : ThemeColor::Text), context.text(m_label), TextAlign::Start);

    const float left = bounds.Max.x - shape.controlWidth;
    const float top = bounds.Min.y + (shape.line - shape.control.y) / 2.0F;
    children().front()->draw(context, ImRect(std::floor(left), std::floor(top), std::floor(left) + shape.controlWidth, std::floor(top) + shape.control.y));

    // Every line of the hint ends where the control above it ends, since each control of a settings row stands against the right edge.
    if (!m_hint.empty()) {
        const float hintTop = bounds.Min.y + shape.line + context.metric(ThemeMetric::ControlVerticalPadding);
        Widgets::paragraph(context, list, context.font(ThemeFont::Caption), ImRect(bounds.Max.x - shape.hintWidth, hintTop, bounds.Max.x, hintTop + shape.hintHeight), context.color(ThemeColor::TextMuted), context.text(m_hint), TextAlign::End);
    }
}

// A caption asks for the width its own words need and wraps only past the readable bound, and a control that carries text and every hint take the shared readable width.
SettingsRow::Geometry SettingsRow::geometry(RenderContext& context, float availableWidth) {
    Geometry shape;
    const FontRole caption = context.font(ThemeFont::Interface);
    const std::string& label = context.text(m_label);
    Component& control = *children().front();
    const float spacing = captionSpacing * context.scale();
    shape.captionWidth = std::min(std::ceil(Widgets::textSize(context, caption, label).x), context.metric(ThemeMetric::SettingsLabelMaximumWidth));
    const float room = availableWidth - shape.captionWidth - spacing;
    const float readable = std::clamp(room, std::min(room, context.metric(ThemeMetric::SettingsControlMinimumWidth)), context.metric(ThemeMetric::SettingsControlMaximumWidth));

    if (control.columnAlignment() == Alignment::Stretch) {
        shape.controlWidth = readable;
        shape.control = control.measure(context, shape.controlWidth);
        shape.control.x = shape.controlWidth;
    } else {
        shape.control = control.measure(context, std::max(0.0F, availableWidth - shape.captionWidth - spacing));
        shape.controlWidth = shape.control.x;
    }

    shape.captionWidth = std::max(0.0F, std::min(shape.captionWidth, availableWidth - shape.controlWidth - spacing));
    shape.captionHeight = Widgets::paragraphSize(context, caption, label, shape.captionWidth).y;
    shape.line = std::max(shape.control.y, Widgets::controlHeight(context));

    if (!m_hint.empty()) {
        shape.hintWidth = std::max(readable, shape.controlWidth);
        shape.hintHeight = context.metric(ThemeMetric::ControlVerticalPadding) + Widgets::paragraphSize(context, context.font(ThemeFont::Caption), context.text(m_hint), shape.hintWidth).y;
    }

    return shape;
}

} // namespace workpane::ui
