#include "ui/AssetPath.h"

#include <algorithm>

namespace workpane::ui {

// An asset path is relative, climbs nothing and names only plain characters.
bool AssetPath::safe(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.find("..") != std::string_view::npos) {
        return false;
    }

    // clang-format off
    return std::ranges::all_of(path, [](char character) { return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') || character == '.' || character == '-' || character == '_' || character == '/'; });
    // clang-format on
}

} // namespace workpane::ui
