#pragma once

#include <imgui.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace workpane::ui {

// Reads the key combinations plugins declare, such as `mod+shift+r`, where `mod` is Command on macOS and Control on every other platform.
// A combination carries `mod` or `alt` unless its key is a function key, so a shortcut never takes a character the reader is typing.
class KeyChords final {
  public:
    [[nodiscard]] static std::optional<ImGuiKeyChord> parse(std::string_view keys);
    [[nodiscard]] static bool reserved(ImGuiKeyChord chord);
    [[nodiscard]] static const std::vector<std::pair<std::string, ImGuiKey>>& named();

  private:
    static constexpr std::array<ImGuiKeyChord, 7> editingChords{ImGuiMod_Ctrl | ImGuiKey_A, ImGuiMod_Ctrl | ImGuiKey_C, ImGuiMod_Ctrl | ImGuiKey_V, ImGuiMod_Ctrl | ImGuiKey_X, ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z};
    static constexpr std::array<ImGuiKey, 8> caretKeys{ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_Home, ImGuiKey_End, ImGuiKey_Backspace, ImGuiKey_Delete};
    static constexpr std::array<ImGuiKey, 4> wordKeys{ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_Backspace, ImGuiKey_Delete};
    static constexpr std::array<std::pair<std::string_view, ImGuiKey>, 18> namedKeys{{{"up", ImGuiKey_UpArrow}, {"down", ImGuiKey_DownArrow}, {"left", ImGuiKey_LeftArrow}, {"right", ImGuiKey_RightArrow}, {"enter", ImGuiKey_Enter}, {"escape", ImGuiKey_Escape}, {"tab", ImGuiKey_Tab}, {"space", ImGuiKey_Space}, {"backspace", ImGuiKey_Backspace}, {"delete", ImGuiKey_Delete}, {"home", ImGuiKey_Home}, {"end", ImGuiKey_End}, {"pageup", ImGuiKey_PageUp}, {"pagedown", ImGuiKey_PageDown}, {"minus", ImGuiKey_Minus}, {"equal", ImGuiKey_Equal}, {"leftbracket", ImGuiKey_LeftBracket}, {"rightbracket", ImGuiKey_RightBracket}}};

    [[nodiscard]] static std::optional<ImGuiKey> key(std::string_view name);
    [[nodiscard]] static bool functionKey(ImGuiKey key);
};

} // namespace workpane::ui
