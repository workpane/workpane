#include "ui/components/terminal/TerminalKeys.h"

#include "ui/FindBar.h"

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

namespace workpane::ui {

// ImGui reads Command as its control modifier on macOS, so the control key of a terminal is the super modifier there.
bool TerminalKeys::mac() {
    return ImGui::GetIO().ConfigMacOSXBehaviors;
}

bool TerminalKeys::windows() {
#if defined(_WIN32)
    return true;
#else
    return false;
#endif
}

bool TerminalKeys::control() {
    return mac() ? ImGui::GetIO().KeySuper : ImGui::GetIO().KeyCtrl;
}

bool TerminalKeys::product() {
    const ImGuiIO& io = ImGui::GetIO();
    return mac() ? io.KeyCtrl : io.KeyCtrl && io.KeyShift;
}

// A held key repeats after the delay and at the interval the system configures, and every repeat that fell between two frames is counted, so a slow frame never drops one.
int TerminalKeys::presses(ImGuiKey key) {
    const ImGuiIO& io = ImGui::GetIO();
    return ImGui::GetKeyPressedAmount(key, io.KeyRepeatDelay, io.KeyRepeatRate);
}

bool TerminalKeys::pressed(ImGuiKey key) {
    return ImGui::IsKeyPressed(key, true);
}

// Windows keeps the plain control combinations of its console for copy and paste, and copy answers only while something is selected, so the same keys interrupt the program otherwise.
// The keys that step through matches answer only while a search is open, so a program that uses F3 still receives it otherwise.
TerminalKeys::Action TerminalKeys::action(bool selecting, bool finding) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool console = windows() && io.KeyCtrl && !io.KeyShift;

    if (const Action paging = shifted(); paging != Action::None) {
        return paging;
    }

    if (const Action scrolling = commanded(); scrolling != Action::None) {
        return scrolling;
    }

    if (const Action step = finding ? stepping() : Action::None; step != Action::None) {
        return step;
    }

    if (!product() && !console) {
        return Action::None;
    }

    if (selecting && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
        return Action::Copy;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_V, false)) {
        return Action::Paste;
    }

    if (!product()) {
        return Action::None;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        return Action::SelectAll;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_K, false)) {
        return Action::Clear;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
        return Action::Find;
    }

    return Action::None;
}

// The platform steps through matches with command and G on macOS and with F3 elsewhere, and shift steps back.
TerminalKeys::Action TerminalKeys::stepping() {
    const int step = FindBar::stepping();
    return step == 0 ? Action::None : step > 0 ? Action::FindNext : Action::FindPrevious;
}

// Shift alone with the paging keys moves through the history and with insert pastes, as the terminals of every desktop do.
TerminalKeys::Action TerminalKeys::shifted() {
    const ImGuiIO& io = ImGui::GetIO();

    if (!io.KeyShift || io.KeyCtrl || io.KeyAlt || io.KeySuper) {
        return Action::None;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Insert, false)) {
        return Action::Paste;
    }

    if (pressed(ImGuiKey_PageUp)) {
        return Action::PageUp;
    }

    if (pressed(ImGuiKey_PageDown)) {
        return Action::PageDown;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
        return Action::Top;
    }

    return ImGui::IsKeyPressed(ImGuiKey_End, false) ? Action::Bottom : Action::None;
}

// Command with the arrows, the paging keys, home and end moves through the history on macOS, as its terminals do.
TerminalKeys::Action TerminalKeys::commanded() {
    const ImGuiIO& io = ImGui::GetIO();

    if (!mac() || !io.KeyCtrl || io.KeyShift || io.KeyAlt || io.KeySuper) {
        return Action::None;
    }

    const std::array<std::pair<ImGuiKey, Action>, 6> scrolls{{{ImGuiKey_UpArrow, Action::LineUp}, {ImGuiKey_DownArrow, Action::LineDown}, {ImGuiKey_PageUp, Action::PageUp}, {ImGuiKey_PageDown, Action::PageDown}, {ImGuiKey_Home, Action::Top}, {ImGuiKey_End, Action::Bottom}}};

    for (const auto& [key, scroll] : scrolls) {
        if (pressed(key)) {
            return scroll;
        }
    }

    return Action::None;
}

VTermModifier TerminalKeys::modifiers() {
    const ImGuiIO& io = ImGui::GetIO();
    unsigned int flags = VTERM_MOD_NONE;
    flags |= io.KeyShift ? static_cast<unsigned int>(VTERM_MOD_SHIFT) : 0U;
    flags |= io.KeyAlt ? static_cast<unsigned int>(VTERM_MOD_ALT) : 0U;
    flags |= control() ? static_cast<unsigned int>(VTERM_MOD_CTRL) : 0U;

    return static_cast<VTermModifier>(flags);
}

