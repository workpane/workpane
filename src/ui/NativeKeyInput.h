#pragma once

#include <imgui.h>

#include <set>
#include <vector>

namespace workpane::ui {

class NativeKeyInput final {
  public:
    void shortcuts(std::vector<ImGuiKeyChord> chords);
    void release();
    [[nodiscard]] bool relay(ImGuiKey key, bool down, ImGuiKeyChord modifiers);
    [[nodiscard]] static ImGuiKey character(unsigned int character);

  private:
    std::vector<ImGuiKeyChord> m_shortcuts;
    std::set<ImGuiKey> m_pressed;
};

} // namespace workpane::ui
