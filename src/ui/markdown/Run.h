#pragma once

#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

#include <imgui.h>

#include <cstddef>
#include <optional>
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
    std::optional<ThemeColor> color;
    bool strike{false};
    std::size_t textOffset{0};
    std::string image;
    float lineTop{0.0F};
};

} // namespace workpane::ui::markdown
