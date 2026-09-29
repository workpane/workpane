#pragma once

#include "platform/InstalledFont.h"

#include <vector>

namespace workpane::platform {

// The font families of macOS, read through Core Text, which answers from any thread.
class MacFonts final {
  public:
    [[nodiscard]] static std::vector<InstalledFont> monospace();
};

} // namespace workpane::platform
