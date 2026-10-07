#include "ui/theme/ThemeCatalog.h"

#include "ui/theme/BlueTheme.h"
#include "ui/theme/GreenTheme.h"
#include "ui/theme/RedTheme.h"

namespace workpane::ui {

ThemeCatalog::ThemeCatalog() {
    m_themes.push_back(std::make_unique<GreenTheme>());
    m_themes.push_back(std::make_unique<BlueTheme>());
    m_themes.push_back(std::make_unique<RedTheme>());
}

const std::vector<std::unique_ptr<Theme>>& ThemeCatalog::themes() const {
    return m_themes;
}

const Theme* ThemeCatalog::find(std::string_view themeId) const {
    for (const auto& theme : m_themes) {
        if (theme->id() == themeId) {
            return theme.get();
        }
    }

    return nullptr;
}

const Theme& ThemeCatalog::defaultTheme() const {
    return *m_themes.front();
}

} // namespace workpane::ui
