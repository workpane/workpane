#pragma once

#include "ui/IconCatalog.h"
#include "ui/components/indicators/Tones.h"
#include "ui/model/TextValue.h"

#include <optional>

namespace workpane::ui {

struct TableCell final {
    TextValue text;
    std::optional<Icon> icon;
    std::optional<Tone> tone;
    bool muted{false};
    bool monospace{false};
};

} // namespace workpane::ui
