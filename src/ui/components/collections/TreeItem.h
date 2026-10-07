#pragma once

#include "ui/IconCatalog.h"
#include "ui/MenuItem.h"
#include "ui/model/TextValue.h"
#include "ui/theme/Theme.h"

#include <optional>
#include <string>
#include <vector>

namespace workpane::ui {

struct TreeItem final {
    std::string id;
    TextValue text;
    std::optional<Icon> icon;
    std::optional<ThemeColor> iconColor;
    std::optional<bool> expanded;
    bool branch{false};
    bool draggable{false};
    bool droppable{false};
    std::vector<MenuItem> menu;
    std::vector<TreeItem> children;
};

} // namespace workpane::ui
