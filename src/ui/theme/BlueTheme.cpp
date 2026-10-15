#include "ui/theme/BlueTheme.h"

namespace workpane::ui {

std::string_view BlueTheme::id() const {
    return "blue";
}

std::string_view BlueTheme::titleKey() const {
    return "workpane.application.theme-blue";
}

Color BlueTheme::accent() const {
    return Color::rgb(45, 116, 203);
}

Color BlueTheme::success() const {
    return Color::rgb(61, 139, 229);
}

} // namespace workpane::ui
