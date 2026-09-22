#pragma once

#include "domain/Result.h"

#include <QColor>
#include <QFont>
#include <QPalette>
#include <QString>

#include <memory>
#include <vector>

namespace workpane::ui {

enum class ThemeColor { Window, Panel, Raised, Hover, Pressed, Border, BorderStrong, Text, TextMuted, Accent, AccentHover, AccentStrong, OnAccent, OnDanger, Success, Warning, Danger, DangerBackground, DangerText, Information, Terminal };

enum class ThemeMetric { ModeBarMinimumWidth, ModeBarMaximumWidth, ModeButtonMinimumHeight, ModeButtonHorizontalPadding, ModeButtonLabelTop, ModeButtonBottomPadding, WorkspaceBarHeight, TerminalHeaderHeight, PageHeaderHeight, StatusBarHeight, CompactButtonSize, SmallIconSize, ScrollBarExtent, SplitterWidth, TabHorizontalPadding, TabVerticalPadding, TerminalHorizontalPadding, TerminalVerticalPadding, TerminalMinimumColumns, TerminalMinimumRows, ControlRadius, BadgeRadius, ComboIndicatorWidth, BadgeHorizontalPadding, BadgeVerticalPadding, ControlHorizontalPadding, ControlVerticalPadding, SettingsHorizontalPadding, SettingsLabelMaximumWidth, SettingsControlMinimumWidth, SettingsControlMaximumWidth, RoundButtonSize };

enum class ThemeFont { Interface, Navigation, Caption, PageTitle, SectionTitle, Monospace };

class Theme {
  public:
    virtual ~Theme() = default;

    [[nodiscard]] virtual QString id() const = 0;
    [[nodiscard]] virtual QString titleKey() const = 0;
    [[nodiscard]] virtual QColor color(ThemeColor role) const = 0;
    [[nodiscard]] virtual int metric(ThemeMetric role) const = 0;
    [[nodiscard]] virtual QFont font(ThemeFont role) const = 0;
    [[nodiscard]] QPalette palette() const;
};

class GreenTheme final : public Theme {
  public:
    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString titleKey() const override;
    [[nodiscard]] QColor color(ThemeColor role) const override;
    [[nodiscard]] int metric(ThemeMetric role) const override;
    [[nodiscard]] QFont font(ThemeFont role) const override;
};

class BlueTheme final : public Theme {
  public:
    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString titleKey() const override;
    [[nodiscard]] QColor color(ThemeColor role) const override;
    [[nodiscard]] int metric(ThemeMetric role) const override;
    [[nodiscard]] QFont font(ThemeFont role) const override;
};

class RedTheme final : public Theme {
  public:
    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString titleKey() const override;
    [[nodiscard]] QColor color(ThemeColor role) const override;
    [[nodiscard]] int metric(ThemeMetric role) const override;
    [[nodiscard]] QFont font(ThemeFont role) const override;
};

class ThemeCatalog final {
  public:
    ThemeCatalog();

    [[nodiscard]] const std::vector<std::unique_ptr<Theme>>& themes() const;
    [[nodiscard]] const Theme* find(const QString& themeId) const;
    [[nodiscard]] const Theme& themeOrDefault(const QString& themeId) const;
    [[nodiscard]] const Theme& defaultTheme() const;
    [[nodiscard]] bool contains(const QString& themeId) const;

  private:
    std::vector<std::unique_ptr<Theme>> m_themes;
};

class ThemeManager final {
  public:
    ThemeManager();

    // The running manager is answered by the class it belongs to, so no surface builds a second one.
    [[nodiscard]] static ThemeManager& instance();

    [[nodiscard]] const ThemeCatalog& catalog() const;
    [[nodiscard]] const Theme& theme() const;
    void loadTheme(const QString& storedThemeId);
    [[nodiscard]] Result<void> selectTheme(const QString& themeId);

  private:
    ThemeCatalog m_catalog;
    const Theme* m_theme{nullptr};
};

// A style sheet resolves theme values through named tokens so every surface reads the same source.
class ThemeTokens final {
  public:
    [[nodiscard]] static QString substituted(QString styleSheet, const Theme& theme);
};

} // namespace workpane::ui
