#pragma once

#include <imgui.h>

namespace workpane::ui {

struct TextFieldResult final {
    bool changed{false};
    bool submitted{false};
    bool active{false};
    bool focused{false};
    bool deactivated{false};
    ImGuiDir arrow{ImGuiDir_None};
};

} // namespace workpane::ui
