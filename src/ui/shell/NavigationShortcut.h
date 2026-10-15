#pragma once

#include <imgui.h>

#include <string>

namespace workpane::ui {

struct NavigationShortcut final {
    std::string id;
    ImGuiKeyChord chord{ImGuiKey_None};
};

} // namespace workpane::ui
