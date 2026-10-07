#pragma once

#include "ui/Fonts.h"
#include "ui/theme/Theme.h"

#include <imgui.h>

namespace workpane::ui {

// The ImGui style is derived entirely from the active theme, so a widget ImGui draws itself follows the same colors and geometry as the product surfaces.
class Style final {
  public:
    static void apply(const Theme& theme, const Fonts& fonts, float scale, ImGuiStyle& style);
    static void matchDensity(float density);

  private:
    static constexpr float curveError{0.2F};
};

} // namespace workpane::ui
