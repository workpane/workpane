#include "ui/model/ContentFontSize.h"

#include <imgui.h>

#include <algorithm>

namespace workpane::ui {

// The plain equal, minus and zero keys zoom, and so do the plus the zoom standard names and the keypad, all with the control modifier.
std::optional<FontStep> ContentFontSize::shortcutStep() {
    if (!ImGui::GetIO().KeyCtrl) {
        return std::nullopt;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) {
        return FontStep::Increase;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) {
        return FontStep::Decrease;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_0, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad0, false)) {
        return FontStep::Reset;
    }

    return std::nullopt;
}

double ContentFontSize::stepped(double size, FontStep step) {
    switch (step) {
    case FontStep::Increase:
        return std::clamp(size + 1.0, minimum, maximum);
    case FontStep::Decrease:
        return std::clamp(size - 1.0, minimum, maximum);
    case FontStep::Reset:
        return standard;
    }

    return size;
}

} // namespace workpane::ui
