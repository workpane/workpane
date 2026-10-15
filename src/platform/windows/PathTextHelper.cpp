#include "platform/PathTextHelper.h"

#include "platform/windows/WindowsTextHelper.h"

#include <algorithm>

namespace workpane::platform {

std::string PathTextHelper::utf8(const std::filesystem::path& path) {
    return WindowsTextHelper::narrow(path.c_str());
}

std::string PathTextHelper::generic(const std::filesystem::path& path) {
    std::wstring text = path.native();
    std::ranges::replace(text, L'\\', L'/');

    return WindowsTextHelper::narrow(text.c_str());
}

} // namespace workpane::platform
