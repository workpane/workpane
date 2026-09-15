#include "terminal/TerminalThemeCatalog.h"

#include <algorithm>

namespace workpane::terminalcore {

class TerminalThemeCatalogHelper final {
  public:
    static domain::TerminalTheme vividTheme();
    static domain::TerminalTheme balancedTheme();
    static domain::TerminalTheme softTheme();
};

domain::TerminalTheme TerminalThemeCatalogHelper::vividTheme() {
    return {QStringLiteral("vivid"), QStringLiteral("Vivid"), QColor(QStringLiteral("#f2f2f2")), QColor(QStringLiteral("#181818")), QColor(QStringLiteral("#d8d8d8")), {QColor(QStringLiteral("#181818")), QColor(QStringLiteral("#f14c4c")), QColor(QStringLiteral("#23d18b")), QColor(QStringLiteral("#f5f543")), QColor(QStringLiteral("#3b8eea")), QColor(QStringLiteral("#d670d6")), QColor(QStringLiteral("#29b8db")), QColor(QStringLiteral("#e5e5e5")), QColor(QStringLiteral("#666666")), QColor(QStringLiteral("#f14c4c")), QColor(QStringLiteral("#23d18b")), QColor(QStringLiteral("#f5f543")), QColor(QStringLiteral("#3b8eea")), QColor(QStringLiteral("#d670d6")), QColor(QStringLiteral("#29b8db")), QColor(QStringLiteral("#ffffff"))}};
}

domain::TerminalTheme TerminalThemeCatalogHelper::balancedTheme() {
    return {QStringLiteral("balanced"), QStringLiteral("Balanced"), QColor(QStringLiteral("#e7e7e7")), QColor(QStringLiteral("#181818")), QColor(QStringLiteral("#c8c8c8")), {QColor(QStringLiteral("#202020")), QColor(QStringLiteral("#d16969")), QColor(QStringLiteral("#65b88a")), QColor(QStringLiteral("#d7ba7d")), QColor(QStringLiteral("#6ca0dc")), QColor(QStringLiteral("#b983c7")), QColor(QStringLiteral("#67b8c1")), QColor(QStringLiteral("#d4d4d4")), QColor(QStringLiteral("#666666")), QColor(QStringLiteral("#df7979")), QColor(QStringLiteral("#74c99a")), QColor(QStringLiteral("#e2c98c")), QColor(QStringLiteral("#7daee4")), QColor(QStringLiteral("#c693d3")), QColor(QStringLiteral("#78c6ce")), QColor(QStringLiteral("#f0f0f0"))}};
}

domain::TerminalTheme TerminalThemeCatalogHelper::softTheme() {
    return {QStringLiteral("soft"), QStringLiteral("Soft"), QColor(QStringLiteral("#d2d2d2")), QColor(QStringLiteral("#181818")), QColor(QStringLiteral("#b7b7b7")), {QColor(QStringLiteral("#282828")), QColor(QStringLiteral("#b76e79")), QColor(QStringLiteral("#7fa78c")), QColor(QStringLiteral("#b9a978")), QColor(QStringLiteral("#7e99b7")), QColor(QStringLiteral("#9e83a8")), QColor(QStringLiteral("#7ca3a6")), QColor(QStringLiteral("#bdbdbd")), QColor(QStringLiteral("#5d5d5d")), QColor(QStringLiteral("#c47b85")), QColor(QStringLiteral("#8db69a")), QColor(QStringLiteral("#c5b685")), QColor(QStringLiteral("#8da8c5")), QColor(QStringLiteral("#ad92b7")), QColor(QStringLiteral("#8bb1b4")), QColor(QStringLiteral("#dddddd"))}};
}

const QVector<domain::TerminalTheme>& TerminalThemes::terminalThemes() {
    static const QVector<domain::TerminalTheme> themes = {TerminalThemeCatalogHelper::vividTheme(), TerminalThemeCatalogHelper::balancedTheme(), TerminalThemeCatalogHelper::softTheme()};
    return themes;
}

const domain::TerminalTheme* TerminalThemes::terminalTheme(const QString& id) {
    const auto& themes = TerminalThemes::terminalThemes();
    const auto match = std::ranges::find(themes, id, &domain::TerminalTheme::id);
    return match == themes.end() ? nullptr : &*match;
}

bool TerminalThemes::terminalThemeExists(const QString& id) {
    const auto& themes = TerminalThemes::terminalThemes();
    return std::ranges::find(themes, id, &domain::TerminalTheme::id) != themes.end();
}

} // namespace workpane::terminalcore
