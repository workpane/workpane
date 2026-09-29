#include "ui/components/buttons/Button.h"

#include "ui/components/buttons/ButtonVariants.h"

namespace workpane::ui {

Button::Button(NodeId id) : Component(id) {}

std::string_view Button::kind() const {
    return "button";
}

Alignment Button::defaultColumnAlignment() const {
    return Alignment::Start;
}

void Button::readProperties(json::ObjectReader& reader) {
    readText(reader, "text", m_text);
    readIcon(reader, "icon", m_icon);
    ButtonVariants::read(reader, m_variant);
    reader.read("checked", m_checked, json::Presence::Optional);
}

Component::Restore Button::keep() {
    return kept(m_text, m_icon, m_variant, m_checked);
}

ImVec2 Button::measureContent(RenderContext& context, float) {
    return Widgets::buttonSize(context, context.text(m_text), m_icon, m_variant);
}

void Button::render(RenderContext& context, const ImRect& bounds) {
    if (Widgets::button(context, "##button", bounds, context.text(m_text), m_icon, m_variant, m_checked)) {
        context.emit(id(), "click");
    }
}

} // namespace workpane::ui
