#pragma once

#include "ui/Color.h"
#include "ui/theme/FontRole.h"

#include <string_view>

namespace workpane::ui {

// The palette is grouped in families: surfaces, states, borders, scroll bars, text, the accent and the four tones.
// A tone has its fill, the ink written over that fill, a subtle background and a text color readable on the window and on that background.
enum class ThemeColor { Window, Panel, Raised, Terminal, Tooltip, Overlay, Hover, Pressed, Selection, Highlight, Focus, Border, BorderStrong, Scrollbar, ScrollbarHover, ScrollbarActive, Text, TextMuted, TextDisabled, OnTooltip, OnTooltipMuted, Accent, AccentHover, AccentStrong, OnAccent, AccentBackground, AccentText, Success, OnSuccess, SuccessBackground, SuccessText, Warning, OnWarning, WarningBackground, WarningText, Danger, DangerHover, DangerStrong, OnDanger, DangerBackground, DangerText, Information, OnInformation, InformationBackground, InformationText };

enum class ThemeMetric { ModeBarMinimumWidth, ModeBarMaximumWidth, ModeButtonMinimumHeight, ModeButtonHorizontalPadding, ModeButtonIconTop, ModeButtonIconSize, ModeButtonLabelTop, ModeButtonBottomPadding, WorkspaceBarHeight, PageHeaderHeight, CompactButtonSize, SmallIconSize, ScrollBarExtent, SplitterWidth, TabHorizontalPadding, TabIndicatorSize, TabIndicatorSpacing, TabCloseSize, TabMinimumWidth, TabMaximumWidth, ControlRadius, BadgeRadius, ComboIndicatorWidth, BadgeHorizontalPadding, BadgeVerticalPadding, ControlHorizontalPadding, ControlVerticalPadding, ControlHeight, ItemSpacing, SettingsHorizontalPadding, SettingsVerticalPadding, SettingsSectionSpacing, SettingsCategoryWidth, SettingsLabelMaximumWidth, SettingsControlMinimumWidth, SettingsControlMaximumWidth, ToastWidth, ToastSpacing, ToastMargin, DialogMinimumWidth, CaretWidth, LineWidth };

enum class ThemeFont { Interface, Navigation, Caption, PageTitle, SectionTitle, Heading, Monospace };

// Every concrete theme answers every semantic color, layout metric and font role, so no surface ever reads a literal.
class Theme {
  public:
    virtual ~Theme() = default;

    [[nodiscard]] virtual std::string_view id() const = 0;
    [[nodiscard]] virtual std::string_view titleKey() const = 0;
    [[nodiscard]] virtual Color color(ThemeColor role) const = 0;
    [[nodiscard]] virtual float metric(ThemeMetric role) const = 0;
    [[nodiscard]] virtual FontRole font(ThemeFont role) const = 0;
};

} // namespace workpane::ui
