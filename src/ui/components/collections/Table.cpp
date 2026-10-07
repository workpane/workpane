#include "ui/components/collections/Table.h"

#include "localization/Localization.h"
#include "ui/ButtonState.h"
#include "ui/Color.h"
#include "ui/IconCatalog.h"
#include "ui/Painter.h"
#include "ui/WidgetHelper.h"
#include "ui/components/collections/TableAction.h"
#include "ui/components/indicators/Tones.h"
#include "ui/model/TextValue.h"
#include "ui/theme/Theme.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <numeric>
#include <set>
#include <utility>

namespace workpane::ui {

Table::Table(NodeId id) : Component(id) {}

std::string_view Table::kind() const {
    return "table";
}

Alignment Table::defaultRowAlignment() const {
    return Alignment::Stretch;
}

void Table::readProperties(json::ObjectReader& reader) {
    reader.read("selected", m_selected, json::Presence::Optional).read("alternate", m_alternate, json::Presence::Optional).read("header", m_header, json::Presence::Optional);
    reader.readChoice("selection", m_selection, {{"accent", Selection::Accent}, {"subtle", Selection::Subtle}}, json::Presence::Optional);

    // Only new columns or rows change the sizes the table measured, so a new selection keeps every cached size.
    if (reader.contains("columns") || reader.contains("rows")) {
        ++m_version;
    }

    if (reader.contains("columns")) {
        const json::Json* columns = &json::ObjectReader::absent();
        reader.readArray("columns", columns);
        auto parsed = parseColumns(*columns);

        if (!parsed.hasValue()) {
            fail(parsed.error());
            return;
        }

        m_columns = std::move(parsed.value());
    }

    if (reader.contains("rows")) {
        const json::Json* rows = &json::ObjectReader::absent();
        reader.readArray("rows", rows);
        auto parsed = parseRows(*rows);

        if (!parsed.hasValue()) {
            fail(parsed.error());
            return;
        }

        m_rows = std::move(parsed.value());
        m_mostActions = 0;

        for (const auto& row : m_rows) {
            m_mostActions = std::max(m_mostActions, row.actions.size());
        }
    }
}

Component::Restore Table::keep() {
    return kept(m_selected, m_alternate, m_header, m_selection, m_version, m_columns, m_rows, m_mostActions);
}

Result<void> Table::validate() const {
    std::set<std::string, std::less<>> columns;
    // clang-format off
    const auto stretches = std::ranges::count_if(m_columns, [](const TableColumn& column) { return column.width == ColumnWidth::Stretch; });
    // clang-format on

    for (const auto& column : m_columns) {
        if (!columns.insert(column.id).second) {
            return Result<void>::failure({"ui_column_duplicate", "Two columns share one identifier", column.id});
        }
    }

    if (!m_columns.empty() && stretches != 1) {
        return Result<void>::failure({"ui_column_stretch", "A table designates exactly one stretch column", std::to_string(stretches)});
    }

    std::set<std::string, std::less<>> rows;

    for (const auto& row : m_rows) {
        if (!rows.insert(row.id).second) {
            return Result<void>::failure({"ui_row_duplicate", "Two rows share one identifier", row.id});
        }

        if (row.cells.size() != m_columns.size()) {
            return Result<void>::failure({"ui_row_cells", "A row carries a different number of cells than the table has columns", row.id});
        }
    }

    if (!m_selected.empty() && !rows.contains(m_selected)) {
        return Result<void>::failure({"ui_row_unknown", "The selection names no row", m_selected});
    }

    return Result<void>::success();
}

ImVec2 Table::measureContent(RenderContext& context, float availableWidth) {
    const auto widths = columnWidths(context, availableWidth);
    const float header = m_header ? context.metric(ThemeMetric::WorkspaceBarHeight) : 0.0F;
    return {availableWidth, header + rowOffsets(context, widths).back()};
}

void Table::render(RenderContext& context, const ImRect& bounds) {
    const float header = m_header ? context.metric(ThemeMetric::WorkspaceBarHeight) : 0.0F;
    const ImRect body(bounds.Min.x, bounds.Min.y + header, bounds.Max.x, bounds.Max.y);
    auto widths = columnWidths(context, bounds.GetWidth());

    // The scroll bar takes its room only when the rows overflow, and the columns are sized again inside what is left.
    if (rowOffsets(context, widths).back() > body.GetHeight()) {
        widths = columnWidths(context, bounds.GetWidth() - ImGui::GetStyle().ScrollbarSize);
    }

    const std::vector<float>& offsets = rowOffsets(context, widths);

    if (m_header) {
        drawHeader(context, ImRect(bounds.Min, ImVec2(bounds.Max.x, bounds.Min.y + header)), widths);
    }

    ImGui::SetCursorScreenPos(body.Min);

    if (!ImGui::BeginChild("##rows", body.GetSize(), ImGuiChildFlags_None, ImGuiWindowFlags_None)) {
        ImGui::EndChild();
        return;
    }

    if (m_scrollTo.has_value()) {
        // clang-format off
        const auto found = std::ranges::find_if(m_rows, [this](const TableRow& row) { return row.id == *m_scrollTo; });
        // clang-format on

        if (found != m_rows.end()) {
            const auto index = static_cast<std::size_t>(std::distance(m_rows.begin(), found));
            ImGui::SetScrollY(std::max(0.0F, offsets[index] - body.GetHeight() / 2.0F));
        }

        m_scrollTo.reset();
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = std::accumulate(widths.begin(), widths.end(), 0.0F) + actionsWidth(context);
    const float top = ImGui::GetScrollY();
    const float bottom = top + body.GetHeight();
    const auto first = static_cast<std::size_t>(std::max<std::ptrdiff_t>(0, std::distance(offsets.begin(), std::ranges::upper_bound(offsets, top)) - 1));

    for (std::size_t index = first; index < m_rows.size() && offsets[index] < bottom; ++index) {
        drawRow(context, index, ImRect(origin.x, origin.y + offsets[index], origin.x + std::max(width, body.GetWidth()), origin.y + offsets[index + 1]), widths);
    }

    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        moveSelection(context, 1);
    }

    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        moveSelection(context, -1);
    }

    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(width, offsets.back()));
    ImGui::EndChild();
}

