#include "ui/components/containers/Splitter.h"

#include "ui/ButtonState.h"
#include "ui/Color.h"
#include "ui/Painter.h"
#include "ui/WidgetHelper.h"
#include "ui/theme/Theme.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace workpane::ui {

Splitter::Splitter(NodeId id) : Component(id) {}

std::string_view Splitter::kind() const {
    return "splitter";
}

std::size_t Splitter::childLimit() const {
    return 2;
}

void Splitter::readProperties(json::ObjectReader& reader) {
    reader.readChoice("orientation", m_axis, {{"horizontal", Axis::Horizontal}, {"vertical", Axis::Vertical}}, json::Presence::Optional);
    reader.readNumber("ratio", m_ratio, 0.0, 1.0, json::Presence::Optional);
    readSpacing(reader, "firstMinimum", m_firstMinimum);
    readSpacing(reader, "secondMinimum", m_secondMinimum);
}

Component::Restore Splitter::keep() {
    return kept(m_axis, m_ratio, m_firstMinimum, m_secondMinimum);
}

Result<void> Splitter::validateChildren() const {
    if (children().size() != 2) {
        return Result<void>::failure({"ui_splitter_children", "A splitter divides exactly two children", std::to_string(children().size())});
    }

    return Result<void>::success();
}

Alignment Splitter::defaultRowAlignment() const {
    return Alignment::Stretch;
}

// A splitter fills the width, and stands as tall as its minimums and its handle when its panes are stacked, as its taller pane when they sit side by side and as the pane left alone when the other one is hidden.
ImVec2 Splitter::measureContent(RenderContext& context, float availableWidth) {
    const float handle = context.metric(ThemeMetric::SplitterWidth);
    const auto shown = visibleChildren();

    if (shown.size() < children().size()) {
        return {availableWidth, shown.empty() ? 0.0F : shown.front()->measure(context, availableWidth).y};
    }

    if (m_axis == Axis::Vertical) {
        return {availableWidth, (m_firstMinimum + m_secondMinimum) * context.scale() + handle};
    }

    const float first = firstExtent(context, availableWidth);
    const float second = std::max(0.0F, availableWidth - first - handle);
    return {availableWidth, std::max(children()[0]->measure(context, first).y, children()[1]->measure(context, second).y)};
}

void Splitter::render(RenderContext& context, const ImRect& bounds) {
    const auto shown = visibleChildren();

    if (shown.size() < children().size()) {
        for (Component* child : shown) {
            child->draw(context, bounds);
        }

        return;
    }

    const float scale = context.scale();
    const bool horizontal = m_axis == Axis::Horizontal;
    const float total = horizontal ? bounds.GetWidth() : bounds.GetHeight();
    const float handle = context.metric(ThemeMetric::SplitterWidth);
    const float available = std::max(0.0F, total - handle);
    const float first = firstExtent(context, total);
    const float start = horizontal ? bounds.Min.x : bounds.Min.y;
    const float hit = splitterHitWidth * scale;
    const ImRect handleArea = horizontal ? ImRect(start + first - hit / 2.0F, bounds.Min.y, start + first + handle + hit / 2.0F, bounds.Max.y) : ImRect(bounds.Min.x, start + first - hit / 2.0F, bounds.Max.x, start + first + handle + hit / 2.0F);

    // The handle is submitted before the panes, so it keeps the pointer where its hit area overlaps them.
    const ButtonState state = WidgetHelper::interact(ImGui::GetID("##handle"), handleArea, ImGuiButtonFlags_PressedOnClick);

    if (state.hovered || state.held) {
        ImGui::SetMouseCursor(horizontal ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
    }

    if (state.held && available > 0.0F) {
        const float pointer = horizontal ? ImGui::GetIO().MousePos.x : ImGui::GetIO().MousePos.y;
        m_ratio = std::clamp(static_cast<double>((pointer - start - handle / 2.0F) / available), 0.0, 1.0);
        m_dragging = true;
        context.requestFrame();
    }

    if (m_dragging && !state.held) {
        m_dragging = false;
        context.emit(id(), "resize", {{"ratio", m_ratio}}, {{"ratio", "ratio"}});
    }

    const ImRect firstBounds = horizontal ? ImRect(bounds.Min.x, bounds.Min.y, bounds.Min.x + first, bounds.Max.y) : ImRect(bounds.Min.x, bounds.Min.y, bounds.Max.x, bounds.Min.y + first);
    const ImRect secondBounds = horizontal ? ImRect(bounds.Min.x + first + handle, bounds.Min.y, bounds.Max.x, bounds.Max.y) : ImRect(bounds.Min.x, bounds.Min.y + first + handle, bounds.Max.x, bounds.Max.y);
    children()[0]->draw(context, firstBounds);
    children()[1]->draw(context, secondBounds);

    const Color line = context.color(state.held ? ThemeColor::Accent : ThemeColor::Border);
    ImDrawList& list = *ImGui::GetWindowDrawList();

    if (horizontal) {
        Painter::verticalDivider(list, ImVec2(bounds.Min.x + first, bounds.Min.y), bounds.GetHeight(), context.metric(ThemeMetric::LineWidth), line);
        return;
    }

    Painter::horizontalDivider(list, ImVec2(bounds.Min.x, bounds.Min.y + first), bounds.GetWidth(), context.metric(ThemeMetric::LineWidth), line);
}

// The first pane takes its ratio of what the handle leaves, kept within both minimums as far as the room allows.
float Splitter::firstExtent(const RenderContext& context, float total) const {
    const float available = std::max(0.0F, total - context.metric(ThemeMetric::SplitterWidth));
    const float least = std::min(m_firstMinimum * context.scale(), available);
    return std::clamp(std::round(available * static_cast<float>(m_ratio)), least, std::max(least, available - m_secondMinimum * context.scale()));
}

} // namespace workpane::ui
