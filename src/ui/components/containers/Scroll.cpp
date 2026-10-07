#include "ui/components/containers/Scroll.h"

#include "ui/WheelScale.h"

#include <algorithm>
#include <string>

namespace workpane::ui {

Scroll::Scroll(NodeId id) : Component(id) {}

std::string_view Scroll::kind() const {
    return "scroll";
}

std::size_t Scroll::childLimit() const {
    return 1;
}

Alignment Scroll::defaultRowAlignment() const {
    return Alignment::Stretch;
}

Result<void> Scroll::validateChildren() const {
    if (children().size() != 1) {
        return Result<void>::failure({"ui_scroll_content", "A scroll area holds exactly one child", std::to_string(children().size())});
    }

    return Result<void>::success();
}

void Scroll::readProperties(json::ObjectReader& reader) {
    readInsets(reader, "padding", m_padding);
    reader.read("follow", m_follow, json::Presence::Optional);
    reader.readChoice("orientation", m_axis, {{"vertical", Axis::Vertical}, {"horizontal", Axis::Horizontal}}, json::Presence::Optional);
}

Component::Restore Scroll::keep() {
    return kept(m_padding, m_follow, m_axis);
}

ImVec2 Scroll::measureContent(RenderContext& context, float availableWidth) {
    const float scale = context.scale();

    if (children().empty()) {
        return {0.0F, 0.0F};
    }

    if (m_axis == Axis::Horizontal) {
        return measureAcross(context, availableWidth);
    }

    const ImVec2 size = children().front()->measure(context, std::max(0.0F, availableWidth - m_padding.horizontal() * scale));
    return {size.x + m_padding.horizontal() * scale, size.y + m_padding.vertical() * scale};
}

void Scroll::render(RenderContext& context, const ImRect& bounds) {
    const float scale = context.scale();

    if (m_axis == Axis::Horizontal) {
        renderAcross(context, bounds);
        return;
    }

    ImGui::SetCursorScreenPos(bounds.Min);

    if (!ImGui::BeginChild("##scroll", bounds.GetSize(), ImGuiChildFlags_None, ImGuiWindowFlags_None)) {
        ImGui::EndChild();
        return;
    }

    // A reader who stays near the bottom keeps seeing it while the content grows, and one who scrolled away is left where they are.
    const bool following = m_follow && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - followDistance * scale;

    if (!children().empty()) {
        Component& child = *children().front();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        float inner = std::max(0.0F, bounds.GetWidth() - m_padding.horizontal() * scale);
        ImVec2 size = child.measure(context, inner);

        // The scroll bar takes its room only when the content overflows, and the content is measured again inside what is left.
        if (size.y + m_padding.vertical() * scale > bounds.GetHeight()) {
            inner = std::max(0.0F, inner - ImGui::GetStyle().ScrollbarSize);
            size = child.measure(context, inner);
        }

        const float height = std::max(size.y, bounds.GetHeight() - m_padding.vertical() * scale);
        child.draw(context, snapped(origin.x + m_padding.left * scale, origin.y + m_padding.top * scale, inner, height));
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy(ImVec2(inner + m_padding.horizontal() * scale, height + m_padding.vertical() * scale));
    }

    if (following) {
        ImGui::SetScrollHereY(1.0F);
    }

    // Arriving at the top of content taller than the area is reported once per arrival, which is how a plugin loads what came before.
    const bool atTop = ImGui::GetScrollY() <= 0.0F && ImGui::GetScrollMaxY() > 0.0F;

    if (atTop && !m_atTop) {
        context.emit(id(), "top");
    }

    m_atTop = atTop;
    ImGui::EndChild();
}

// Content scrolled sideways is measured at the width of the area, so children of a fixed width overflow it while filling children take it, and the height grows by the scroll bar when the content overflows.
ImVec2 Scroll::measureAcross(RenderContext& context, float availableWidth) {
    const float scale = context.scale();
    const ImVec2 size = children().front()->measure(context, std::max(0.0F, availableWidth - m_padding.horizontal() * scale));
    const float width = size.x + m_padding.horizontal() * scale;
    const float bar = width > availableWidth ? ImGui::GetStyle().ScrollbarSize : 0.0F;
    return {std::min(width, availableWidth), size.y + m_padding.vertical() * scale + bar};
}

// The wheel moves the area sideways as far as it moves any view, its turn down taken as a turn across since the area has no other direction to scroll in, and an area that can move takes the wheel from the view around it.
void Scroll::renderAcross(RenderContext& context, const ImRect& bounds) {
    const float scale = context.scale();
    ImGui::SetCursorScreenPos(bounds.Min);

    if (!ImGui::BeginChild("##scroll", bounds.GetSize(), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::EndChild();
        return;
    }

    const ImVec2 moved = WheelScale::distance();
    const bool scrollable = ImGui::IsWindowHovered() && ImGui::GetScrollMaxX() > 0.0F;

    if (scrollable) {
        WheelScale::claim(ImGui::GetID("##wheel"));
    }

    if (scrollable && (moved.x != 0.0F || moved.y != 0.0F)) {
        ImGui::SetScrollX(ImGui::GetScrollX() - moved.x - moved.y);
    }

    Component& child = *children().front();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float natural = child.measure(context, std::max(0.0F, bounds.GetWidth() - m_padding.horizontal() * scale)).x;
    const float inner = std::max(natural, bounds.GetWidth() - m_padding.horizontal() * scale);
    const float bar = natural + m_padding.horizontal() * scale > bounds.GetWidth() ? ImGui::GetStyle().ScrollbarSize : 0.0F;
    const float height = std::max(0.0F, bounds.GetHeight() - m_padding.vertical() * scale - bar);
    child.draw(context, snapped(origin.x + m_padding.left * scale, origin.y + m_padding.top * scale, inner, height));
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(inner + m_padding.horizontal() * scale, height + m_padding.vertical() * scale));
    ImGui::EndChild();
}

} // namespace workpane::ui