Result<std::vector<TableColumn>> Table::parseColumns(const json::Json& columns) const {
    std::vector<TableColumn> parsed;

    for (const auto& entry : columns) {
        TableColumn column;
        const json::Json* title = &json::ObjectReader::absent();
        const json::Json* width = &json::ObjectReader::absent();
        json::ObjectReader reader(entry, "table.columns");
        reader.readText("id", column.id).readAny("title", title).readAny("width", width, json::Presence::Optional).readChoice("align", column.align, {{"start", TextAlign::Start}, {"center", TextAlign::Center}, {"end", TextAlign::End}}, json::Presence::Optional);

        if (const auto finished = reader.finish(); !finished.hasValue()) {
            return Result<std::vector<TableColumn>>::failure(finished.error());
        }

        auto label = TextValue::parse(*title, "table.columns.title");

        if (!label.hasValue()) {
            return Result<std::vector<TableColumn>>::failure(label.error());
        }

        if (width->is_null() || *width == "content") {
            column.width = ColumnWidth::Content;
        } else if (*width == "stretch") {
            column.width = ColumnWidth::Stretch;
        } else if (width->is_number() && width->get<double>() > 0.0 && width->get<double>() <= 4000.0) {
            column.width = ColumnWidth::Fixed;
            column.fixed = width->get<float>();
        } else {
            return Result<std::vector<TableColumn>>::failure({"ui_column_width", "A column width is content, stretch or a number in range", column.id});
        }

        column.title = std::move(label.value());
        parsed.push_back(std::move(column));
    }

    return Result<std::vector<TableColumn>>::success(std::move(parsed));
}

