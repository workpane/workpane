#pragma once

#include "ui/Fonts.h"
#include "ui/theme/FontRole.h"

#include <string_view>

namespace workpane::ui {

// A font role stays pushed for exactly the lifetime of the scope that asked for it.
class FontScope final {
  public:
    FontScope(const Fonts& fonts, FontRole role);
    FontScope(const Fonts& fonts, FontFace face, float size);
    FontScope(const Fonts& fonts, FontFace face, float size, std::string_view family);
    ~FontScope();

    FontScope(const FontScope&) = delete;
    FontScope& operator=(const FontScope&) = delete;
};

} // namespace workpane::ui
