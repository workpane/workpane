#pragma once

#include "ui/theme/FontRole.h"

#include <imgui.h>

#include <string>

namespace workpane::ui::markdown {

enum class Ink { Text, Muted, Link };

struct Run final {
    ImVec2 offset;
    float width{0.0F};
    float height{0.0F};
    FontRole font;
    Ink ink{Ink::Text};
    bool code{false};
    int link{-1};
    int clip{-1};
    std::string text;
};

} // namespace workpane::ui::markdown
