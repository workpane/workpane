#pragma once

#include <array>

namespace workpane::platform {

// Opens pipes whose ends no program the product starts inherits, as far as the system can promise it while other threads start programs.
class PosixPipe final {
  public:
    [[nodiscard]] static bool open(std::array<int, 2>& ends);
};

} // namespace workpane::platform
