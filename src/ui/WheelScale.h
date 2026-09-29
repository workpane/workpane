#pragma once

#include <imgui.h>

namespace workpane::ui {

// The distances of the wheel in the units ImGui scrolls by, five lines of the interface font down and two across for each unit it receives, so the steps of the window reach ImGui as far as the system scrolls and a view that scrolls by itself reads the same distance back.
class WheelScale final {
  public:
    [[nodiscard]] static ImVec2 units(ImVec2 steps, float points);
    [[nodiscard]] static ImVec2 distance();
    static void claim(ImGuiID owner);

  private:
    static constexpr float linesDown{5.0F};
    static constexpr float linesAcross{2.0F};

    [[nodiscard]] static ImVec2 unit();
};

} // namespace workpane::ui
