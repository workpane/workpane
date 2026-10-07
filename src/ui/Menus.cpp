#include "ui/Menus.h"

#include "ui/ButtonState.h"
#include "ui/Color.h"
#include "ui/IconCatalog.h"
#include "ui/Painter.h"
#include "ui/WidgetHelper.h"
#include "ui/model/RenderContext.h"
#include "ui/model/TextValue.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_set>
#include <utility>

namespace workpane::ui {

Result<std::vector<MenuItem>> Menus::parse(const json::Json& items, std::string_view context) {
    std::vector<MenuItem> parsed;

    if (!json::ObjectReader::isList(items)) {
        return Result<std::vector<MenuItem>>::failure({"ui_menu_invalid", "Menu items must be a list", std::string(context)});
    }

    std::unordered_set<std::string> identities;

    for (const auto& entry : items) {
        MenuItem item;
        const json::Json* text = &json::ObjectReader::absent();
        json::ObjectReader reader(entry, std::string(context) + ".items");
        reader.read("separator", item.separator, json::Presence::Optional);

        if (item.separator) {
            if (const auto finished = reader.finish(); !finished.hasValue()) {
                return Result<std::vector<MenuItem>>::failure(finished.error());
            }

            parsed.push_back(std::move(item));
            continue;
        }

        std::string icon;
        reader.readText("id", item.id).readAny("text", text).read("icon", icon, json::Presence::Optional).read("shortcut", item.shortcut, json::Presence::Optional).read("enabled", item.enabled, json::Presence::Optional).read("destructive", item.destructive, json::Presence::Optional);

        if (const auto finished = reader.finish(); !finished.hasValue()) {
            return Result<std::vector<MenuItem>>::failure(finished.error());
        }

        if (!identities.insert(item.id).second) {
            return Result<std::vector<MenuItem>>::failure({"ui_item_duplicate", "Every item of a menu has an identity of its own", std::string(context) + ".items"});
        }

        auto label = TextValue::parse(*text, std::string(context) + ".items.text");

        if (!label.hasValue()) {
            return Result<std::vector<MenuItem>>::failure(label.error());
        }

        item.text = std::move(label.value());

        if (!icon.empty()) {
            item.icon = IconCatalog::parse(icon);

            if (!item.icon.has_value()) {
                return Result<std::vector<MenuItem>>::failure({"ui_icon_unknown", "An icon names nothing in the painted set", icon});
            }
        }

        parsed.push_back(std::move(item));
    }

    return Result<std::vector<MenuItem>>::success(std::move(parsed));
}

std::optional<std::string> Menus::drawItems(RenderContext& context, const std::vector<MenuItem>& items) {
    const float scale = context.scale();
    const FontRole font = context.font(ThemeFont::Interface);
    const float padding = context.metric(ThemeMetric::ControlHorizontalPadding);
    const float icon = context.metric(ThemeMetric::SmallIconSize);
    const float height = WidgetHelper::controlHeight(context) - 2.0F * scale;
    // clang-format off
    const bool anyIcon = std::ranges::any_of(items, [](const MenuItem& item) { return item.icon.has_value(); });
    // clang-format on

    // Every row takes the width of the widest label and shortcut, and an icon column exists when any item has an icon.
    float width = menuMinimumWidth * scale;

    for (const auto& item : items) {
        const float shortcut = item.shortcut.empty() ? 0.0F : WidgetHelper::textSize(context, font, item.shortcut).x + menuShortcutSpacing * scale;
        width = std::max(width, padding * 2.0F + (anyIcon ? icon + padding : 0.0F) + WidgetHelper::textSize(context, font, context.text(item.text)).x + shortcut);
    }

    std::optional<std::string> picked;
    ImDrawList& list = *ImGui::GetWindowDrawList();

    for (std::size_t index = 0; index < items.size(); ++index) {
        const MenuItem& item = items[index];
        const ImVec2 origin = ImGui::GetCursorScreenPos();

        if (item.separator) {
            const float margin = menuSeparatorMargin * scale;
            Painter::horizontalDivider(list, ImVec2(origin.x, origin.y + margin), width, context.metric(ThemeMetric::LineWidth), context.color(ThemeColor::Border));
            ImGui::Dummy(ImVec2(width, margin * 2.0F + context.metric(ThemeMetric::LineWidth)));
            continue;
        }

        const ImRect row(origin, ImVec2(origin.x + width, origin.y + height));

        if (!item.enabled) {
            ImGui::BeginDisabled();
        }

        const ButtonState state = WidgetHelper::interact(ImGui::GetID(static_cast<int>(index)), row);

        if (!item.enabled) {
            ImGui::EndDisabled();
        }

        const Color ink = context.color(!item.enabled ? ThemeColor::TextMuted : item.destructive ? ThemeColor::DangerText : ThemeColor::Text);

        if (state.hovered && item.enabled) {
            list.AddRectFilled(row.Min, row.Max, WidgetHelper::ink(context.color(ThemeColor::Hover)), context.metric(ThemeMetric::ControlRadius));
        }

        float x = row.Min.x + padding;

        if (item.icon.has_value()) {
            IconCatalog::draw(context.fonts(), list, *item.icon, ImFloor(ImVec2(x, row.GetCenter().y - icon / 2.0F)), icon, ink);
        }

        if (anyIcon) {
            x += icon + padding;
        }

        WidgetHelper::alignedText(context, list, font, ImRect(x, row.Min.y, row.Max.x - padding, row.Max.y), ink, context.text(item.text), TextAlign::Start);

        if (!item.shortcut.empty()) {
            WidgetHelper::alignedText(context, list, font, ImRect(row.Min.x, row.Min.y, row.Max.x - padding, row.Max.y), context.color(ThemeColor::TextMuted), item.shortcut, TextAlign::End);
        }

        if (state.pressed && item.enabled) {
            picked = item.id;
        }
    }

    return picked;
}

std::optional<std::string> Menus::popup(RenderContext& context, const char* id, const std::vector<MenuItem>& items) {
    const float inset = menuInset * context.scale();
    std::optional<std::string> picked;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(inset, inset));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0F, 0.0F));

    if (ImGui::BeginPopup(id, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize)) {
        picked = drawItems(context, items);

        if (picked.has_value()) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);

    return picked;
}

} // namespace workpane::ui
