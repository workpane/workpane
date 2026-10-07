#include "ui/shell/NavigationItem.h"

namespace workpane::ui {

std::string NavigationItem::destination() const {
    return plugin + ":" + id;
}

std::string NavigationItem::surface() const {
    return "view:" + plugin + ":" + id;
}

} // namespace workpane::ui
