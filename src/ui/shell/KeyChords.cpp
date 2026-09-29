#include "ui/shell/KeyChords.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

namespace workpane::ui {

std::optional<ImGuiKey> KeyChords::key(std::string_view name) {
    if (name.size() == 1 && name[0] >= 'a' && name[0] <= 'z') {
        return static_cast<ImGuiKey>(ImGuiKey_A + (name[0] - 'a'));
    }

    if (name.size() == 1 && name[0] >= '0' && name[0] <= '9') {
        return static_cast<ImGuiKey>(ImGuiKey_0 + (name[0] - '0'));
    }

    if (name.size() >= 2 && name.size() <= 3 && name[0] == 'f') {
        const std::string digits(name.substr(1));
        const int number = digits.find_first_not_of("0123456789") == std::string::npos ? std::stoi(digits) : 0;

        if (number >= 1 && number <= 12) {
            return static_cast<ImGuiKey>(ImGuiKey_F1 + (number - 1));
        }
    }

    for (const auto& [named, value] : namedKeys) {
        if (named == name) {
            return value;
        }
    }

    return std::nullopt;
}

// Every key the grammar names with its name, the letters, the digits, the function keys and the named keys, which is how a canvas tells its plugin what was pressed.
const std::vector<std::pair<std::string, ImGuiKey>>& KeyChords::named() {
    // clang-format off
    static const std::vector<std::pair<std::string, ImGuiKey>> keys = [] {
        std::vector<std::pair<std::string, ImGuiKey>> values;

        for (char letter = 'a'; letter <= 'z'; ++letter) {
            values.emplace_back(std::string(1, letter), static_cast<ImGuiKey>(ImGuiKey_A + (letter - 'a')));
        }

        for (char digit = '0'; digit <= '9'; ++digit) {
            values.emplace_back(std::string(1, digit), static_cast<ImGuiKey>(ImGuiKey_0 + (digit - '0')));
        }

        for (int number = 1; number <= 12; ++number) {
            values.emplace_back("f" + std::to_string(number), static_cast<ImGuiKey>(ImGuiKey_F1 + (number - 1)));
        }

        for (const auto& [name, key] : namedKeys) {
            values.emplace_back(std::string(name), key);
        }

        return values;
    }();
    // clang-format on
    return keys;
}

bool KeyChords::functionKey(ImGuiKey key) {
    return key >= ImGuiKey_F1 && key <= ImGuiKey_F12;
}

std::optional<ImGuiKeyChord> KeyChords::parse(std::string_view keys) {
    ImGuiKeyChord modifiers = ImGuiMod_None;
    std::optional<ImGuiKey> key;
    std::size_t start = 0;

    while (start <= keys.size()) {
        const std::size_t separator = keys.find('+', start);
        const std::string_view part = keys.substr(start, separator == std::string_view::npos ? std::string_view::npos : separator - start);
        const ImGuiKeyChord modifier = part == "mod" ? ImGuiMod_Ctrl : part == "shift" ? ImGuiMod_Shift : part == "alt" ? ImGuiMod_Alt : ImGuiMod_None;

        // Modifiers come first and each one once, and exactly one key closes the combination.
        if (key.has_value()) {
            return std::nullopt;
        }

        if (modifier != ImGuiMod_None) {
            if ((modifiers & modifier) != 0) {
                return std::nullopt;
            }

            modifiers |= modifier;
        } else {
            key = KeyChords::key(part);

            if (!key.has_value()) {
                return std::nullopt;
            }
        }

        if (separator == std::string_view::npos) {
            break;
        }

        start = separator + 1;
    }

    if (!key.has_value() || ((modifiers & (ImGuiMod_Ctrl | ImGuiMod_Alt)) == 0 && !functionKey(*key))) {
        return std::nullopt;
    }

    return modifiers | *key;
}

// The core keeps quitting, switching between the first nine destinations and editing text for itself, because plugin shortcuts are answered even while a field has the keyboard.
// Editing text includes moving and deleting by word, line or document, which mod takes with the caret keys and alt takes with the word keys, with or without shift.
bool KeyChords::reserved(ImGuiKeyChord chord) {
    if (chord == (ImGuiMod_Ctrl | ImGuiKey_Q) || std::ranges::find(editingChords, chord) != editingChords.end()) {
        return true;
    }

    const ImGuiKeyChord modifiers = chord & ImGuiMod_Mask_ & ~ImGuiMod_Shift;
    const auto key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
    const bool caret = modifiers == ImGuiMod_Ctrl && std::ranges::find(caretKeys, key) != caretKeys.end();
    const bool word = modifiers == ImGuiMod_Alt && std::ranges::find(wordKeys, key) != wordKeys.end();

    if (caret || word) {
        return true;
    }

    return chord >= (ImGuiMod_Ctrl | ImGuiKey_1) && chord <= (ImGuiMod_Ctrl | ImGuiKey_9);
}

} // namespace workpane::ui
