#include "ui/NativeKeyInput.h"

#include <algorithm>
#include <array>
#include <utility>

namespace workpane::ui {

void NativeKeyInput::shortcuts(std::vector<ImGuiKeyChord> chords) {
    if (chords != m_shortcuts) {
        release();
    }

    m_shortcuts = std::move(chords);
}

void NativeKeyInput::release() {
    for (const ImGuiKey key : m_pressed) {
        ImGui::GetIO().AddKeyEvent(key, false);
    }

    m_pressed.clear();
}

bool NativeKeyInput::relay(ImGuiKey key, bool down, ImGuiKeyChord modifiers) {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiKeyChord canonical = modifiers;

    if (io.ConfigMacOSXBehaviors) {
        canonical &= ~(ImGuiMod_Ctrl | ImGuiMod_Super);
        canonical |= (modifiers & ImGuiMod_Super) != 0 ? ImGuiMod_Ctrl : 0;
        canonical |= (modifiers & ImGuiMod_Ctrl) != 0 ? ImGuiMod_Super : 0;
    }

    if (key == ImGuiKey_None || (down && std::ranges::find(m_shortcuts, canonical | key) == m_shortcuts.end()) || (!down && !m_pressed.contains(key))) {
        return false;
    }

    for (const ImGuiKey modifier : std::array{ImGuiMod_Ctrl, ImGuiMod_Shift, ImGuiMod_Alt, ImGuiMod_Super}) {
        io.AddKeyEvent(modifier, (modifiers & modifier) != 0);
    }

    io.AddKeyEvent(key, down);

    if (down) {
        m_pressed.insert(key);
    } else {
        m_pressed.erase(key);
    }

    return true;
}

ImGuiKey NativeKeyInput::character(unsigned int character) {
    if (character >= 'A' && character <= 'Z') {
        character += 'a' - 'A';
    }

    if (character >= 'a' && character <= 'z') {
        return static_cast<ImGuiKey>(ImGuiKey_A + static_cast<int>(character - 'a'));
    }

    if (character >= '0' && character <= '9') {
        return static_cast<ImGuiKey>(ImGuiKey_0 + static_cast<int>(character - '0'));
    }

    switch (character) {
    case ',':
        return ImGuiKey_Comma;
    case '-':
        return ImGuiKey_Minus;
    case '=':
        return ImGuiKey_Equal;
    case '[':
        return ImGuiKey_LeftBracket;
    case ']':
        return ImGuiKey_RightBracket;
    case '\t':
    case 25:
        return ImGuiKey_Tab;
    case ' ':
        return ImGuiKey_Space;
    case 27:
        return ImGuiKey_Escape;
    default:
        return ImGuiKey_None;
    }
}

} // namespace workpane::ui