Result<std::vector<TableRow>> Table::parseRows(const json::Json& rows) const {
    std::vector<TableRow> parsed;
    parsed.reserve(rows.size());

    for (const auto& entry : rows) {
        TableRow row;
        const json::Json* cells = &json::ObjectReader::absent();
        const json::Json* actions = &json::ObjectReader::emptyList();
        json::ObjectReader reader(entry, "table.rows");
        reader.readText("id", row.id).readArray("cells", cells).readArray("actions", actions, json::Presence::Optional);

        if (const auto finished = reader.finish(); !finished.hasValue()) {
            return Result<std::vector<TableRow>>::failure(finished.error());
        }

        // A cell is either plain text or an object adding an icon, a tone and a text style.
        for (const auto& value : *cells) {
            TableCell cell;

            if (value.is_string()) {
                cell.text = TextValue::literal(value.get<std::string>());
                row.cells.push_back(std::move(cell));
                continue;
            }

            const json::Json* text = &json::ObjectReader::absent();
            std::string icon;
            json::ObjectReader cellReader(value, "table.rows.cells");
            cellReader.readAny("text", text).read("icon", icon, json::Presence::Optional).read("muted", cell.muted, json::Presence::Optional).read("monospace", cell.monospace, json::Presence::Optional);

            if (cellReader.contains("tone")) {
                Tone tone = Tone::Neutral;
                Tones::read(cellReader, "tone", tone);
                cell.tone = tone;
            }

            if (const auto finished = cellReader.finish(); !finished.hasValue()) {
                return Result<std::vector<TableRow>>::failure(finished.error());
            }

            auto label = TextValue::parse(*text, "table.rows.cells.text");
            auto glyph = IconCatalog::parseOptional(icon);

            if (!label.hasValue()) {
                return Result<std::vector<TableRow>>::failure(label.error());
            }

            if (!glyph.hasValue()) {
                return Result<std::vector<TableRow>>::failure(glyph.error());
            }

            cell.text = std::move(label.value());
            cell.icon = glyph.value();
            row.cells.push_back(std::move(cell));
        }

        // Every action names an icon of the painted set, may carry a tooltip and answers to an identity no other action of its row takes.
        std::set<std::string, std::less<>> actionIds;

        for (const auto& value : *actions) {
            TableAction action;
            std::string icon;
            const json::Json* tooltip = &json::ObjectReader::absent();
            json::ObjectReader actionReader(value, "table.rows.actions");
            actionReader.readText("id", action.id).readText("icon", icon).readAny("tooltip", tooltip, json::Presence::Optional).read("destructive", action.destructive, json::Presence::Optional);

            if (actionReader.contains("tone")) {
                Tone tone = Tone::Neutral;
                Tones::read(actionReader, "tone", tone);
                action.tone = tone;
            }

            if (const auto finished = actionReader.finish(); !finished.hasValue()) {
                return Result<std::vector<TableRow>>::failure(finished.error());
            }

            if (!actionIds.insert(action.id).second) {
                return Result<std::vector<TableRow>>::failure({"ui_action_duplicate", "Two actions of a row share one identifier", action.id});
            }

            const auto glyph = IconCatalog::parse(icon);
            auto label = tooltip->is_null() ? Result<TextValue>::success(TextValue()) : TextValue::parse(*tooltip, "table.rows.actions.tooltip");

            if (!glyph.has_value()) {
                return Result<std::vector<TableRow>>::failure({"ui_icon_unknown", "An icon names nothing in the painted set", icon});
            }

            if (!label.hasValue()) {
                return Result<std::vector<TableRow>>::failure(label.error());
            }

            action.icon = *glyph;
            action.tooltip = std::move(label.value());
            row.actions.push_back(std::move(action));
        }

        parsed.push_back(std::move(row));
    }

    return Result<std::vector<TableRow>>::success(std::move(parsed));
}

