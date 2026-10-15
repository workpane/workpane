#include "ui/components/buttons/Popover.h"

#include "ui/components/buttons/ButtonVariants.h"

#include <string>

namespace workpane::ui {

Popover::Popover(NodeId id) : Component(id) {}

std::string_view Popover::kind() const {
    return "popover";
}

std::size_t Popover::childLimit() const {
    return 1;
}

Result<void> Popover::validateChildren() const {
    if (children().size() != 1) {
        return Result<void>::failure({"ui_popover_children", "A popover holds exactly one child", std::to_string(children().size())});
    }

    return Result<void>::success();
}

// The panel opens and closes on the next frame, so a choice made inside it can close it from its own handler.
Result<void> Popover::command(RenderContext& context, std::string_view name, const json::Json& arguments) {
    if ((name != "open" && name != "close") || !arguments.empty()) {
        return Component::command(context, name, arguments);
    }

    m_openRequested = name == "open";
    m_closeRequested = name == "close";
    context.requestFrame();

    return Result<void>::success();
}

Alignment Popover::defaultColumnAlignment() const {
    return Alignment::Start;
}

void Popover::readProperties(json::ObjectReader& reader) {
    readText(reader, "text", m_text);
    readIcon(reader, "icon", m_icon);
    ButtonVariants::read(reader, m_variant);
}

Component::Restore Popover::keep() {
    return kept(m_text, m_icon, m_variant);
}

ImVec2 Popover::measureContent(RenderContext& context, float) {
    return WidgetHelper::buttonSize(context, context.text(m_text), m_icon, m_variant);
}

void Popover::render(RenderContext& context, const ImRect& bounds) {
    const float scale = context.scale();

    const bool pressed = WidgetHelper::button(context, "##popoverButton", bounds, context.text(m_text), m_icon, m_variant, ImGui::IsPopupOpen("##popover"));

    if (pressed || m_openRequested) {
        m_openRequested = false;
        ImGui::OpenPopup("##popover");
    }

    // The panel fits the size its child measures and opens below the button, or wherever around it the window still has room.
    Component& content = *children().front();
    const ImVec2 size = content.measure(context, panelMaximumWidth * scale);
    WidgetHelper::placePopup(context, "##popover", bounds);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(panelInset * scale, panelInset * scale));
    const bool open = ImGui::BeginPopup("##popover", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar();

    if (!open) {
        m_closeRequested = false;
        return;
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::ItemSize(size);
    content.draw(context, ImRect(origin, origin + size));

    if (m_closeRequested) {
        m_closeRequested = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

} // namespace workpane::ui
