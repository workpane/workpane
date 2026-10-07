#pragma once

#include "ui/MenuItem.h"
#include "ui/model/DragValue.h"
#include "ui/model/TextValue.h"

#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace workpane::ui {

enum class Alignment { Start, Center, End, Stretch };

struct CommonProperties final {
    static constexpr float unbounded{std::numeric_limits<float>::max()};

    bool visible{true};
    bool enabled{true};
    TextValue tooltip;
    float grow{0.0F};
    int collapse{0};
    std::optional<float> width;
    std::optional<float> height;
    float minimumWidth{0.0F};
    float maximumWidth{unbounded};
    float minimumHeight{0.0F};
    float maximumHeight{unbounded};
    std::optional<Alignment> align;
    std::vector<MenuItem> menu;
    std::optional<DragValue> drag;
    std::vector<std::string> accepts;
};

} // namespace workpane::ui
