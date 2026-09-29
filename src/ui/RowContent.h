#pragma once

#include "ui/IconCatalog.h"
#include "ui/theme/Theme.h"

#include <optional>
#include <string_view>

namespace workpane::ui {

struct RowContent final {
    std::string_view text;
    std::string_view detail;
    std::optional<Icon> icon;
    std::optional<ThemeColor> iconColor;
};

} // namespace workpane::ui
