#pragma once

#include "ui/Color.h"

#include <imgui.h>

namespace workpane::ui {

enum class ChevronDirection { Down, Right };

// The shapes every surface shares are painted rather than styled, so they stay correct at any size and any display density.
class Painter final {
  public:
    static void indicator(ImDrawList& list, ImVec2 center, float diameter, Color color);
    static void closeCircle(ImDrawList& list, ImVec2 center, float diameter, Color circle, Color cross, bool emphasized);
    static void chevron(ImDrawList& list, ImVec2 center, float width, ChevronDirection direction, Color color);
    static void checkMark(ImDrawList& list, ImVec2 topLeft, float size, Color color);
    static void busyRing(ImDrawList& list, ImVec2 center, float radius, double seconds, Color color);
    static void horizontalDivider(ImDrawList& list, ImVec2 from, float width, float thickness, Color color);
    static void verticalDivider(ImDrawList& list, ImVec2 from, float height, float thickness, Color color);
    static void rectBorder(ImDrawList& list, ImVec2 minimum, ImVec2 maximum, float rounding, float thickness, Color color);

  private:
    static constexpr double busyTurnSeconds{1.0};
    static constexpr float busySweepDegrees{110.0F};
    static constexpr float closeCrossInset{0.32F};
    static constexpr float closeCrossThickness{1.4F};
    static constexpr float chevronThickness{1.6F};

    [[nodiscard]] static float pixels(float thickness);
};

} // namespace workpane::ui
