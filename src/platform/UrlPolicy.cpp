#include "platform/UrlPolicy.h"

#include <algorithm>
#include <string_view>

namespace workpane::platform {

bool UrlPolicy::allowed(std::string_view url) {
    const bool secure = url.starts_with("https://");
    const bool plain = url.starts_with("http://");

    if (!secure && !plain) {
        return false;
    }

    const std::string_view rest = url.substr(secure ? 8 : 7);

    // clang-format off
    return !rest.empty() && rest.front() != '/' && std::ranges::none_of(url, [](char character) { return static_cast<unsigned char>(character) <= 0x20 || character == '"' || character == '\\' || character == '`'; });
    // clang-format on
}

} // namespace workpane::platform
