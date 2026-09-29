#include "ui/theme/AccentTheme.h"

namespace workpane::ui {

// Every role of the palette is defined here, where a fill darkens under the pointer and further while pressed so its ink reads at least as well as at rest, so no surface derives a color.
Color AccentTheme::color(ThemeColor role) const {
    const Color white = Color::rgb(255, 255, 255);
    const Color ink = Color::rgb(24, 24, 24);

    switch (role) {
    case ThemeColor::Window:
        return Color::rgb(31, 31, 31);
    case ThemeColor::Panel:
        return Color::rgb(38, 38, 38);
    case ThemeColor::Raised:
        return Color::rgb(45, 45, 45);
    case ThemeColor::Terminal:
        return Color::rgb(24, 24, 24);
    case ThemeColor::Tooltip:
        return Color::rgb(12, 12, 12);
    case ThemeColor::Overlay:
        return Color::rgb(0, 0, 0).withAlpha(overlayOpacity);
    case ThemeColor::Hover:
        return Color::rgb(53, 53, 53);
    case ThemeColor::Pressed:
        return Color::rgb(63, 63, 63);
    case ThemeColor::Selection:
        return accent().withAlpha(selectionOpacity);
    case ThemeColor::Highlight:
        return color(ThemeColor::Warning).withAlpha(highlightOpacity);
    case ThemeColor::Focus:
        return accent();
    case ThemeColor::Border:
        return Color::rgb(63, 63, 63);
    case ThemeColor::BorderStrong:
    case ThemeColor::Scrollbar:
        return Color::rgb(89, 89, 89);
    case ThemeColor::ScrollbarHover:
        return color(ThemeColor::Scrollbar).lighter(120);
    case ThemeColor::ScrollbarActive:
        return color(ThemeColor::Scrollbar).lighter(140);
    case ThemeColor::Text:
        return Color::rgb(242, 242, 242);
    case ThemeColor::TextMuted:
    case ThemeColor::OnTooltipMuted:
        return Color::rgb(174, 174, 174);
    case ThemeColor::TextDisabled:
        return color(ThemeColor::TextMuted).darker(125);
    case ThemeColor::OnTooltip:
    case ThemeColor::OnAccent:
    case ThemeColor::OnDanger:
        return white;
    case ThemeColor::Accent:
        return accent();
    case ThemeColor::AccentHover:
        return accent().darker(hoverDarkening);
    case ThemeColor::Success:
        return success();
    case ThemeColor::AccentStrong:
        return accent().darker(pressedDarkening);
    case ThemeColor::Warning:
        return Color::rgb(221, 166, 70);
    case ThemeColor::Danger:
        return Color::rgb(221, 91, 95);
    case ThemeColor::DangerHover:
        return color(ThemeColor::Danger).darker(hoverDarkening);
    case ThemeColor::DangerStrong:
        return color(ThemeColor::Danger).darker(pressedDarkening);
    case ThemeColor::DangerBackground:
        return Color::rgb(53, 35, 38);
    case ThemeColor::DangerText:
        return Color::rgb(239, 138, 144);
    case ThemeColor::Information:
        return Color::rgb(82, 148, 226);
    case ThemeColor::OnSuccess:
    case ThemeColor::OnWarning:
    case ThemeColor::OnInformation:
        return ink;
    case ThemeColor::AccentBackground:
        return background(ThemeColor::Accent);
    case ThemeColor::SuccessBackground:
        return background(ThemeColor::Success);
    case ThemeColor::WarningBackground:
        return background(ThemeColor::Warning);
    case ThemeColor::InformationBackground:
        return background(ThemeColor::Information);
    case ThemeColor::AccentText:
        return readable(ThemeColor::Accent);
    case ThemeColor::SuccessText:
        return readable(ThemeColor::Success);
    case ThemeColor::WarningText:
        return readable(ThemeColor::Warning);
    case ThemeColor::InformationText:
        return readable(ThemeColor::Information);
    }

    return Color::rgb(31, 31, 31);
}

// A subtle background is the window mixed with a little of its tone.
Color AccentTheme::background(ThemeColor tone) const {
    return Color::blend(color(ThemeColor::Window), color(tone), backgroundMix);
}

// A text color is its tone mixed with white, so it stays readable on the window and on the background of the tone.
Color AccentTheme::readable(ThemeColor tone) const {
    return Color::blend(color(tone), Color::rgb(255, 255, 255), textMix);
}

float AccentTheme::metric(ThemeMetric role) const {
    switch (role) {
    case ThemeMetric::ModeBarMinimumWidth:
        return 64.0F;
    case ThemeMetric::ModeBarMaximumWidth:
        return 76.0F;
    case ThemeMetric::ModeButtonMinimumHeight:
        return 58.0F;
    case ThemeMetric::ModeButtonHorizontalPadding:
        return 6.0F;
    case ThemeMetric::ModeButtonIconTop:
        return 8.0F;
    case ThemeMetric::ModeButtonIconSize:
        return 19.0F;
    case ThemeMetric::ModeButtonLabelTop:
        return 31.0F;
    case ThemeMetric::ModeButtonBottomPadding:
        return 7.0F;
    case ThemeMetric::WorkspaceBarHeight:
        return 34.0F;
    case ThemeMetric::PageHeaderHeight:
        return 40.0F;
    case ThemeMetric::CompactButtonSize:
        return 26.0F;
    case ThemeMetric::SmallIconSize:
        return 16.0F;
    case ThemeMetric::ScrollBarExtent:
        return 10.0F;
    case ThemeMetric::SplitterWidth:
        return 1.0F;
    case ThemeMetric::TabHorizontalPadding:
        return 12.0F;
    case ThemeMetric::TabIndicatorSize:
        return 7.0F;
    case ThemeMetric::TabIndicatorSpacing:
        return 8.0F;
    case ThemeMetric::TabCloseSize:
        return 15.0F;
    case ThemeMetric::TabMinimumWidth:
        return 118.0F;
    case ThemeMetric::TabMaximumWidth:
        return 260.0F;
    case ThemeMetric::ControlRadius:
        return 2.0F;
    case ThemeMetric::BadgeRadius:
        return 10.0F;
    case ThemeMetric::ComboIndicatorWidth:
        return 24.0F;
    case ThemeMetric::BadgeHorizontalPadding:
        return 8.0F;
    case ThemeMetric::BadgeVerticalPadding:
        return 1.0F;
    case ThemeMetric::ControlHorizontalPadding:
        return 10.0F;
    case ThemeMetric::ControlVerticalPadding:
        return 5.0F;
    case ThemeMetric::ControlHeight:
        return 28.0F;
    case ThemeMetric::ItemSpacing:
        return 8.0F;
    case ThemeMetric::SettingsHorizontalPadding:
        return 24.0F;
    case ThemeMetric::SettingsVerticalPadding:
        return 22.0F;
    case ThemeMetric::SettingsSectionSpacing:
        return 22.0F;
    case ThemeMetric::SettingsCategoryWidth:
        return 210.0F;
    case ThemeMetric::SettingsLabelMaximumWidth:
        return 320.0F;
    case ThemeMetric::SettingsControlMinimumWidth:
        return 220.0F;
    case ThemeMetric::SettingsControlMaximumWidth:
        return 420.0F;
    case ThemeMetric::ToastWidth:
        return 380.0F;
    case ThemeMetric::ToastSpacing:
        return 10.0F;
    case ThemeMetric::ToastMargin:
        return 18.0F;
    case ThemeMetric::DialogMinimumWidth:
        return 440.0F;
    case ThemeMetric::CaretWidth:
        return 2.0F;
    case ThemeMetric::LineWidth:
        return 1.0F;
    }

    return 0.0F;
}

// Inter runs about a tenth wider than the system face of the original, so each role is one point smaller to keep the same text widths.
FontRole AccentTheme::font(ThemeFont role) const {
    switch (role) {
    case ThemeFont::Interface:
        return {FontFace::Regular, 12.0F};
    case ThemeFont::Navigation:
        return {FontFace::Regular, 10.0F};
    case ThemeFont::Caption:
        return {FontFace::Regular, 10.0F};
    case ThemeFont::PageTitle:
        return {FontFace::SemiBold, 13.0F};
    case ThemeFont::SectionTitle:
        return {FontFace::SemiBold, 13.0F};
    case ThemeFont::Heading:
        return {FontFace::SemiBold, 20.0F};
    case ThemeFont::Monospace:
        return {FontFace::Monospace, 12.0F};
    }

    return {FontFace::Regular, 12.0F};
}

} // namespace workpane::ui