// Content widths depend on the rows, the language and the display scale, so they are measured again only when one of those changes.
const std::vector<float>& Table::contentWidths(RenderContext& context) {
    if (m_widthsKey == m_version && m_widthsGeneration == context.localization().generation() && m_widthsScale == context.scale()) {
        return m_contentWidths;
    }

    const float scale = context.scale();
    const FontRole header = context.font(ThemeFont::Interface);
    const float icon = context.metric(ThemeMetric::SmallIconSize) + glyphSpacing * scale;
    m_contentWidths.assign(m_columns.size(), 0.0F);

    for (std::size_t column = 0; column < m_columns.size(); ++column) {
        float widest = WidgetHelper::textSize(context, header, context.text(m_columns[column].title)).x;

        for (const auto& row : m_rows) {
            const TableCell& cell = row.cells[column];
            widest = std::max(widest, WidgetHelper::textSize(context, cellFont(context, cell), context.text(cell.text)).x + (cell.icon.has_value() ? icon : 0.0F));
        }

        m_contentWidths[column] = std::ceil(widest + cellHorizontalPadding * 2.0F * scale);
    }

    m_widthsKey = m_version;
    m_widthsGeneration = context.localization().generation();
    m_widthsScale = scale;

    return m_contentWidths;
}

std::vector<float> Table::columnWidths(RenderContext& context, float available) {
    const auto& content = contentWidths(context);
    std::vector<float> widths(m_columns.size(), 0.0F);
    float used = actionsWidth(context);
    std::size_t stretch = m_columns.size();

    for (std::size_t column = 0; column < m_columns.size(); ++column) {
        switch (m_columns[column].width) {
        case ColumnWidth::Content:
            widths[column] = content[column];
            break;
        case ColumnWidth::Fixed:
            widths[column] = m_columns[column].fixed * context.scale();
            break;
        case ColumnWidth::Stretch:
            stretch = column;
            break;
        }

        used += widths[column];
    }

    if (stretch < m_columns.size()) {
        widths[stretch] = std::max(minimumStretchWidth * context.scale(), available - used);
    }

    return widths;
}

// Row tops are accumulated once per layout, so drawing a frame only searches them for the rows the viewport shows.
// Two layouts are kept, because a table whose rows overflow is measured at its whole width and drawn beside its scroll bar every frame.
const std::vector<float>& Table::rowOffsets(RenderContext& context, const std::vector<float>& widths) {
    // clang-format off
    const auto stretch = std::ranges::find_if(m_columns, [](const TableColumn& column) { return column.width == ColumnWidth::Stretch; });
    // clang-format on
    const float stretchWidth = stretch == m_columns.end() ? 0.0F : widths[static_cast<std::size_t>(std::distance(m_columns.begin(), stretch))];
    const std::uint64_t generation = context.localization().generation();

    for (const Offsets& kept : m_offsets) {
        if (kept.key == m_version && kept.generation == generation && kept.stretch == stretchWidth && kept.tops.size() == m_rows.size() + 1) {
            return kept.tops;
        }
    }

    m_offsetsSlot = (m_offsetsSlot + 1) % m_offsets.size();
    Offsets& built = m_offsets[m_offsetsSlot];
    const float scale = context.scale();
    const float minimum = context.metric(ThemeMetric::WorkspaceBarHeight);
    built.tops.assign(1, 0.0F);

    for (const auto& row : m_rows) {
        float height = minimum;

        for (std::size_t column = 0; column < m_columns.size(); ++column) {
            if (m_columns[column].width != ColumnWidth::Stretch) {
                continue;
            }

            const TableCell& cell = row.cells[column];
            const float glyph = cell.icon.has_value() ? context.metric(ThemeMetric::SmallIconSize) + glyphSpacing * scale : 0.0F;
            const float wrap = std::max(1.0F, widths[column] - cellHorizontalPadding * 2.0F * scale - glyph);
            height = std::max(height, WidgetHelper::paragraphSize(context, cellFont(context, cell), context.text(cell.text), wrap).y + cellVerticalPadding * 2.0F * scale);
        }

        built.tops.push_back(built.tops.back() + std::ceil(height));
    }

    built.key = m_version;
    built.generation = generation;
    built.stretch = stretchWidth;

    return built.tops;
}

