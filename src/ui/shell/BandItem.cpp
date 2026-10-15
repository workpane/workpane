#include "ui/shell/BandItem.h"

namespace workpane::ui {

std::string BandItem::surface() const {
    return "band:" + plugin + ":" + id;
}

} // namespace workpane::ui
