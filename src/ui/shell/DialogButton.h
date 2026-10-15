#pragma once

#include "ui/IconCatalog.h"
#include "ui/WidgetHelper.h"

#include <optional>
#include <string>

namespace workpane::ui {

struct DialogButton final {
    std::string id;
    std::string text;
    ButtonVariant variant{ButtonVariant::Default};
    std::optional<Icon> icon;
    bool closes{true};
    bool enabled{true};
};

} // namespace workpane::ui