// The widest set of actions of the rows is counted when the rows arrive, so every drawn row reads it at once.
float Table::actionsWidth(RenderContext& context) const {
    if (m_mostActions == 0) {
        return 0.0F;
    }

    const float scale = context.scale();
    return static_cast<float>(m_mostActions) * (context.metric(ThemeMetric::CompactButtonSize) + actionSpacing * scale) + cellHorizontalPadding * scale;
}

FontRole Table::cellFont(RenderContext& context, const TableCell& cell) const {
    return context.font(cell.monospace ? ThemeFont::Monospace : ThemeFont::Interface);
}

void Table::drawHeader(RenderContext& context, const ImRect& bounds, const std::vector<float>& widths) {
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const float padding = cellHorizontalPadding * context.scale();
    list.AddRectFilled(bounds.Min, bounds.Max, WidgetHelper::ink(context.color(ThemeColor::Panel)));
    Painter::horizontalDivider(list, ImVec2(bounds.Min.x, bounds.Max.y - context.metric(ThemeMetric::LineWidth)), bounds.GetWidth(), context.metric(ThemeMetric::LineWidth), context.color(ThemeColor::Border));

    float x = bounds.Min.x;

    for (std::size_t column = 0; column < m_columns.size(); ++column) {
        const ImRect cell(x + padding, bounds.Min.y, x + widths[column] - padding, bounds.Max.y - context.metric(ThemeMetric::LineWidth));
        WidgetHelper::alignedText(context, list, context.font(ThemeFont::Interface), cell, context.color(ThemeColor::TextMuted), context.text(m_columns[column].title), m_columns[column].align);
        x += widths[column];
    }
}

