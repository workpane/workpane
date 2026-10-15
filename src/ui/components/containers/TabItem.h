#pragma once

#include "ui/IconCatalog.h"
#include "ui/model/TextValue.h"

#include <optional>
#include <string>

namespace workpane::ui {

struct TabItem final {
    std::string id;
    TextValue text;
    std::optional<Icon> icon;
    std::string image;
    std::optional<bool> closable;
    TextValue tooltip;
};

} // namespace workpane::ui
