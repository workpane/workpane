#include "ui/components/collections/List.h"

#include "ui/ButtonState.h"
#include "ui/RowContent.h"
#include "ui/WidgetHelper.h"
#include "ui/model/TextValue.h"
#include "ui/theme/ThemeColorNames.h"

#include <cstddef>
#include <functional>
#include <set>
#include <utility>

namespace workpane::ui {

List::List(NodeId id) : Component(id) {}

std::string_view List::kind() const {
    return "list";
}

// Revealing scrolls an item into view on the next frame, which is how a list chosen from the keyboard keeps its selection in sight.
Result<void> List::command(RenderContext& context, std::string_view name, const json::Json& arguments) {
    if (name != "reveal") {
        return Component::command(context, name, arguments);
    }

    std::string item;
    json::ObjectReader reader(arguments, "list.reveal");
    reader.readText("id", item);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return finished;
    }

    m_reveal = std::move(item);
    context.requestFrame();

    return Result<void>::success();
}

void List::readProperties(json::ObjectReader& reader) {
    reader.read("selected", m_selected, json::Presence::Optional);
    reader.readChoice("style", m_style, {{"default", ListStyle::Default}, {"navigation", ListStyle::Navigation}}, json::Presence::Optional);

    if (!reader.contains("items")) {
        return;
    }

    const json::Json* items = &json::ObjectReader::absent();
    reader.readArray("items", items);
    std::vector<ListItem> parsed;

    for (const auto& entry : *items) {
        ListItem item;
        const json::Json* text = &json::ObjectReader::absent();
        const json::Json* detail = &json::ObjectReader::absent();
        std::string icon;
        std::string iconColor;
        json::ObjectReader itemReader(entry, "list.items");
        itemReader.readText("id", item.id).readAny("text", text).readAny("detail", detail, json::Presence::Optional).read("icon", icon, json::Presence::Optional).read("iconColor", iconColor, json::Presence::Optional);

        if (const auto finished = itemReader.finish(); !finished.hasValue()) {
            fail(finished.error());
            return;
        }

        auto label = TextValue::parse(*text, "list.items.text");
        auto secondary = detail->is_null() ? Result<TextValue>::success(TextValue()) : TextValue::parse(*detail, "list.items.detail");
        auto glyph = IconCatalog::parseOptional(icon);
        auto ink = ThemeColorNames::parseOptional(iconColor);

        for (const Error* error : {label.hasValue() ? nullptr : &label.error(), secondary.hasValue() ? nullptr : &secondary.error(), glyph.hasValue() ? nullptr : &glyph.error(), ink.hasValue() ? nullptr : &ink.error()}) {
            if (error != nullptr) {
                fail(*error);
                return;
            }
        }

        item.text = std::move(label.value());
        item.detail = std::move(secondary.value());
        item.icon = glyph.value();
        item.iconColor = ink.value();
        parsed.push_back(std::move(item));
    }

    m_items = std::move(parsed);
}

Component::Restore List::keep() {
    return kept(m_selected, m_style, m_items);
}

Result<void> List::validate() const {
    std::set<std::string, std::less<>> seen;

    for (const auto& item : m_items) {
        if (!seen.insert(item.id).second) {
            return Result<void>::failure({"ui_item_duplicate", "Two items share one identifier", item.id});
        }
    }

    if (!m_selected.empty() && !seen.contains(m_selected)) {
        return Result<void>::failure({"ui_item_unknown", "The selection names no item", m_selected});
    }

    return Result<void>::success();
}

ImVec2 List::measureContent(RenderContext& context, float availableWidth) {
    return {availableWidth, WidgetHelper::listRowHeight(context) * static_cast<float>(m_items.size())};
}

bool List::revealing() const {
    return !m_reveal.empty();
}

void List::render(RenderContext& context, const ImRect& bounds) {
    const float height = WidgetHelper::listRowHeight(context);

    for (std::size_t index = 0; index < m_items.size(); ++index) {
        const ListItem& item = m_items[index];
        const ImRect row(bounds.Min.x, bounds.Min.y + height * static_cast<float>(index), bounds.Max.x, bounds.Min.y + height * static_cast<float>(index + 1));

        // A revealed item is scrolled into the view that holds the list before rows out of sight are skipped.
        if (!m_reveal.empty() && item.id == m_reveal) {
            ImGui::ScrollToRect(ImGui::GetCurrentWindow(), row);
            m_reveal.clear();
        }

        if (!ImGui::IsRectVisible(row.Min, row.Max)) {
            continue;
        }

        const bool selected = item.id == m_selected;
        const RowContent content{context.text(item.text), context.text(item.detail), item.icon, item.iconColor};
        const ButtonState state = WidgetHelper::listRow(context, ImGui::GetID(item.id.data(), item.id.data() + item.id.size()), row, content, selected, m_style == ListStyle::Navigation ? RowStyle::Navigation : RowStyle::Default);

        if (state.pressed && !selected) {
            m_selected = item.id;
            context.emit(id(), "select", {{"id", item.id}}, {{"selected", "id"}});
        }

        if (state.doubleClicked()) {
            context.emit(id(), "activate", {{"id", item.id}});
        }
    }
}

} // namespace workpane::ui
