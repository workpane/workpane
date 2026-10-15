#pragma once

#include "platform/FallbackFace.h"
#include "platform/InstalledFont.h"

#include <windows.h>

#include <dwrite_1.h>

#include <array>
#include <string>
#include <vector>

namespace workpane::platform {

// The font families of Windows, read through DirectWrite.
class WindowsFonts final {
  public:
    [[nodiscard]] static std::vector<InstalledFont> monospace();
    [[nodiscard]] static std::vector<FallbackFace> fallbacks();

  private:
    static constexpr std::array<const wchar_t*, 7> fallbackFamilies{L"Segoe UI Emoji", L"Segoe UI Symbol", L"Segoe UI Historic", L"Microsoft YaHei", L"Yu Gothic", L"Malgun Gothic", L"Nirmala UI"};

    [[nodiscard]] static std::wstring familyName(IDWriteFontFamily& family);
    [[nodiscard]] static std::wstring fontFile(IDWriteFont& font);
};

} // namespace workpane::platform
