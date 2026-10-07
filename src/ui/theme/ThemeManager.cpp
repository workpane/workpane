#include "ui/theme/ThemeManager.h"

#include "Result.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeCatalog.h"

#include <string>

namespace workpane::ui {

ThemeManager::ThemeManager() : m_theme(&m_catalog.defaultTheme()) {}

const ThemeCatalog& ThemeManager::catalog() const {
    return m_catalog;
}

const Theme& ThemeManager::theme() const {
    return *m_theme;
}

Result<void> ThemeManager::selectTheme(std::string_view themeId) {
    const Theme* selected = m_catalog.find(themeId);

    if (selected == nullptr) {
        return Result<void>::failure({"application_theme_invalid", "The application theme is unsupported", std::string(themeId)});
    }

    m_theme = selected;

    return Result<void>::success();
}

// A missing or unknown stored identifier selects Green, which is the only implicit theme choice the product makes.
void ThemeManager::loadStoredTheme(std::string_view storedThemeId) {
    const Theme* stored = m_catalog.find(storedThemeId);
    m_theme = stored == nullptr ? &m_catalog.defaultTheme() : stored;
}

} // namespace workpane::ui
