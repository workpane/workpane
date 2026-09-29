#include "ui/components/inputs/TextArea.h"

#include "ui/TextAreaOptions.h"
#include "ui/TextFieldResult.h"
#include "ui/Widgets.h"
#include "ui/theme/Theme.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace workpane::ui {

TextArea::TextArea(NodeId id) : Component(id) {}

std::string_view TextArea::kind() const {
    return "textArea";
}

FocusMode TextArea::focusMode() const {
    return FocusMode::Typing;
}

void TextArea::readProperties(json::ObjectReader& reader) {
    std::string value = m_value;
    reader.read("value", value, json::Presence::Optional);

    if (value != m_value) {
        m_value = std::move(value);
        m_reload = true;
    }

    readText(reader, "placeholder", m_placeholder);
    reader.readInteger("rows", m_rows, 1, 200, json::Presence::Optional);
    reader.read("readOnly", m_readOnly, json::Presence::Optional).read("wrap", m_wrap, json::Presence::Optional).read("submitOnEnter", m_submitOnEnter, json::Presence::Optional);
}

Component::Restore TextArea::keep() {
    return kept(m_value, m_reload, m_placeholder, m_rows, m_readOnly, m_wrap, m_submitOnEnter);
}

ImVec2 TextArea::measureContent(RenderContext& context, float availableWidth) {
    const float line = Widgets::fontSize(context, context.font(ThemeFont::Interface));
    const float padding = 8.0F * context.scale();
    return {std::min(availableWidth, context.metric(ThemeMetric::SettingsControlMaximumWidth)), std::ceil(line * static_cast<float>(m_rows) + padding * 2.0F)};
}

void TextArea::render(RenderContext& context, const ImRect& bounds) {
    const TextFieldResult result = Widgets::textArea(context, "##area", bounds, m_value, TextAreaOptions{context.text(m_placeholder), m_readOnly, m_wrap, m_submitOnEnter, m_reload});
    m_reload = false;

    if (result.changed) {
        context.emit(id(), "change", {{"value", m_value}}, {{"value", "value"}});
    }

    if (result.submitted) {
        context.emit(id(), "submit", {{"value", m_value}});
    }
}

} // namespace workpane::ui
