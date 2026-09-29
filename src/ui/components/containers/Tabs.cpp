#include "ui/components/containers/Tabs.h"

#include "ui/ButtonState.h"
#include "ui/IconCatalog.h"
#include "ui/Painter.h"
#include "ui/Texture.h"
#include "ui/TextureCache.h"
#include "ui/WheelScale.h"
#include "ui/Widgets.h"
#include "ui/model/TextValue.h"
#include "ui/theme/Theme.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <set>
#include <string>
#include <utility>

namespace workpane::ui {

Tabs::Tabs(NodeId id) : Component(id) {}

std::string_view Tabs::kind() const {
    return "tabs";
}

std::size_t Tabs::childLimit() const {
    return unlimitedChildren;
}

Result<void> Tabs::validateChildren() const {
    if (!children().empty() && children().size() != m_items.size()) {
        return Result<void>::failure({"ui_tabs_pages", "A tab strip with pages carries exactly one page per tab", std::to_string(children().size())});
    }

    return Result<void>::success();
}

float Tabs::tabWidth(RenderContext& context, const TabItem& item, bool closable) {
    const float padding = context.metric(ThemeMetric::TabHorizontalPadding);
    const float icon = item.icon.has_value() || !item.image.empty() ? context.metric(ThemeMetric::SmallIconSize) + tabIconSpacing * context.scale() : 0.0F;
    const float close = item.closable.value_or(closable) ? context.metric(ThemeMetric::TabCloseSize) + tabCloseReserve * context.scale() : 0.0F;
    const float text = Widgets::textSize(context, context.font(ThemeFont::Interface), context.text(item.text)).x;
    const float width = padding * 2.0F + context.metric(ThemeMetric::TabIndicatorSize) + context.metric(ThemeMetric::TabIndicatorSpacing) + icon + text + close;
    return std::ceil(std::clamp(width, context.metric(ThemeMetric::TabMinimumWidth), context.metric(ThemeMetric::TabMaximumWidth)));
}

Tabs::Strip Tabs::drawStrip(RenderContext& context, const ImRect& bounds) {
    Strip strip;
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const float scale = context.scale();
    const float padding = context.metric(ThemeMetric::TabHorizontalPadding);
    const float indicator = context.metric(ThemeMetric::TabIndicatorSize);
    const float indicatorSpacing = context.metric(ThemeMetric::TabIndicatorSpacing);
    const float closeSize = context.metric(ThemeMetric::TabCloseSize);
    const float iconSize = context.metric(ThemeMetric::SmallIconSize);
    const float addSize = context.metric(ThemeMetric::CompactButtonSize);
    // A strip over pages draws the line between its tabs and the page, while a strip alone leaves that line to the container that holds it.
    const bool paged = !children().empty();
    const float stripBottom = paged ? bounds.Max.y - context.metric(ThemeMetric::LineWidth) : bounds.Max.y;

    list.AddRectFilled(bounds.Min, bounds.Max, Widgets::ink(context.color(ThemeColor::Panel)));

    float total = m_addButton ? addSize + addButtonMargin * 2.0F * scale : 0.0F;

    for (const auto& item : m_items) {
        total += tabWidth(context, item, m_closable);
    }

    // A strip wider than its room scrolls with the wheel on either axis as far as any view and takes the wheel from the view around it, and the offset never leaves the range the tabs occupy.
    const ImVec2 moved = WheelScale::distance();
    const bool scrollable = ImGui::IsMouseHoveringRect(bounds.Min, bounds.Max) && ImGui::IsWindowHovered() && total > bounds.GetWidth();

    if (scrollable) {
        WheelScale::claim(ImGui::GetID("##wheel"));
    }

    if (scrollable && (moved.x != 0.0F || moved.y != 0.0F)) {
        m_offset -= moved.x + moved.y;
    }

    m_offset = std::clamp(m_offset, 0.0F, std::max(0.0F, total - bounds.GetWidth()));
    ImGui::PushClipRect(bounds.Min, bounds.Max, true);
    float x = bounds.Min.x - m_offset;
    std::optional<std::size_t> dragged;
    std::size_t target = 0;
    std::vector<ImRect> slots;
    slots.reserve(m_items.size());

    for (std::size_t index = 0; index < m_items.size(); ++index) {
        const TabItem& item = m_items[index];
        const float width = tabWidth(context, item, m_closable);
        const ImRect tab(x, bounds.Min.y, x + width, stripBottom);
        slots.push_back(tab);
        const bool selected = item.id == m_current;
        const bool itemClosable = item.closable.value_or(m_closable);

        ImGui::PushID(item.id.data(), item.id.data() + item.id.size());
        ImGui::SetNextItemAllowOverlap();
        const ButtonState state = Widgets::interact(ImGui::GetID("##tab"), tab, ImGuiButtonFlags_PressedOnClick);

        if (!item.tooltip.empty()) {
            Widgets::itemTooltip(context, context.text(item.tooltip));
        }

        if (m_movable && state.held && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            dragged = index;
        }

        if (ImGui::GetIO().MousePos.x >= tab.Min.x) {
            target = index;
        }

        list.AddRectFilled(tab.Min, tab.Max, Widgets::ink(context.color(selected ? ThemeColor::Window : state.hovered ? ThemeColor::Hover : ThemeColor::Panel)));
        Painter::verticalDivider(list, ImVec2(tab.Max.x - context.metric(ThemeMetric::LineWidth), tab.Min.y), tab.GetHeight(), context.metric(ThemeMetric::LineWidth), context.color(ThemeColor::Border));

        float left = tab.Min.x + padding;
        const float right = tab.Max.x - padding - (itemClosable ? closeSize + tabCloseReserve * scale : 0.0F);
        Painter::indicator(list, ImVec2(left + indicator / 2.0F, tab.GetCenter().y), indicator, context.color(selected ? ThemeColor::Accent : ThemeColor::TextMuted));
        left += indicator + indicatorSpacing;

        if (item.icon.has_value()) {
            IconCatalog::draw(context.fonts(), list, *item.icon, ImFloor(ImVec2(left, tab.GetCenter().y - iconSize / 2.0F)), iconSize, context.color(selected ? ThemeColor::Text : ThemeColor::TextMuted));
            left += iconSize + tabIconSpacing * scale;
        }

        // An image keeps its square while it decodes, so the title never moves when it appears.
        if (!item.image.empty()) {
            const Texture& image = context.textures().requestData(item.image);
            const ImVec2 corner = ImFloor(ImVec2(left, tab.GetCenter().y - iconSize / 2.0F));

            if (image.state == TextureState::Ready) {
                list.AddImage(image.reference(), corner, ImVec2(corner.x + iconSize, corner.y + iconSize));
            }

            left += iconSize + tabIconSpacing * scale;
        }

        Widgets::alignedText(context, list, context.font(ThemeFont::Interface), ImRect(left, tab.Min.y, right, tab.Max.y), context.color(selected ? ThemeColor::Text : ThemeColor::TextMuted), context.text(item.text), TextAlign::Start);

        if (itemClosable && Widgets::closeCircle(context, "##close", ImVec2(tab.Max.x - padding - closeSize / 2.0F, tab.GetCenter().y), closeSize)) {
            strip.closed = item.id;
        }

        if (itemClosable && state.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            strip.closed = item.id;
        }

        if (state.pressed && !selected) {
            strip.selected = item.id;
        }

        if (state.doubleClicked()) {
            strip.activated = item.id;
        }

        ImGui::PopID();
        x += width;
    }

    // A dragged tab takes the place of the tab under the pointer, so it follows the pointer one slot at a time.
    if (dragged.has_value() && target != *dragged && lands(slots, *dragged, target, ImGui::GetIO().MousePos.x)) {
        strip.moved = std::make_pair(*dragged, target);
        context.requestFrame();
    }

    if (m_addButton) {
        const ImRect add(x + addButtonMargin * scale, bounds.GetCenter().y - addSize / 2.0F, x + addButtonMargin * scale + addSize, bounds.GetCenter().y + addSize / 2.0F);
        strip.added = Widgets::button(context, "##add", add, {}, Icon::Add, ButtonVariant::Icon);
    }

    ImGui::PopClipRect();

    if (paged) {
        Painter::horizontalDivider(list, ImVec2(bounds.Min.x, stripBottom), bounds.GetWidth(), context.metric(ThemeMetric::LineWidth), context.color(ThemeColor::Border));
    }

    return strip;
}

// A tab moves to a slot only when the pointer would fall inside it there, so two tabs of different widths never trade places back and forth under a resting pointer.
bool Tabs::lands(const std::vector<ImRect>& slots, std::size_t dragged, std::size_t target, float pointer) {
    const float width = slots[dragged].GetWidth();

    if (target < dragged) {
        return pointer < slots[target].Min.x + width;
    }

    return pointer >= slots[target].Max.x - width;
}

Alignment Tabs::defaultRowAlignment() const {
    return Alignment::Stretch;
}

void Tabs::readProperties(json::ObjectReader& reader) {
    reader.read("current", m_current, json::Presence::Optional).read("closable", m_closable, json::Presence::Optional).read("addButton", m_addButton, json::Presence::Optional).read("movable", m_movable, json::Presence::Optional);

    if (!reader.contains("items")) {
        return;
    }

    const json::Json* items = &json::ObjectReader::absent();
    reader.readArray("items", items);
    std::vector<TabItem> parsed;

    for (const auto& entry : *items) {
        TabItem item;
        const json::Json* text = &json::ObjectReader::absent();
        const json::Json* tooltip = &json::ObjectReader::absent();
        std::string icon;
        bool closable = false;
        json::ObjectReader itemReader(entry, "tabs.items");
        itemReader.readText("id", item.id).readAny("text", text).read("icon", icon, json::Presence::Optional).read("image", item.image, json::Presence::Optional).read("closable", closable, json::Presence::Optional).readAny("tooltip", tooltip, json::Presence::Optional);

        if (const auto finished = itemReader.finish(); !finished.hasValue()) {
            fail(finished.error());
            return;
        }

        auto label = TextValue::parse(*text, "tabs.items.text");
        auto hint = tooltip->is_null() ? Result<TextValue>::success(TextValue()) : TextValue::parse(*tooltip, "tabs.items.tooltip");

        if (!label.hasValue() || !hint.hasValue()) {
            fail(label.hasValue() ? hint.error() : label.error());
            return;
        }

        // An image is a data address of a small picture, such as the icon of a web page, and a tab shows either an icon or an image.
        if (!item.image.empty() && (!item.image.starts_with(imagePrefix) || item.image.size() > largestImage || !icon.empty())) {
            fail({"ui_tab_image_invalid", "A tab shows an icon or an image given as a data address of a small picture", item.id});
            return;
        }

        if (!icon.empty()) {
            item.icon = IconCatalog::parse(icon);

            if (!item.icon.has_value()) {
                fail({"ui_icon_unknown", "An icon names nothing in the painted set", icon});
                return;
            }
        }

        if (entry.contains("closable")) {
            item.closable = closable;
        }

        item.text = std::move(label.value());
        item.tooltip = std::move(hint.value());
        parsed.push_back(std::move(item));
    }

    m_items = std::move(parsed);
    m_itemSpecs = *items;
}

Component::Restore Tabs::keep() {
    return kept(m_current, m_closable, m_addButton, m_movable, m_items, m_itemSpecs);
}

Result<void> Tabs::validate() const {
    std::set<std::string, std::less<>> seen;

    for (const auto& item : m_items) {
        if (!seen.insert(item.id).second) {
            return Result<void>::failure({"ui_item_duplicate", "Two tabs share one identifier", item.id});
        }
    }

    if (m_items.empty() != m_current.empty() || (!m_current.empty() && !seen.contains(m_current))) {
        return Result<void>::failure({"ui_tabs_current", "The current tab names no tab, or a strip with tabs names none", m_current});
    }

    return Result<void>::success();
}

ImVec2 Tabs::measureContent(RenderContext& context, float availableWidth) {
    const float strip = context.metric(ThemeMetric::WorkspaceBarHeight);

    if (children().empty()) {
        return {availableWidth, strip};
    }

    float page = 0.0F;

    // A strip without pages measures its tabs alone, while a strip with pages carries one page for each tab.
    for (std::size_t index = 0; index < std::min(m_items.size(), children().size()); ++index) {
        if (m_items[index].id == m_current) {
            page = children()[index]->measure(context, availableWidth).y;
        }
    }

    return {availableWidth, strip + page};
}

void Tabs::render(RenderContext& context, const ImRect& bounds) {
    const float strip = context.metric(ThemeMetric::WorkspaceBarHeight);
    const auto result = drawStrip(context, ImRect(bounds.Min, ImVec2(bounds.Max.x, bounds.Min.y + strip)));

    if (result.selected.has_value()) {
        m_current = *result.selected;
        context.emit(id(), "select", {{"id", m_current}}, {{"current", "id"}});
    }

    if (result.closed.has_value()) {
        context.emit(id(), "close", {{"id", *result.closed}});
    }

    if (result.activated.has_value()) {
        context.emit(id(), "activate", {{"id", *result.activated}});
    }

    if (result.added) {
        context.emit(id(), "add");
    }

    // A moved tab reports the items in their new order and the order of the pages that moved with them, which its node takes on the Lua side as well.
    if (result.moved.has_value()) {
        const auto [from, to] = *result.moved;
        const std::string moved = m_items[from].id;
        move(from, to);
        std::vector<NodeId> pages;

        for (const auto& page : children()) {
            pages.push_back(page->id());
        }

        context.emit(id(), "move", {{"id", moved}, {"index", to}, {"items", m_itemSpecs}}, {{"items", "items"}}, std::move(pages));
    }

    for (std::size_t index = 0; index < std::min(m_items.size(), children().size()); ++index) {
        if (m_items[index].id == m_current) {
            children()[index]->draw(context, ImRect(bounds.Min.x, bounds.Min.y + strip, bounds.Max.x, bounds.Max.y));
        }
    }
}

// A tab and its page move together, so the page drawn for each tab stays the one declared for it.
void Tabs::move(std::size_t from, std::size_t to) {
    // clang-format off
    const auto rotate = [from, to](auto& entries) {
        if (from < to) {
            std::rotate(entries.begin() + static_cast<std::ptrdiff_t>(from), entries.begin() + static_cast<std::ptrdiff_t>(from) + 1, entries.begin() + static_cast<std::ptrdiff_t>(to) + 1);
            return;
        }

        std::rotate(entries.begin() + static_cast<std::ptrdiff_t>(to), entries.begin() + static_cast<std::ptrdiff_t>(from), entries.begin() + static_cast<std::ptrdiff_t>(from) + 1);
    };
    // clang-format on

    rotate(m_items);
    rotate(m_itemSpecs);

    if (children().size() == m_items.size()) {
        rotate(children());
    }
}

} // namespace workpane::ui
