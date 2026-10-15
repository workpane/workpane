#include "ui/shell/ModeBar.h"

#include "ui/ButtonState.h"
#include "ui/IconCatalog.h"
#include "ui/Painter.h"
#include "ui/WidgetHelper.h"
#include "ui/model/RenderContext.h"
#include "ui/shell/NavigationItem.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string_view>

namespace workpane::ui {

FontRole ModeBar::labelFont(RenderContext& context, bool selected) {
    FontRole font = context.font(ThemeFont::Navigation);
    font.face = selected ? FontFace::SemiBold : FontFace::Regular;

    return font;
}

float ModeBar::longestWord(RenderContext& context, const std::string& title) {
    float longest = 0.0F;
    std::size_t start = 0;

    while (start <= title.size()) {
        const std::size_t end = std::min(title.find(' ', start), title.size());
        longest = std::max(longest, WidgetHelper::textSize(context, labelFont(context, true), std::string_view(title).substr(start, end - start)).x);
        start = end + 1;
    }

    return longest;
}

// The bar never grows past its maximum width, because a language with long words must not widen the shell.
float ModeBar::width(RenderContext& context, const std::vector<ModeEntry>& entries) const {
    float required = context.metric(ThemeMetric::ModeBarMinimumWidth);

    for (const auto& entry : entries) {
        required = std::max(required, longestWord(context, entry.title) + context.metric(ThemeMetric::ModeButtonHorizontalPadding) * 2.0F);
    }

    return std::ceil(std::min(required, context.metric(ThemeMetric::ModeBarMaximumWidth)));
}

std::optional<std::string> ModeBar::draw(RenderContext& context, const ImRect& bounds, const std::vector<ModeEntry>& entries, const std::string& current) {
    ImDrawList& list = *ImGui::GetWindowDrawList();
    list.AddRectFilled(bounds.Min, bounds.Max, WidgetHelper::ink(context.color(ThemeColor::Window)));

    std::optional<std::string> picked;
    float top = bounds.Min.y;
    float bottom = bounds.Max.y;

    for (const auto& entry : entries) {
        if (entry.placement != NavigationPlacement::Primary) {
            continue;
        }

        const float height = buttonHeight(context, entry, bounds.GetWidth());

        if (drawButton(context, ImRect(bounds.Min.x, top, bounds.Max.x, top + height), entry, entry.destination == current)) {
            picked = entry.destination;
        }

        top += height;
    }

    // Secondary destinations stack upward from the bottom edge in their declared order, so the last declared one sits lowest.
    for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry) {
        if (entry->placement != NavigationPlacement::Secondary) {
            continue;
        }

        const float height = buttonHeight(context, *entry, bounds.GetWidth());

        if (drawButton(context, ImRect(bounds.Min.x, bottom - height, bounds.Max.x, bottom), *entry, entry->destination == current)) {
            picked = entry->destination;
        }

        bottom -= height;
    }

    return picked;
}

float ModeBar::buttonHeight(RenderContext& context, const ModeEntry& entry, float width) const {
    const float padding = context.metric(ThemeMetric::ModeButtonHorizontalPadding);
    const float label = WidgetHelper::paragraphSize(context, labelFont(context, true), entry.title, width - padding * 2.0F).y;
    return std::max(context.metric(ThemeMetric::ModeButtonMinimumHeight), std::ceil(context.metric(ThemeMetric::ModeButtonLabelTop) + label + context.metric(ThemeMetric::ModeButtonBottomPadding)));
}

bool ModeBar::drawButton(RenderContext& context, const ImRect& bounds, const ModeEntry& entry, bool selected) {
    ImDrawList& list = *ImGui::GetWindowDrawList();

    // The identity of a destination lives in the scope of the bar, so a settings category of the same owner never shares it.
    ImGui::PushID(identityScope);
    const ImGuiID identity = ImGui::GetID(entry.destination.data(), entry.destination.data() + entry.destination.size());
    ImGui::PopID();
    const ButtonState state = WidgetHelper::interact(identity, bounds, ImGuiButtonFlags_PressedOnClick);

    // The hover fades in faster than it fades out, which reads as responsive without flickering while the pointer crosses the bar.
    float& fade = m_fades[entry.destination];
    const float target = state.hovered ? 1.0F : 0.0F;
    const float step = ImGui::GetIO().DeltaTime / (target > fade ? fadeInSeconds : fadeOutSeconds);
    fade = target > fade ? std::min(target, fade + step) : std::max(target, fade - step);

    if (fade != target) {
        context.requestFrame();
    }

    if (state.hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    if (selected) {
        list.AddRectFilled(bounds.Min, bounds.Max, WidgetHelper::ink(context.color(ThemeColor::Raised)));
        list.AddRectFilled(bounds.Min, ImVec2(bounds.Min.x + selectionMarker * context.scale(), bounds.Max.y), WidgetHelper::ink(context.color(ThemeColor::Accent)));
    } else if (fade > 0.0F) {
        list.AddRectFilled(bounds.Min, bounds.Max, WidgetHelper::ink(context.color(ThemeColor::Hover).withAlpha(fade * hoverOpacity)));
    }

    const float icon = context.metric(ThemeMetric::ModeButtonIconSize);
    IconCatalog::draw(context.fonts(), list, entry.icon, ImFloor(ImVec2(bounds.GetCenter().x - icon / 2.0F, bounds.Min.y + context.metric(ThemeMetric::ModeButtonIconTop))), icon, context.color(selected ? ThemeColor::Text : ThemeColor::TextMuted));

    const float padding = context.metric(ThemeMetric::ModeButtonHorizontalPadding);
    const float labelTop = bounds.Min.y + context.metric(ThemeMetric::ModeButtonLabelTop);
    WidgetHelper::paragraph(context, list, labelFont(context, selected), ImRect(bounds.Min.x + padding, labelTop, bounds.Max.x - padding, bounds.Max.y), context.color(selected ? ThemeColor::Text : ThemeColor::TextMuted), entry.title, TextAlign::Center);

    WidgetHelper::itemTooltip(context, entry.title);

    return state.pressed;
}

} // namespace workpane::ui
