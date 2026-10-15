#pragma once

#include "ui/IconCatalog.h"
#include "ui/components/indicators/Tones.h"
#include "ui/model/TextValue.h"

#include <optional>
#include <string>

namespace workpane::ui {

struct TableAction final {
    std::string id;
    Icon icon{Icon::Edit};
    TextValue tooltip;
    bool destructive{false};
    std::optional<Tone> tone;
};

} // namespace workpane::ui
