#pragma once

#include "ui/components/terminal/TerminalScreen.h"

#include <imgui.h>
#include <vterm.h>

#include <array>
#include <utility>

namespace workpane::ui {

// Turns the keys the reader presses into what the program behind a terminal receives, the way the terminals of each platform do.
// Command combinations on macOS and control with shift elsewhere belong to the product, so they never reach the program.
class TerminalKeys final {
  public:
    enum class Action { None, Copy, Paste, SelectAll, Clear, Find, FindNext, FindPrevious, PageUp, PageDown, Top, Bottom };

    [[nodiscard]] static Action action(bool selecting, bool finding);
    static void send(TerminalScreen& screen);
    [[nodiscard]] static VTermModifier modifiers();
    [[nodiscard]] static bool linkModifier();
    [[nodiscard]] static bool product();

  private:
    static constexpr int functionKeys{12};
    static constexpr std::array<std::pair<ImGuiKey, VTermKey>, 15> namedKeys{{{ImGuiKey_Enter, VTERM_KEY_ENTER}, {ImGuiKey_KeypadEnter, VTERM_KEY_KP_ENTER}, {ImGuiKey_Tab, VTERM_KEY_TAB}, {ImGuiKey_Backspace, VTERM_KEY_BACKSPACE}, {ImGuiKey_Escape, VTERM_KEY_ESCAPE}, {ImGuiKey_UpArrow, VTERM_KEY_UP}, {ImGuiKey_DownArrow, VTERM_KEY_DOWN}, {ImGuiKey_LeftArrow, VTERM_KEY_LEFT}, {ImGuiKey_RightArrow, VTERM_KEY_RIGHT}, {ImGuiKey_Insert, VTERM_KEY_INS}, {ImGuiKey_Delete, VTERM_KEY_DEL}, {ImGuiKey_Home, VTERM_KEY_HOME}, {ImGuiKey_End, VTERM_KEY_END}, {ImGuiKey_PageUp, VTERM_KEY_PAGEUP}, {ImGuiKey_PageDown, VTERM_KEY_PAGEDOWN}}};
    static constexpr std::array<std::pair<ImGuiKey, char>, 7> controlPunctuation{{{ImGuiKey_LeftBracket, '['}, {ImGuiKey_Backslash, '\\'}, {ImGuiKey_RightBracket, ']'}, {ImGuiKey_Space, ' '}, {ImGuiKey_Slash, '/'}, {ImGuiKey_Minus, '-'}, {ImGuiKey_Apostrophe, '\''}}};

    [[nodiscard]] static bool mac();
    [[nodiscard]] static bool windows();
    [[nodiscard]] static bool control();
    [[nodiscard]] static bool pressed(ImGuiKey key);
    [[nodiscard]] static Action shifted();
    [[nodiscard]] static Action stepping();
    [[nodiscard]] static bool moved(TerminalScreen& screen, ImGuiKey key);
    static void sendControl(TerminalScreen& screen);
    static void sendCharacters(TerminalScreen& screen);
};

} // namespace workpane::ui
