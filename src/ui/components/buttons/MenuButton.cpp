#include "ui/components/buttons/MenuButton.h"

#include "ui/Menus.h"
#include "ui/components/buttons/ButtonVariants.h"

#include <utility>

namespace workpane::ui {

MenuButton::MenuButton(NodeId id) : Component(id) {}

std::string_view MenuButton::kind() const {
    return "menuButton";
}

Alignment MenuButton::defaultColumnAlignment() const {
    return Alignment::Start;
}

void MenuButton::readProperties(json::ObjectReader& reader) {
    readText(reader, "text", m_text);
    readIcon(reader, "icon", m_icon);
    ButtonVariants::read(reader, m_variant);

    if (!reader.contains("items")) {
        return;
    }

    const json::Json* items = &json::ObjectReader::absent();
    reader.readArray("items", items);
    auto parsed = Menus::parse(*items, kind());

    if (!parsed.hasValue()) {
        fail(parsed.error());
        return;
    }

    m_items = std::move(parsed.value());
}

Component::Restore MenuButton::keep() {
    return kept(m_text, m_icon, m_variant, m_items);
}

ImVec2 MenuButton::measureContent(RenderContext& context, float) {
    return Widgets::buttonSize(context, context.text(m_text), m_icon, m_variant);
}

void MenuButton::render(RenderContext& context, const ImRect& bounds) {
    if (Widgets::button(context, "##menuButton", bounds, context.text(m_text), m_icon, m_variant, ImGui::IsPopupOpen(itemsPopup))) {
        ImGui::OpenPopup(itemsPopup);
    }

    Widgets::placePopup(context, itemsPopup, bounds);

    if (const auto picked = Menus::popup(context, itemsPopup, m_items); picked.has_value()) {
        context.emit(id(), "select", {{"item", *picked}});
    }
}

} // namespace workpane::ui
