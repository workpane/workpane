#pragma once

#include "ui/theme/Theme.h"

#include <memory>
#include <string_view>
#include <vector>

namespace workpane::ui {

class ThemeCatalog final {
  public:
    ThemeCatalog();

    [[nodiscard]] const std::vector<std::unique_ptr<Theme>>& themes() const;
    [[nodiscard]] const Theme* find(std::string_view themeId) const;
    [[nodiscard]] const Theme& defaultTheme() const;

  private:
    std::vector<std::unique_ptr<Theme>> m_themes;
};

} // namespace workpane::ui