// Everything a row selected in the accent draws takes the ink made for the accent, because a color that carries a meaning stops carrying it on the accent.
// A subtle selection keeps every color of the row on the hover background instead.
void Table::drawRow(RenderContext& context, std::size_t index, const ImRect& row, const std::vector<float>& widths) {
    const TableRow& data = m_rows[index];
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const float scale = context.scale();
    const float padding = cellHorizontalPadding * scale;
    const float icon = context.metric(ThemeMetric::SmallIconSize);
    const bool selected = data.id == m_selected;
    const bool accented = selected && m_selection == Selection::Accent;

    ImGui::PushID(data.id.data(), data.id.data() + data.id.size());
    ImGui::SetNextItemAllowOverlap();
    const ButtonState state = WidgetHelper::interact(ImGui::GetID("##row"), row);

    if (selected) {
        list.AddRectFilled(row.Min, row.Max, WidgetHelper::ink(context.color(accented ? ThemeColor::Accent : ThemeColor::Hover)));
    } else if (state.hovered) {
        list.AddRectFilled(row.Min, row.Max, WidgetHelper::ink(context.color(ThemeColor::Hover)));
    } else if (m_alternate && index % 2 == 1) {
        list.AddRectFilled(row.Min, row.Max, WidgetHelper::ink(context.color(ThemeColor::Panel)));
    }

    // The stretch column wraps its text under a leading icon, and every other column keeps one aligned line.
    float x = row.Min.x;

    for (std::size_t column = 0; column < m_columns.size(); ++column) {
        const TableCell& cell = data.cells[column];
        const FontRole font = cellFont(context, cell);
        const Color ink = accented ? context.color(ThemeColor::OnAccent) : cell.tone.has_value() ? context.color(Tones::text(*cell.tone)) : context.color(cell.muted ? ThemeColor::TextMuted : ThemeColor::Text);
        const bool wraps = m_columns[column].width == ColumnWidth::Stretch;
        float left = x + padding;

        if (cell.icon.has_value()) {
            const float iconTop = wraps ? row.Min.y + cellVerticalPadding * scale + (WidgetHelper::lineHeight(context, font) - icon) / 2.0F : row.GetCenter().y - icon / 2.0F;
            IconCatalog::draw(context.fonts(), list, *cell.icon, ImFloor(ImVec2(left, iconTop)), icon, ink);
            left += icon + glyphSpacing * scale;
        }

        const ImRect textBounds(left, row.Min.y, x + widths[column] - padding, row.Max.y);

        if (wraps) {
            WidgetHelper::paragraph(context, list, font, ImRect(textBounds.Min.x, row.Min.y + cellVerticalPadding * scale, textBounds.Max.x, row.Max.y), ink, context.text(cell.text), m_columns[column].align);
        } else {
            WidgetHelper::alignedText(context, list, font, textBounds, ink, context.text(cell.text), m_columns[column].align);
        }

        x += widths[column];
    }

    const float button = context.metric(ThemeMetric::CompactButtonSize);
    float actionX = row.Max.x - actionsWidth(context) + padding / 2.0F;

    for (const auto& action : data.actions) {
        const ImRect area(actionX, row.GetCenter().y - button / 2.0F, actionX + button, row.GetCenter().y + button / 2.0F);
        const ButtonState pressed = WidgetHelper::interact(ImGui::GetID(action.id.data(), action.id.data() + action.id.size()), area);

        if (!action.tooltip.empty()) {
            WidgetHelper::itemTooltip(context, context.text(action.tooltip));
        }

        if (pressed.hovered) {
            const ThemeColor hover = !selected ? ThemeColor::Hover : accented ? ThemeColor::AccentHover : ThemeColor::Pressed;
            list.AddRectFilled(area.Min, area.Max, WidgetHelper::ink(context.color(hover)), context.metric(ThemeMetric::ControlRadius));
        }

        const Color ink = accented ? context.color(ThemeColor::OnAccent) : action.destructive ? context.color(ThemeColor::Danger) : action.tone.has_value() ? context.color(Tones::color(*action.tone)) : context.color(pressed.hovered ? ThemeColor::Text : ThemeColor::TextMuted);
        IconCatalog::draw(context.fonts(), list, action.icon, ImFloor(ImVec2(area.GetCenter().x - icon / 2.0F, area.GetCenter().y - icon / 2.0F)), icon, ink);

        if (pressed.pressed) {
            context.emit(id(), "action", {{"id", data.id}, {"action", action.id}});
        }

        actionX += button + actionSpacing * scale;
    }

    ImGui::PopID();

    if (state.pressed && !selected) {
        m_selected = data.id;
        context.emit(id(), "select", {{"id", data.id}}, {{"selected", "id"}});
    }

    if (state.doubleClicked()) {
        context.emit(id(), "activate", {{"id", data.id}});
    }
}

void Table::moveSelection(RenderContext& context, int step) {
    if (m_rows.empty()) {
        return;
    }

    // clang-format off
    const auto current = std::ranges::find_if(m_rows, [this](const TableRow& row) { return row.id == m_selected; });
    // clang-format on
    const auto index = current == m_rows.end() ? std::ptrdiff_t{0} : std::clamp<std::ptrdiff_t>(std::distance(m_rows.begin(), current) + step, 0, static_cast<std::ptrdiff_t>(m_rows.size()) - 1);
    const std::string& next = m_rows[static_cast<std::size_t>(index)].id;

    if (next == m_selected) {
        return;
    }

    m_selected = next;
    m_scrollTo = next;
    context.emit(id(), "select", {{"id", next}}, {{"selected", "id"}});
}

} // namespace workpane::ui
