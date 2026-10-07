#include "ui/components/inputs/FilterField.h"

#include "ui/Painter.h"
#include "ui/TextFieldOptions.h"
#include "ui/TextFieldResult.h"
#include "ui/WidgetHelper.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

#include <string>
#include <utility>

namespace workpane::ui {

FilterField::FilterField(NodeId id) : Component(id) {}

std::string_view FilterField::kind() const {
    return "filterField";
}

// A value the plugin replaces while the reader types is reloaded, because the field otherwise writes its own copy back over it.
void FilterField::readProperties(json::ObjectReader& reader) {
    std::string value = m_value;
    reader.read("value", value, json::Presence::Optional);

    if (value != m_value) {
        m_value = std::move(value);
        m_reload = true;
    }

    readText(reader, "caption", m_caption);
    readText(reader, "placeholder", m_placeholder);
}

Component::Restore FilterField::keep() {
    return kept(m_value, m_reload, m_caption, m_placeholder);
}

ImVec2 FilterField::measureContent(RenderContext& context, float availableWidth) {
    const float caption = WidgetHelper::lineHeight(context, context.font(ThemeFont::Caption));
    return {availableWidth, caption + filterCaptionSpacing * context.scale() + WidgetHelper::controlHeight(context)};
}

void FilterField::render(RenderContext& context, const ImRect& bounds) {
    const FontRole captionFont = context.font(ThemeFont::Caption);
    const float caption = WidgetHelper::lineHeight(context, captionFont);
    ImDrawList& list = *ImGui::GetWindowDrawList();
    WidgetHelper::alignedText(context, list, captionFont, ImRect(bounds.Min.x, bounds.Min.y, bounds.Max.x, bounds.Min.y + caption), context.color(ThemeColor::TextMuted), context.text(m_caption), TextAlign::Start);

    const float top = bounds.Min.y + caption + filterCaptionSpacing * context.scale();
    const ImRect field(ImVec2(bounds.Min.x, top), ImVec2(bounds.Max.x, top + WidgetHelper::controlHeight(context)));
    const std::string placeholder = context.text(m_placeholder);
    const TextFieldOptions options{.placeholder = placeholder, .clearButton = true, .reload = std::exchange(m_reload, false)};
    const TextFieldResult result = WidgetHelper::textField(context, "##filter", field, m_value, options);

    if (!result.focused) {
        Painter::rectBorder(list, field.Min, field.Max, context.metric(ThemeMetric::ControlRadius), context.metric(ThemeMetric::LineWidth), context.color(ThemeColor::BorderStrong));
    }

    if (result.changed) {
        context.emit(id(), "change", {{"value", m_value}}, {{"value", "value"}});
    }
}

} // namespace workpane::ui
