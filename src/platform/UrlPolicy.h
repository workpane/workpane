#pragma once

#include <string_view>

namespace workpane::platform {

// Only web addresses leave the product for the default browser, so no plugin can hand the platform a command disguised as a link.
class UrlPolicy final {
  public:
    [[nodiscard]] static bool allowed(std::string_view url);
};

} // namespace workpane::platform
