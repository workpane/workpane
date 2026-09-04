#include "ui/Theme.h"

#include "ui/Components.h"

#include <QApplication>

#include <algorithm>
#include <memory>
#include <vector>

namespace workpane::ui {

class ThemeHelper final {
  public:
    static int controlContentHeight(const Theme& theme);
    static QColor themeColor(ThemeColor role, const QColor& accent, const QColor& accentHover);
    static int themeMetric(ThemeMetric role);
    static QFont themeFont(ThemeFont role);
};

QColor ThemeHelper::themeColor(ThemeColor role, const QColor& accent, const QColor& accentHover) {
    switch (role) {
    case ThemeColor::Window:
        return QColor(31, 31, 31);
    case ThemeColor::Panel:
        return QColor(38, 38, 38);
    case ThemeColor::Raised:
        return QColor(45, 45, 45);
    case ThemeColor::Hover:
        return QColor(53, 53, 53);
    case ThemeColor::Pressed:
        return QColor(63, 63, 63);
    case ThemeColor::Border:
        return QColor(63, 63, 63);
    case ThemeColor::BorderStrong:
        return QColor(89, 89, 89);
    case ThemeColor::Text:
        return QColor(242, 242, 242);
    case ThemeColor::TextMuted:
        return QColor(174, 174, 174);
    case ThemeColor::Accent:
        return accent;
    case ThemeColor::AccentHover:
        return accentHover;
    case ThemeColor::AccentStrong:
        return accent.darker(140);
    case ThemeColor::OnAccent:
        return QColor(Qt::white);
    case ThemeColor::OnDanger:
        return QColor(Qt::white);
    case ThemeColor::Success:
        return accentHover;
    case ThemeColor::Warning:
        return QColor(221, 166, 70);
    case ThemeColor::Danger:
        return QColor(221, 91, 95);
    case ThemeColor::DangerBackground:
        return QColor(53, 35, 38);
    case ThemeColor::DangerText:
        return QColor(239, 138, 144);
    case ThemeColor::Information:
        return QColor(82, 148, 226);
    case ThemeColor::Terminal:
        return QColor(24, 24, 24);
    }

    return QColor(31, 31, 31);
}

int ThemeHelper::themeMetric(ThemeMetric role) {
    switch (role) {
    case ThemeMetric::ModeBarMinimumWidth:
        return 64;
    case ThemeMetric::ModeBarMaximumWidth:
        return 76;
    case ThemeMetric::ModeButtonMinimumHeight:
        return 58;
    case ThemeMetric::ModeButtonHorizontalPadding:
        return 6;
    case ThemeMetric::ModeButtonLabelTop:
        return 31;
    case ThemeMetric::ModeButtonBottomPadding:
        return 7;
    case ThemeMetric::WorkspaceBarHeight:
        return 34;
    case ThemeMetric::TerminalHeaderHeight:
        return 27;
    case ThemeMetric::PageHeaderHeight:
        return 40;
    case ThemeMetric::StatusBarHeight:
        return 22;
    case ThemeMetric::CompactButtonSize:
        return 26;
    case ThemeMetric::SmallIconSize:
        return 16;
    case ThemeMetric::ScrollBarExtent:
        return 10;
    case ThemeMetric::SplitterWidth:
        return 1;
    case ThemeMetric::TabHorizontalPadding:
        return 20;
    case ThemeMetric::TabVerticalPadding:
        return 7;
    case ThemeMetric::TerminalHorizontalPadding:
        return 7;
    case ThemeMetric::TerminalVerticalPadding:
        return 5;
    case ThemeMetric::TerminalMinimumColumns:
        return 18;
    case ThemeMetric::TerminalMinimumRows:
        return 3;
    case ThemeMetric::ControlRadius:
        return 2;
    case ThemeMetric::BadgeRadius:
        return 10;
    case ThemeMetric::ComboIndicatorWidth:
        return 24;
    case ThemeMetric::BadgeHorizontalPadding:
        return 8;
    case ThemeMetric::BadgeVerticalPadding:
        return 1;
    case ThemeMetric::ControlHorizontalPadding:
        return 10;
    case ThemeMetric::ControlVerticalPadding:
        return 5;
    case ThemeMetric::SettingsHorizontalPadding:
        return 24;
    case ThemeMetric::SettingsLabelMaximumWidth:
        return 320;
    case ThemeMetric::SettingsControlMinimumWidth:
        return 220;
    case ThemeMetric::SettingsControlMaximumWidth:
        return 420;
    case ThemeMetric::RoundButtonSize:
        return 36;
    }

    return 0;
}

QFont ThemeHelper::themeFont(ThemeFont role) {
    QFont font = QApplication::font();

    if (role == ThemeFont::Caption) {
        font.setPointSizeF(std::max(8.0, font.pointSizeF() - 2.0));
    }

    if (role == ThemeFont::Navigation) {
        font.setPointSizeF(std::max(8.0, font.pointSizeF() - 2.0));
        font.setWeight(QFont::DemiBold);
    }

    if (role == ThemeFont::PageTitle || role == ThemeFont::SectionTitle) {
        font.setPointSizeF(font.pointSizeF() + 1.0);
        font.setWeight(QFont::DemiBold);
    }

    if (role == ThemeFont::Monospace) {
        // A style sheet reads the family as a name, so the role resolves one instead of leaving a hint the sheet discards.
        if (const QString family = Components::defaultMonospacedFontFamily(); !family.isEmpty()) {
            font.setFamily(family);
        }

        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
    }

    return font;
}

QPalette Theme::palette() const {
    QPalette result;
    result.setColor(QPalette::Window, color(ThemeColor::Window));
    result.setColor(QPalette::WindowText, color(ThemeColor::Text));
    result.setColor(QPalette::Base, color(ThemeColor::Panel));
    result.setColor(QPalette::AlternateBase, color(ThemeColor::Raised));
    result.setColor(QPalette::ToolTipBase, color(ThemeColor::Raised));
    result.setColor(QPalette::ToolTipText, color(ThemeColor::Text));
    result.setColor(QPalette::Text, color(ThemeColor::Text));
    result.setColor(QPalette::Button, color(ThemeColor::Raised));
    result.setColor(QPalette::ButtonText, color(ThemeColor::Text));
    result.setColor(QPalette::BrightText, color(ThemeColor::OnAccent));
    result.setColor(QPalette::Highlight, color(ThemeColor::Accent));
    result.setColor(QPalette::HighlightedText, color(ThemeColor::OnAccent));
    result.setColor(QPalette::PlaceholderText, color(ThemeColor::TextMuted));
    result.setColor(QPalette::Disabled, QPalette::Text, color(ThemeColor::TextMuted).darker(125));
    result.setColor(QPalette::Disabled, QPalette::ButtonText, color(ThemeColor::TextMuted).darker(125));
    return result;
}

QString GreenTheme::id() const {
    return QStringLiteral("green");
}

QString GreenTheme::titleKey() const {
    return QStringLiteral("workpane.application.theme-green");
}

QColor GreenTheme::color(ThemeColor role) const {
    return ThemeHelper::themeColor(role, QColor(31, 155, 93), QColor(39, 191, 115));
}

int GreenTheme::metric(ThemeMetric role) const {
    return ThemeHelper::themeMetric(role);
}

QFont GreenTheme::font(ThemeFont role) const {
    return ThemeHelper::themeFont(role);
}

QString BlueTheme::id() const {
    return QStringLiteral("blue");
}

QString BlueTheme::titleKey() const {
    return QStringLiteral("workpane.application.theme-blue");
}

QColor BlueTheme::color(ThemeColor role) const {
    return ThemeHelper::themeColor(role, QColor(45, 116, 203), QColor(61, 139, 229));
}

int BlueTheme::metric(ThemeMetric role) const {
    return ThemeHelper::themeMetric(role);
}

QFont BlueTheme::font(ThemeFont role) const {
    return ThemeHelper::themeFont(role);
}

QString RedTheme::id() const {
    return QStringLiteral("red");
}

QString RedTheme::titleKey() const {
    return QStringLiteral("workpane.application.theme-red");
}

QColor RedTheme::color(ThemeColor role) const {
    return ThemeHelper::themeColor(role, QColor(190, 70, 75), QColor(218, 82, 88));
}

int RedTheme::metric(ThemeMetric role) const {
    return ThemeHelper::themeMetric(role);
}

QFont RedTheme::font(ThemeFont role) const {
    return ThemeHelper::themeFont(role);
}

// The room a control needs for its text is what the running style answers for a selectable field, because a button beside one must not be shorter.
int ThemeHelper::controlContentHeight(const Theme& theme) {
    const QFontMetrics metrics(theme.font(ThemeFont::Interface));
    QStyleOptionComboBox option;
    option.fontMetrics = metrics;
    const QSize content(0, metrics.height());
    return QApplication::style()->sizeFromContents(QStyle::CT_ComboBox, &option, content).height();
}

QString ThemeTokens::substituted(QString styleSheet, const Theme& theme) {
    // Longer tokens are replaced first so a shorter token never consumes their prefix.
    const QVector<QPair<QString, QString>> tokens{
        {QStringLiteral("@borderStrong"), theme.color(ThemeColor::BorderStrong).name()}, {QStringLiteral("@border"), theme.color(ThemeColor::Border).name()}, {QStringLiteral("@dangerBackground"), theme.color(ThemeColor::DangerBackground).name()}, {QStringLiteral("@dangerText"), theme.color(ThemeColor::DangerText).name()}, {QStringLiteral("@dangerHover"), theme.color(ThemeColor::Danger).lighter(112).name()}, {QStringLiteral("@danger"), theme.color(ThemeColor::Danger).name()}, {QStringLiteral("@accentStrong"), theme.color(ThemeColor::AccentStrong).name()}, {QStringLiteral("@accentHover"), theme.color(ThemeColor::AccentHover).name()}, {QStringLiteral("@accent"), theme.color(ThemeColor::Accent).name()}, {QStringLiteral("@onAccent"), theme.color(ThemeColor::OnAccent).name()}, {QStringLiteral("@onDanger"), theme.color(ThemeColor::OnDanger).name()}, {QStringLiteral("@textMuted"), theme.color(ThemeColor::TextMuted).name()}, {QStringLiteral("@text"), theme.color(ThemeColor::Text).name()}, {QStringLiteral("@window"), theme.color(ThemeColor::Window).name()}, {QStringLiteral("@panel"), theme.color(ThemeColor::Panel).name()}, {QStringLiteral("@raised"), theme.color(ThemeColor::Raised).name()}, {QStringLiteral("@hover"), theme.color(ThemeColor::Hover).name()}, {QStringLiteral("@success"), theme.color(ThemeColor::Success).name()}, {QStringLiteral("@warning"), theme.color(ThemeColor::Warning).name()}, {QStringLiteral("@information"), theme.color(ThemeColor::Information).name()}, {QStringLiteral("@terminal"), theme.color(ThemeColor::Terminal).name()}, {QStringLiteral("@interfaceFontSize"), QString::number(theme.font(ThemeFont::Interface).pointSize())}, {QStringLiteral("@pageTitleFontSize"), QString::number(theme.font(ThemeFont::PageTitle).pointSize())}, {QStringLiteral("@sectionTitleFontSize"), QString::number(theme.font(ThemeFont::SectionTitle).pointSize())}, {QStringLiteral("@captionFontSize"), QString::number(theme.font(ThemeFont::Caption).pointSize())}, {QStringLiteral("@monospaceFamily"), theme.font(ThemeFont::Monospace).family()}, {QStringLiteral("@roundButtonRadius"), QString::number(theme.metric(ThemeMetric::RoundButtonSize) / 2)}, {QStringLiteral("@roundButtonSize"), QString::number(theme.metric(ThemeMetric::RoundButtonSize))}, {QStringLiteral("@controlHorizontalPadding"), QString::number(theme.metric(ThemeMetric::ControlHorizontalPadding))}, {QStringLiteral("@controlVerticalPadding"), QString::number(theme.metric(ThemeMetric::ControlVerticalPadding))}, {QStringLiteral("@controlTextHeight"), QString::number(ThemeHelper::controlContentHeight(theme))}, {QStringLiteral("@controlRadius"), QString::number(theme.metric(ThemeMetric::ControlRadius))}, {QStringLiteral("@comboIndicatorWidth"), QString::number(theme.metric(ThemeMetric::ComboIndicatorWidth))}, {QStringLiteral("@badgeRadius"), QString::number(theme.metric(ThemeMetric::BadgeRadius))}, {QStringLiteral("@badgeHorizontalPadding"), QString::number(theme.metric(ThemeMetric::BadgeHorizontalPadding))}, {QStringLiteral("@badgeVerticalPadding"), QString::number(theme.metric(ThemeMetric::BadgeVerticalPadding))}, {QStringLiteral("@scrollBarExtent"), QString::number(theme.metric(ThemeMetric::ScrollBarExtent))},
    };

    for (const auto& token : tokens) {
        styleSheet.replace(token.first, token.second);
    }

    return styleSheet;
}

ThemeCatalog::ThemeCatalog() {
    m_themes.push_back(std::make_unique<GreenTheme>());
    m_themes.push_back(std::make_unique<BlueTheme>());
    m_themes.push_back(std::make_unique<RedTheme>());
}

const std::vector<std::unique_ptr<Theme>>& ThemeCatalog::themes() const {
    return m_themes;
}

const Theme* ThemeCatalog::find(const QString& themeId) const {
    for (const auto& theme : m_themes) {
        if (theme->id() == themeId) {
            return theme.get();
        }
    }

    return nullptr;
}

const Theme& ThemeCatalog::themeOrDefault(const QString& themeId) const {
    const Theme* selected = find(themeId);
    return selected == nullptr ? defaultTheme() : *selected;
}

const Theme& ThemeCatalog::defaultTheme() const {
    return *m_themes.front();
}

bool ThemeCatalog::contains(const QString& themeId) const {
    return find(themeId) != nullptr;
}

ThemeManager::ThemeManager() : m_theme(&m_catalog.defaultTheme()) {}

const ThemeCatalog& ThemeManager::catalog() const {
    return m_catalog;
}

const Theme& ThemeManager::theme() const {
    return *m_theme;
}

void ThemeManager::loadTheme(const QString& storedThemeId) {
    m_theme = &m_catalog.themeOrDefault(storedThemeId);
}

Result<void> ThemeManager::selectTheme(const QString& themeId) {
    const Theme* selected = m_catalog.find(themeId);

    if (selected == nullptr) {
        return Result<void>::failure({"application_theme_invalid", "The application theme is unsupported", themeId});
    }

    m_theme = selected;
    return Result<void>::success();
}

ThemeManager& ThemeManager::instance() {
    static ThemeManager manager;
    return manager;
}

} // namespace workpane::ui
