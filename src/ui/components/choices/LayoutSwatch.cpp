#include "ui/components/choices/LayoutSwatch.h"

#include "ui/ButtonState.h"
#include "ui/Painter.h"
#include "ui/WidgetHelper.h"
#include "ui/theme/Theme.h"

#include <string>
#include <utility>

namespace workpane::ui {

LayoutSwatch::LayoutSwatch(NodeId id) : Component(id) {}

std::string_view LayoutSwatch::kind() const {
    return "layoutSwatch";
}

Alignment LayoutSwatch::defaultColumnAlignment() const {
    return Alignment::Start;
}

// A cell names its column and row from zero and the optional number of columns and rows it spans.
void LayoutSwatch::readProperties(json::ObjectReader& reader) {
    reader.readInteger("columns", m_columns, 1, largestGrid, json::Presence::Optional).readInteger("rows", m_rows, 1, largestGrid, json::Presence::Optional).read("checked", m_checked, json::Presence::Optional);

    if (!reader.contains("cells")) {
        return;
    }

    const json::Json* cells = &json::ObjectReader::absent();
    reader.readArray("cells", cells);
    std::vector<Cell> parsed;

    for (const auto& item : *cells) {
        Cell cell;
        json::ObjectReader cellReader(item, std::string(kind()) + ".cells");
        cellReader.readInteger("column", cell.column, 0, largestGrid - 1).readInteger("row", cell.row, 0, largestGrid - 1).readInteger("columnSpan", cell.columnSpan, 1, largestGrid, json::Presence::Optional).readInteger("rowSpan", cell.rowSpan, 1, largestGrid, json::Presence::Optional);

        if (auto finished = cellReader.finish(); !finished.hasValue()) {
            fail(finished.error());
            return;
        }

        parsed.push_back(cell);
    }

    m_cells = std::move(parsed);
}

Component::Restore LayoutSwatch::keep() {
    return kept(m_columns, m_rows, m_checked, m_cells);
}

Result<void> LayoutSwatch::validate() const {
    if (m_cells.empty()) {
        return Result<void>::failure({"ui_swatch_cell_invalid", "A layout swatch shows at least one cell", std::string(kind())});
    }

    for (const auto& cell : m_cells) {
        if (cell.column + cell.columnSpan > m_columns || cell.row + cell.rowSpan > m_rows) {
            return Result<void>::failure({"ui_swatch_cell_invalid", "A cell of a layout swatch leaves its grid", std::to_string(cell.column) + "," + std::to_string(cell.row)});
        }
    }

    return Result<void>::success();
}

ImVec2 LayoutSwatch::measureContent(RenderContext& context, float) {
    return {swatchWidth * context.scale(), swatchHeight * context.scale()};
}

void LayoutSwatch::render(RenderContext& context, const ImRect& bounds) {
    const ButtonState state = WidgetHelper::interact(ImGui::GetID("##swatch"), bounds);
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const float scale = context.scale();
    const float radius = context.metric(ThemeMetric::ControlRadius);

    if (state.hovered || m_checked) {
        list.AddRectFilled(bounds.Min, bounds.Max, WidgetHelper::ink(context.color(ThemeColor::Hover)), radius);
    }

    if (m_checked) {
        Painter::rectBorder(list, bounds.Min, bounds.Max, radius, context.metric(ThemeMetric::LineWidth), context.color(ThemeColor::Accent));
    }

    const ImVec2 picture(pictureWidth * scale, pictureHeight * scale);
    const ImVec2 origin = ImFloor(bounds.GetCenter() - picture * 0.5F);
    const float gap = cellGap * scale;
    const float cellWidth = (picture.x - gap * static_cast<float>(m_columns - 1)) / static_cast<float>(m_columns);
    const float cellHeight = (picture.y - gap * static_cast<float>(m_rows - 1)) / static_cast<float>(m_rows);
    const Color fill = context.color(m_checked ? ThemeColor::Accent : state.hovered ? ThemeColor::Hover : ThemeColor::Pressed);
    const Color outline = context.color(m_checked || state.hovered ? ThemeColor::Text : ThemeColor::TextMuted);

    for (const auto& cell : m_cells) {
        const ImVec2 minimum(origin.x + static_cast<float>(cell.column) * (cellWidth + gap), origin.y + static_cast<float>(cell.row) * (cellHeight + gap));
        const ImVec2 maximum(minimum.x + cellWidth * static_cast<float>(cell.columnSpan) + gap * static_cast<float>(cell.columnSpan - 1), minimum.y + cellHeight * static_cast<float>(cell.rowSpan) + gap * static_cast<float>(cell.rowSpan - 1));
        list.AddRectFilled(minimum, maximum, WidgetHelper::ink(fill));
        Painter::rectBorder(list, minimum, maximum, 0.0F, context.metric(ThemeMetric::LineWidth), outline);
    }

    if (state.pressed) {
        context.emit(id(), "click");
    }
}

} // namespace workpane::ui
