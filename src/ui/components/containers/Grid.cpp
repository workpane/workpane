#include "ui/components/containers/Grid.h"

#include <algorithm>
#include <numeric>

namespace workpane::ui {

Grid::Grid(NodeId id) : Component(id) {}

std::string_view Grid::kind() const {
    return "grid";
}

std::size_t Grid::childLimit() const {
    return unlimitedChildren;
}

Alignment Grid::defaultRowAlignment() const {
    return Alignment::Stretch;
}

void Grid::readProperties(json::ObjectReader& reader) {
    reader.readInteger("columns", m_columns, 1, 24, json::Presence::Optional);
    readSpacing(reader, "columnSpacing", m_columnSpacing);
    readSpacing(reader, "rowSpacing", m_rowSpacing);
    readInsets(reader, "padding", m_padding);
}

Component::Restore Grid::keep() {
    return kept(m_columns, m_columnSpacing, m_rowSpacing, m_padding);
}

ImVec2 Grid::measureContent(RenderContext& context, float availableWidth) {
    const float scale = context.scale();
    const auto heights = rowHeights(context, cellWidth(context, availableWidth));
    const float spacing = heights.size() > 1 ? m_rowSpacing * scale * static_cast<float>(heights.size() - 1) : 0.0F;
    return {availableWidth, std::accumulate(heights.begin(), heights.end(), 0.0F) + spacing + (m_padding.vertical() * scale)};
}

void Grid::render(RenderContext& context, const ImRect& bounds) {
    const float scale = context.scale();
    const float width = cellWidth(context, bounds.GetWidth());
    const auto heights = rowHeights(context, width);
    const auto children = visibleChildren();
    const auto columns = static_cast<std::size_t>(m_columns);
    float y = bounds.Min.y + m_padding.top * scale;

    for (std::size_t row = 0; row < heights.size(); ++row) {
        for (std::size_t column = 0; column < columns && row * columns + column < children.size(); ++column) {
            Component* child = children[row * columns + column];
            const ImVec2 size = child->measure(context, width);
            const float x = bounds.Min.x + m_padding.left * scale + static_cast<float>(column) * (width + m_columnSpacing * scale);
            const Alignment vertical = child->rowAlignment();
            const float height = vertical == Alignment::Stretch ? heights[row] : size.y;
            child->draw(context, snapped(x, y + alignedOffset(vertical, heights[row], height), child->columnAlignment() == Alignment::Stretch ? width : std::min(size.x, width), height));
        }

        y += heights[row] + m_rowSpacing * scale;
    }
}

std::vector<float> Grid::rowHeights(RenderContext& context, float cellWidth) {
    const auto children = visibleChildren();
    const auto columns = static_cast<std::size_t>(m_columns);
    std::vector<float> heights((children.size() + columns - 1) / columns, 0.0F);

    for (std::size_t index = 0; index < children.size(); ++index) {
        heights[index / columns] = std::max(heights[index / columns], children[index]->measure(context, cellWidth).y);
    }

    return heights;
}

float Grid::cellWidth(RenderContext& context, float available) const {
    const float scale = context.scale();
    const float spacing = m_columnSpacing * scale * static_cast<float>(m_columns - 1);
    return std::max(0.0F, (available - m_padding.horizontal() * scale - spacing) / static_cast<float>(m_columns));
}

} // namespace workpane::ui
