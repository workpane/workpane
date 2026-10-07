#include "ui/components/collections/Tree.h"

#include "ui/ButtonState.h"
#include "ui/Color.h"
#include "ui/IconCatalog.h"
#include "ui/Menus.h"
#include "ui/Painter.h"
#include "ui/WidgetHelper.h"
#include "ui/model/TextValue.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeColorNames.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace workpane::ui {

Tree::Tree(NodeId id) : Component(id) {}

std::string_view Tree::kind() const {
    return "tree";
}

// Revealing scrolls an item into view on the next frame, and the plugin opens the branches above it by their expansion.
Result<void> Tree::command(RenderContext& context, std::string_view name, const json::Json& arguments) {
    if (name != "reveal") {
        return Component::command(context, name, arguments);
    }

    std::string item;
    json::ObjectReader reader(arguments, "tree.reveal");
    reader.readText("id", item);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return finished;
    }

    m_reveal = std::move(item);
    context.requestFrame();

    return Result<void>::success();
}

void Tree::readProperties(json::ObjectReader& reader) {
    reader.read("selected", m_selected, json::Presence::Optional);

    if (!reader.contains("items")) {
        return;
    }

    const json::Json* items = &json::ObjectReader::absent();
    reader.readArray("items", items);
    std::set<std::string, std::less<>> seen;
    auto parsed = parseItems(*items, 0, seen);

    if (!parsed.hasValue()) {
        fail(parsed.error());
        return;
    }

    m_items = std::move(parsed.value());
    m_rowsStale = true;
    applyExpansion(m_items);
}

Component::Restore Tree::keep() {
    return kept(m_selected, m_items, m_rowsStale, m_expanded);
}

ImVec2 Tree::measureContent(RenderContext& context, float availableWidth) {
    return {availableWidth, rowHeight(context) * static_cast<float>(visibleRows().size())};
}

bool Tree::revealing() const {
    return !m_reveal.empty();
}

