#include "platform/PathText.h"

#include "platform/windows/WindowsText.h"

#include <algorithm>

namespace workpane::platform {

std::string PathText::utf8(const std::filesystem::path& path) {
    return WindowsText::narrow(path.c_str());
}

std::string PathText::generic(const std::filesystem::path& path) {
    std::wstring text = path.native();
    std::ranges::replace(text, L'\\', L'/');

    return WindowsText::narrow(text.c_str());
}

} // namespace workpane::platform
