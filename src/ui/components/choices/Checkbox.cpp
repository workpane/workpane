#include "ui/components/choices/Checkbox.h"

#include "ui/Widgets.h"

namespace workpane::ui {

Checkbox::Checkbox(NodeId id) : Component(id) {}

std::string_view Checkbox::kind() const {
    return "checkbox";
}

Alignment Checkbox::defaultColumnAlignment() const {
    return Alignment::Start;
}

void Checkbox::readProperties(json::ObjectReader& reader) {
    readText(reader, "text", m_text);
    reader.read("checked", m_checked, json::Presence::Optional);
}

Component::Restore Checkbox::keep() {
    return kept(m_text, m_checked);
}

ImVec2 Checkbox::measureContent(RenderContext& context, float) {
    return Widgets::checkboxSize(context, context.text(m_text));
}

void Checkbox::render(RenderContext& context, const ImRect& bounds) {
    if (Widgets::checkbox(context, "##checkbox", bounds, m_checked, context.text(m_text))) {
        context.emit(id(), "change", {{"checked", m_checked}}, {{"checked", "checked"}});
    }
}

} // namespace workpane::ui