void Tree::render(RenderContext& context, const ImRect& bounds) {
    const std::vector<VisibleRow>& rows = visibleRows();

    ImDrawList& list = *ImGui::GetWindowDrawList();
    const float scale = context.scale();
    const float height = rowHeight(context);
    const float icon = context.metric(ThemeMetric::SmallIconSize);
    const FontRole font = context.font(ThemeFont::Interface);

    for (std::size_t index = 0; index < rows.size(); ++index) {
        const TreeItem& item = *rows[index].item;
        const ImRect row(bounds.Min.x, bounds.Min.y + height * static_cast<float>(index), bounds.Max.x, bounds.Min.y + height * static_cast<float>(index + 1));

        // A revealed item is scrolled into the view that holds the tree before rows out of sight are skipped.
        if (!m_reveal.empty() && item.id == m_reveal) {
            ImGui::ScrollToRect(ImGui::GetCurrentWindow(), row, ImGuiScrollFlags_KeepVisibleCenterY);
            m_reveal.clear();
        }

        if (!ImGui::IsRectVisible(row.Min, row.Max)) {
            continue;
        }

        const bool selected = item.id == m_selected;
        const bool branch = item.branch || !item.children.empty();
        const bool expanded = m_expanded.contains(item.id);
        const float indent = (static_cast<float>(rows[index].depth) * treeIndent + 4.0F) * scale;
        const ImRect chevron(row.Min.x + indent, row.Min.y, row.Min.x + indent + icon, row.Max.y);

        ImGui::PushID(item.id.data(), item.id.data() + item.id.size());
        ImGui::SetNextItemAllowOverlap();
        const ButtonState state = WidgetHelper::interact(ImGui::GetID("##row"), row);
        const ButtonState toggle = branch ? WidgetHelper::interact(ImGui::GetID("##toggle"), chevron) : ButtonState{};
        ImGui::PopID();

        if (selected) {
            list.AddRectFilled(row.Min, row.Max, WidgetHelper::ink(context.color(ThemeColor::Accent)));
        } else if (state.hovered || toggle.hovered) {
            list.AddRectFilled(row.Min, row.Max, WidgetHelper::ink(context.color(ThemeColor::Hover)));
        }

        const Color ink = context.color(selected ? ThemeColor::OnAccent : ThemeColor::Text);

        if (branch) {
            Painter::chevron(list, chevron.GetCenter(), chevronWidth * scale, expanded ? ChevronDirection::Down : ChevronDirection::Right, selected ? ink : context.color(ThemeColor::TextMuted));
        }

        float x = chevron.Max.x + glyphSpacing * scale;

        if (item.icon.has_value()) {
            IconCatalog::draw(context.fonts(), list, *item.icon, ImFloor(ImVec2(x, row.GetCenter().y - icon / 2.0F)), icon, selected ? ink : context.color(item.iconColor.value_or(ThemeColor::TextMuted)));
            x += icon + glyphSpacing * scale;
        }

        WidgetHelper::alignedText(context, list, font, ImRect(x, row.Min.y, row.Max.x - glyphSpacing * scale, row.Max.y), ink, context.text(item.text), TextAlign::Start);

        // Opening or closing a branch never changes the selection, and a double click on a branch opens it like its chevron does.
        const bool toggled = toggle.pressed || (branch && state.doubleClicked());

        if (toggled) {
            if (expanded) {
                m_expanded.erase(item.id);
            } else {
                m_expanded.insert(item.id);
            }

            m_rowsStale = true;

            context.emit(id(), "toggle", {{"id", item.id}, {"expanded", !expanded}});
        }

        if (state.pressed && !selected) {
            m_selected = item.id;
            context.emit(id(), "select", {{"id", item.id}}, {{"selected", "id"}});
        }

        if (!branch && state.doubleClicked()) {
            context.emit(id(), "activate", {{"id", item.id}});
        }

        if (item.draggable && state.held && m_dragged.empty() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            m_dragged = item.id;
        }

        // A secondary click on an item with a menu selects it first, so the menu acts on the item the reader pointed at.
        if (!item.menu.empty() && state.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && context.claimMenuClick()) {
            m_menuRow = item.id;
            ImGui::OpenPopup("##itemMenu");

            if (!selected) {
                m_selected = item.id;
                context.emit(id(), "select", {{"id", item.id}}, {{"selected", "id"}});
            }
        }
    }

    if (!m_dragged.empty() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        finishDrag(context, bounds, rows);
    }

    if (const auto target = m_dragged.empty() ? std::nullopt : dropTarget(context, bounds, rows); target.has_value()) {
        const Color accent = context.color(ThemeColor::Accent);
        const float thickness = dropMarkerThickness * scale;

        if (target->into) {
            list.AddRect(target->marker.Min, target->marker.Max, WidgetHelper::ink(accent), context.metric(ThemeMetric::ControlRadius), thickness);
        } else {
            list.AddRectFilled(ImVec2(target->marker.Min.x, target->marker.Min.y - thickness / 2.0F), ImVec2(target->marker.Max.x, target->marker.Min.y + thickness / 2.0F), WidgetHelper::ink(accent));
        }
    }

    if (!m_dragged.empty()) {
        context.requestFrame();
    }

    drawItemMenu(context);
}

