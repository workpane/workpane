#include "ui/ButtonState.h"

#include <imgui.h>

namespace workpane::ui {

bool ButtonState::doubleClicked() const {
    return hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
}

} // namespace workpane::ui
