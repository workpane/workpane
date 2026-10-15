#pragma once

#include "platform/FallbackFace.h"
#include "platform/InstalledFont.h"

#import <Foundation/Foundation.h>

#include <optional>
#include <vector>

namespace workpane::platform {

// The font families of macOS, read through Core Text, which answers from any thread.
class MacFonts final {
  public:
    [[nodiscard]] static std::vector<InstalledFont> monospace();
    [[nodiscard]] static std::vector<FallbackFace> fallbacks();

  private:
    [[nodiscard]] static std::optional<unsigned int> faceIndex(NSURL* file, NSString* name);
};

} // namespace workpane::platform