// The pointer drops into a droppable item near its middle, and before or after an item whose parent takes drops near its edges.
std::optional<Tree::DropTarget> Tree::dropTarget(RenderContext& context, const ImRect& bounds, const std::vector<VisibleRow>& rows) const {
    const ImVec2 pointer = ImGui::GetIO().MousePos;
    const TreeItem* dragged = find(m_items, m_dragged);

    if (rows.empty() || dragged == nullptr || !bounds.Contains(pointer)) {
        return std::nullopt;
    }

    const float height = rowHeight(context);
    const std::size_t index = std::min(rows.size() - 1, static_cast<std::size_t>((pointer.y - bounds.Min.y) / height));
    const VisibleRow& row = rows[index];

    if (holds(*dragged, row.item->id)) {
        return std::nullopt;
    }

    const float top = bounds.Min.y + height * static_cast<float>(index);
    const float fraction = (pointer.y - top) / height;
    const bool edge = fraction < dropIntoBand || fraction > 1.0F - dropIntoBand;
    const bool between = row.parent != nullptr && row.parent->droppable && (edge || !row.item->droppable);

    if (row.item->droppable && !between) {
        return DropTarget{row.item->id, position(*row.item, {}, m_dragged), ImRect(bounds.Min.x, top, bounds.Max.x, top + height), true};
    }

    if (!between) {
        return std::nullopt;
    }

    const bool after = fraction >= 0.5F;
    const float indent = (static_cast<float>(row.depth) * treeIndent + 4.0F) * context.scale();
    const float line = after ? top + height : top;
    return DropTarget{row.parent->id, position(*row.parent, row.item->id, m_dragged) + (after ? 1U : 0U), ImRect(bounds.Min.x + indent, line, bounds.Max.x, line), false};
}

// A drop that puts the item back where it was reports nothing, because nothing moved.
void Tree::finishDrag(RenderContext& context, const ImRect& bounds, const std::vector<VisibleRow>& rows) {
    const auto target = dropTarget(context, bounds, rows);
    const std::string dragged = std::exchange(m_dragged, {});

    if (!target.has_value()) {
        return;
    }

    for (const auto& row : rows) {
        if (row.item->id == dragged && row.parent != nullptr && row.parent->id == target->parent && position(*row.parent, dragged, {}) == target->index) {
            return;
        }
    }

    context.emit(id(), "move", {{"item", dragged}, {"parent", target->parent}, {"index", target->index}});
}

void Tree::drawItemMenu(RenderContext& context) {
    const TreeItem* item = m_menuRow.empty() ? nullptr : find(m_items, m_menuRow);

    if (item == nullptr || !ImGui::IsPopupOpen("##itemMenu")) {
        m_menuRow.clear();
        return;
    }

    if (const auto picked = Menus::popup(context, "##itemMenu", item->menu); picked.has_value()) {
        context.emit(id(), "item-menu", {{"id", item->id}, {"item", *picked}});
    }
}

bool Tree::holds(const TreeItem& item, std::string_view id) {
    if (item.id == id) {
        return true;
    }

    // clang-format off
    return std::ranges::any_of(item.children, [id](const TreeItem& child) { return holds(child, id); });
    // clang-format on
}

// The place of a child among the children of its parent that skips one of them, or the count of those children for a child the parent does not hold.
std::size_t Tree::position(const TreeItem& parent, std::string_view child, std::string_view skipped) {
    std::size_t index = 0;

    for (const auto& candidate : parent.children) {
        if (candidate.id == child) {
            return index;
        }

        if (candidate.id != skipped) {
            ++index;
        }
    }

    return index;
}

Result<void> Tree::validate() const {
    if (!m_selected.empty() && find(m_items, m_selected) == nullptr) {
        return Result<void>::failure({"ui_item_unknown", "The selection names no item", m_selected});
    }

    return Result<void>::success();
}

const TreeItem* Tree::find(const std::vector<TreeItem>& items, std::string_view id) const {
    for (const auto& item : items) {
        if (item.id == id) {
            return &item;
        }

        if (const TreeItem* nested = find(item.children, id); nested != nullptr) {
            return nested;
        }
    }

    return nullptr;
}

