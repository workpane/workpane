#include "ui/theme/RedTheme.h"

namespace workpane::ui {

std::string_view RedTheme::id() const {
    return "red";
}

std::string_view RedTheme::titleKey() const {
    return "workpane.application.theme-red";
}

Color RedTheme::accent() const {
    return Color::rgb(190, 70, 75);
}

Color RedTheme::success() const {
    return Color::rgb(218, 82, 88);
}

} // namespace workpane::ui
