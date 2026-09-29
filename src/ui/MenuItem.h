#pragma once

#include "ui/IconCatalog.h"
#include "ui/model/TextValue.h"

#include <optional>
#include <string>

namespace workpane::ui {

struct MenuItem final {
    std::string id;
    TextValue text;
    std::optional<Icon> icon;
    std::string shortcut;
    bool enabled{true};
    bool separator{false};
    bool destructive{false};
};

} // namespace workpane::ui
