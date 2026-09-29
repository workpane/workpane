#pragma once

#include "platform/InstalledFont.h"

#include <windows.h>

#include <dwrite_1.h>

#include <string>
#include <vector>

namespace workpane::platform {

// The font families of Windows, read through DirectWrite.
class WindowsFonts final {
  public:
    [[nodiscard]] static std::vector<InstalledFont> monospace();

  private:
    [[nodiscard]] static std::wstring familyName(IDWriteFontFamily& family);
    [[nodiscard]] static std::wstring fontFile(IDWriteFont& font);
};

} // namespace workpane::platform