// An address opens with the platform modifier and a click, which is Command on macOS and control elsewhere.
bool TerminalKeys::linkModifier() {
    return ImGui::GetIO().KeyCtrl;
}

void TerminalKeys::send(TerminalScreen& screen) {
    for (const auto& [key, named] : namedKeys) {
        for (int count = presses(key); count > 0; --count) {
            sendNamed(screen, key, named);
        }
    }

    if (product()) {
        ImGui::GetIO().InputQueueCharacters.resize(0);
        return;
    }

    for (int number = 1; number <= functionKeys; ++number) {
        for (int count = presses(static_cast<ImGuiKey>(ImGuiKey_F1 + number - 1)); count > 0; --count) {
            screen.key(static_cast<VTermKey>(VTERM_KEY_FUNCTION(number)), modifiers());
        }
    }

    sendControl(screen);
    sendCharacters(screen);
}

void TerminalKeys::sendNamed(TerminalScreen& screen, ImGuiKey key, VTermKey named) {
    if (const auto edited = edit(key); edited.has_value()) {
        screen.character(edited->character, edited->modifiers);
        return;
    }

    if (!product()) {
        screen.key(named, modifiers());
    }
}

// No shell binds a modified arrow or erasing key, so the word and line edits write the keys the shells of each platform really bind, the way the terminals of the platform do.
// On macOS command moves to the ends of the line and erases to them, and option moves and erases by word, while Linux moves and erases by word with control, and Windows leaves every modified key to the console.
std::optional<TerminalKeys::Edit> TerminalKeys::edit(ImGuiKey key) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool command = mac() && io.KeyCtrl && !io.KeyShift && !io.KeyAlt && !io.KeySuper;
    const bool option = mac() && io.KeyAlt && !io.KeyCtrl && !io.KeySuper;
    const bool word = !mac() && !windows() && io.KeyCtrl && !io.KeyShift && !io.KeyAlt;

    if (command) {
        const std::array<std::pair<ImGuiKey, char>, 4> lineEdits{{{ImGuiKey_LeftArrow, 'a'}, {ImGuiKey_RightArrow, 'e'}, {ImGuiKey_Backspace, 'u'}, {ImGuiKey_Delete, 'k'}}};

        for (const auto& [edited, character] : lineEdits) {
            if (key == edited) {
                return Edit{static_cast<std::uint32_t>(character), VTERM_MOD_CTRL};
            }
        }

        return std::nullopt;
    }

    if (!option && !word) {
        return std::nullopt;
    }

    if (key == ImGuiKey_LeftArrow || key == ImGuiKey_RightArrow || key == ImGuiKey_Delete) {
        return Edit{static_cast<std::uint32_t>(key == ImGuiKey_LeftArrow ? 'b' : key == ImGuiKey_RightArrow ? 'f' : 'd'), VTERM_MOD_ALT};
    }

    if (word && key == ImGuiKey_Backspace) {
        return Edit{static_cast<std::uint32_t>('w'), VTERM_MOD_CTRL};
    }

    return std::nullopt;
}

// Letters and a few punctuation keys held with control send their control characters, which no character input reports.
void TerminalKeys::sendControl(TerminalScreen& screen) {
    if (!control()) {
        return;
    }

    const auto alt = ImGui::GetIO().KeyAlt ? VTERM_MOD_ALT : VTERM_MOD_NONE;
    const auto modifiers = static_cast<VTermModifier>(VTERM_MOD_CTRL | alt);

    for (int letter = 0; letter < 26; ++letter) {
        for (int count = presses(static_cast<ImGuiKey>(ImGuiKey_A + letter)); count > 0; --count) {
            screen.character(static_cast<std::uint32_t>('a' + letter), modifiers);
        }
    }

    for (const auto& [key, punctuation] : controlPunctuation) {
        for (int count = presses(key); count > 0; --count) {
            screen.character(static_cast<std::uint32_t>(punctuation), modifiers);
        }
    }
}

// Typed characters arrive composed by the platform, and alt makes them meta characters except on macOS, where option composes them.
void TerminalKeys::sendCharacters(TerminalScreen& screen) {
    ImGuiIO& io = ImGui::GetIO();

    if (!control()) {
        const auto modifiers = io.KeyAlt && !mac() ? VTERM_MOD_ALT : VTERM_MOD_NONE;

        for (const ImWchar character : io.InputQueueCharacters) {
            screen.character(static_cast<std::uint32_t>(character), modifiers);
        }
    }

    io.InputQueueCharacters.resize(0);
}

} // namespace workpane::ui
