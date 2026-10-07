#pragma once

#include <string>
#include <string_view>

namespace workpane::platform {

// Converts between the UTF-8 text of the product and the wide text the Windows API speaks.
class WindowsTextHelper final {
  public:
    [[nodiscard]] static std::wstring wide(std::string_view text);
    [[nodiscard]] static std::string narrow(const wchar_t* text);
    [[nodiscard]] static std::wstring quoted(const std::wstring& argument);
};

} // namespace workpane::platform
