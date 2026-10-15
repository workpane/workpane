#include "ui/theme/GreenTheme.h"

namespace workpane::ui {

std::string_view GreenTheme::id() const {
    return "green";
}

std::string_view GreenTheme::titleKey() const {
    return "workpane.application.theme-green";
}

Color GreenTheme::accent() const {
    return Color::rgb(31, 155, 93);
}

Color GreenTheme::success() const {
    return Color::rgb(39, 191, 115);
}

} // namespace workpane::ui
