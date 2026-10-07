#include "ui/WheelScale.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace workpane::ui {

// Answers the units ImGui receives for steps that each scroll a number of points of the layout, so a window moves exactly that far.
ImVec2 WheelScale::units(ImVec2 steps, float points) {
    const ImVec2 size = unit();
    const float travel = points * ImGui::GetStyle().FontScaleDpi;

    return {steps.x * travel / size.x, steps.y * travel / size.y};
}

// Answers how far the wheel of this frame moves a view on each axis, in pixels of the layout, positive toward the start as ImGui counts it.
ImVec2 WheelScale::distance() {
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 size = unit();

    return {io.MouseWheelH * size.x, io.MouseWheel * size.y};
}

// Takes the wheel of the next frame from the windows around a view that scrolls by itself, so a turn over it moves that view alone.
void WheelScale::claim(ImGuiID owner) {
    ImGui::SetKeyOwner(ImGuiKey_MouseWheelX, owner);
    ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, owner);
}

// ImGui moves a window by the whole pixels of five lines of its font down and two across for each unit, the font drawn at the rounded size ImGui gives it.
ImVec2 WheelScale::unit() {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float line = ImGui::GetRoundedFontSize(style.FontSizeBase * style.FontScaleMain * style.FontScaleDpi);

    return {std::max(1.0F, std::trunc(linesAcross * line)), std::max(1.0F, std::trunc(linesDown * line))};
}

} // namespace workpane::ui
