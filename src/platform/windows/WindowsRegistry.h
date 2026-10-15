#pragma once

#include <cstdint>
#include <string>

namespace workpane::platform {

// Reads values of the machine registry, answering an empty text or zero for a value this machine does not carry.
class WindowsRegistry final {
  public:
    [[nodiscard]] static std::string text(const wchar_t* key, const wchar_t* value);
    [[nodiscard]] static std::uint32_t number(const wchar_t* key, const wchar_t* value);
};

} // namespace workpane::platform