Result<std::vector<TreeItem>> Tree::parseItems(const json::Json& items, std::size_t depth, std::set<std::string, std::less<>>& seen) {
    if (depth > maximumTreeDepth) {
        return Result<std::vector<TreeItem>>::failure({"ui_tree_too_deep", "A tree nests deeper than the declared bound", std::to_string(depth)});
    }

    std::vector<TreeItem> parsed;

    for (const auto& entry : items) {
        TreeItem item;
        const json::Json* text = &json::ObjectReader::absent();
        const json::Json* children = &json::ObjectReader::emptyList();
        const json::Json* menu = &json::ObjectReader::emptyList();
        std::string icon;
        std::string iconColor;
        bool expanded = false;
        json::ObjectReader reader(entry, "tree.items");
        reader.readText("id", item.id).readAny("text", text).read("icon", icon, json::Presence::Optional).read("iconColor", iconColor, json::Presence::Optional).read("expanded", expanded, json::Presence::Optional).read("branch", item.branch, json::Presence::Optional).readArray("children", children, json::Presence::Optional);
        reader.read("draggable", item.draggable, json::Presence::Optional).read("droppable", item.droppable, json::Presence::Optional).readArray("menu", menu, json::Presence::Optional);

        if (const auto finished = reader.finish(); !finished.hasValue()) {
            return Result<std::vector<TreeItem>>::failure(finished.error());
        }

        // Identities are unique across the whole tree, because selection and expansion are reported by them.
        if (!seen.insert(item.id).second) {
            return Result<std::vector<TreeItem>>::failure({"ui_item_duplicate", "Two items share one identifier", item.id});
        }

        auto label = TextValue::parse(*text, "tree.items.text");
        auto glyph = IconCatalog::parseOptional(icon);
        auto ink = ThemeColorNames::parseOptional(iconColor);
        auto nested = parseItems(*children, depth + 1, seen);
        auto actions = Menus::parse(*menu, "tree.items.menu");

        if (!label.hasValue()) {
            return Result<std::vector<TreeItem>>::failure(label.error());
        }

        if (!glyph.hasValue()) {
            return Result<std::vector<TreeItem>>::failure(glyph.error());
        }

        if (!ink.hasValue()) {
            return Result<std::vector<TreeItem>>::failure(ink.error());
        }

        if (!nested.hasValue()) {
            return nested;
        }

        if (!actions.hasValue()) {
            return Result<std::vector<TreeItem>>::failure(actions.error());
        }

        item.text = std::move(label.value());
        item.icon = glyph.value();
        item.iconColor = ink.value();
        item.children = std::move(nested.value());
        item.menu = std::move(actions.value());

        // An item that declares no expansion keeps the one the reader chose.
        if (entry.contains("expanded")) {
            item.expanded = expanded;
        }

        parsed.push_back(std::move(item));
    }

    return Result<std::vector<TreeItem>>::success(std::move(parsed));
}

void Tree::collect(const std::vector<TreeItem>& items, const TreeItem* parent, std::size_t depth, std::vector<VisibleRow>& rows) const {
    for (const auto& item : items) {
        rows.push_back({&item, parent, depth});

        if (m_expanded.contains(item.id)) {
            collect(item.children, &item, depth + 1, rows);
        }
    }
}

// The rows on show are gathered once for the items and the branches open, and gathered again only after either changes.
const std::vector<Tree::VisibleRow>& Tree::visibleRows() {
    if (m_rowsStale) {
        m_rows.clear();
        collect(m_items, nullptr, 0, m_rows);
        m_rowsStale = false;
    }

    return m_rows;
}

// Only an item that states its expansion changes it, so replacing the items keeps every branch the reader opened.
void Tree::applyExpansion(const std::vector<TreeItem>& items) {
    for (const auto& item : items) {
        if (item.expanded.has_value() && *item.expanded) {
            m_expanded.insert(item.id);
        } else if (item.expanded.has_value()) {
            m_expanded.erase(item.id);
        }

        applyExpansion(item.children);
    }
}

float Tree::rowHeight(RenderContext& context) const {
    return WidgetHelper::lineHeight(context, context.font(ThemeFont::Interface)) + treeRowPadding * context.scale();
}

} // namespace workpane::ui
