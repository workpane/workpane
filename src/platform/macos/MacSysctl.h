#pragma once

#include <cstdint>
#include <string>

namespace workpane::platform {

// Reads the kernel values macOS publishes by name, answering an empty text or zero for a name this machine does not publish.
class MacSysctl final {
  public:
    [[nodiscard]] static std::string text(const char* name);
    [[nodiscard]] static std::int64_t integer(const char* name);
};

} // namespace workpane::platform
