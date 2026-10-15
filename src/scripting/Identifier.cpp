#include "scripting/Identifier.h"

#include <algorithm>

namespace workpane::scripting {

// An identifier is lowercase letters, numbers and single hyphens between them, and never holding two hyphens in a row is what keeps the table prefix of every plugin its own.
bool Identifier::valid(std::string_view identifier) {
    if (identifier.empty() || identifier.front() == '-' || identifier.back() == '-' || identifier.find("--") != std::string_view::npos) {
        return false;
    }

    // clang-format off
    return std::ranges::all_of(identifier, [](char character) { return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '-'; });
    // clang-format on
}

} // namespace workpane::scripting
