#include "ui/theme/ThemeColorNames.h"

namespace workpane::ui {

const std::vector<std::pair<ThemeColor, std::string_view>>& ThemeColorNames::all() {
    static const std::vector<std::pair<ThemeColor, std::string_view>> names{{ThemeColor::Window, "window"}, {ThemeColor::Panel, "panel"}, {ThemeColor::Raised, "raised"}, {ThemeColor::Terminal, "terminal"}, {ThemeColor::Tooltip, "tooltip"}, {ThemeColor::Overlay, "overlay"}, {ThemeColor::Hover, "hover"}, {ThemeColor::Pressed, "pressed"}, {ThemeColor::Selection, "selection"}, {ThemeColor::Highlight, "highlight"}, {ThemeColor::Focus, "focus"}, {ThemeColor::Border, "border"}, {ThemeColor::BorderStrong, "border-strong"}, {ThemeColor::Scrollbar, "scrollbar"}, {ThemeColor::ScrollbarHover, "scrollbar-hover"}, {ThemeColor::ScrollbarActive, "scrollbar-active"}, {ThemeColor::Text, "text"}, {ThemeColor::TextMuted, "text-muted"}, {ThemeColor::TextDisabled, "text-disabled"}, {ThemeColor::OnTooltip, "on-tooltip"}, {ThemeColor::OnTooltipMuted, "on-tooltip-muted"}, {ThemeColor::Accent, "accent"}, {ThemeColor::AccentHover, "accent-hover"}, {ThemeColor::AccentStrong, "accent-strong"}, {ThemeColor::OnAccent, "on-accent"}, {ThemeColor::AccentBackground, "accent-background"}, {ThemeColor::AccentText, "accent-text"}, {ThemeColor::Success, "success"}, {ThemeColor::OnSuccess, "on-success"}, {ThemeColor::SuccessBackground, "success-background"}, {ThemeColor::SuccessText, "success-text"}, {ThemeColor::Warning, "warning"}, {ThemeColor::OnWarning, "on-warning"}, {ThemeColor::WarningBackground, "warning-background"}, {ThemeColor::WarningText, "warning-text"}, {ThemeColor::Danger, "danger"}, {ThemeColor::DangerHover, "danger-hover"}, {ThemeColor::DangerStrong, "danger-strong"}, {ThemeColor::OnDanger, "on-danger"}, {ThemeColor::DangerBackground, "danger-background"}, {ThemeColor::DangerText, "danger-text"}, {ThemeColor::Information, "information"}, {ThemeColor::OnInformation, "on-information"}, {ThemeColor::InformationBackground, "information-background"}, {ThemeColor::InformationText, "information-text"}};
    return names;
}

std::optional<ThemeColor> ThemeColorNames::parse(std::string_view name) {
    for (const auto& [role, text] : all()) {
        if (text == name) {
            return role;
        }
    }

    return std::nullopt;
}

// An empty name declares no color, and any other name must be one of the theme roles.
Result<std::optional<ThemeColor>> ThemeColorNames::parseOptional(const std::string& name) {
    if (name.empty()) {
        return Result<std::optional<ThemeColor>>::success(std::nullopt);
    }

    const auto parsed = parse(name);

    if (!parsed.has_value()) {
        return Result<std::optional<ThemeColor>>::failure({"ui_color_unknown", "A color names no theme role", name});
    }

    return Result<std::optional<ThemeColor>>::success(parsed);
}

} // namespace workpane::ui
