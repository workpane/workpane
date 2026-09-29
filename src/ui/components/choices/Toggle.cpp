#include "ui/components/choices/Toggle.h"

#include "ui/Widgets.h"

namespace workpane::ui {

Toggle::Toggle(NodeId id) : Component(id) {}

std::string_view Toggle::kind() const {
    return "toggle";
}

Alignment Toggle::defaultColumnAlignment() const {
    return Alignment::Start;
}

void Toggle::readProperties(json::ObjectReader& reader) {
    reader.read("checked", m_checked, json::Presence::Optional);
}

Component::Restore Toggle::keep() {
    return kept(m_checked);
}

ImVec2 Toggle::measureContent(RenderContext& context, float) {
    return Widgets::toggleSize(context);
}

void Toggle::render(RenderContext& context, const ImRect& bounds) {
    if (Widgets::toggle(context, "##toggle", bounds, m_checked)) {
        context.emit(id(), "change", {{"checked", m_checked}}, {{"checked", "checked"}});
    }
}

} // namespace workpane::ui
