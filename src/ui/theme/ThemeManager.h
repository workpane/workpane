#pragma once

#include "Result.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeCatalog.h"

#include <string_view>

namespace workpane::ui {

// The selected theme is validated against the catalog before it changes, and an unknown stored identifier selects the default.
class ThemeManager final {
  public:
    ThemeManager();

    [[nodiscard]] const ThemeCatalog& catalog() const;
    [[nodiscard]] const Theme& theme() const;
    [[nodiscard]] Result<void> selectTheme(std::string_view themeId);
    void loadStoredTheme(std::string_view storedThemeId);

  private:
    ThemeCatalog m_catalog;
    const Theme* m_theme{nullptr};
};

} // namespace workpane::ui
