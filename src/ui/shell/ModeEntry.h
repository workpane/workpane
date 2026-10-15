#pragma once

#include "ui/IconCatalog.h"
#include "ui/shell/NavigationItem.h"

#include <string>

namespace workpane::ui {

struct ModeEntry final {
    std::string destination;
    std::string title;
    Icon icon{Icon::Workspace};
    NavigationPlacement placement{NavigationPlacement::Primary};
};

} // namespace workpane::ui
