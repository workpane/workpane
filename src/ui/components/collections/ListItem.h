#pragma once

#include "ui/IconCatalog.h"
#include "ui/model/TextValue.h"
#include "ui/theme/Theme.h"

#include <optional>
#include <string>

namespace workpane::ui {

struct ListItem final {
    std::string id;
    TextValue text;
    TextValue detail;
    std::optional<Icon> icon;
    std::optional<ThemeColor> iconColor;
};

} // namespace workpane::ui
