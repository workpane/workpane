#pragma once

#include <filesystem>
#include <string_view>

namespace workpane::tests {

// Names a path from the root of the drive the suites run on, so a path a fake answers is absolute on every platform.
class RootedPath final {
  public:
    [[nodiscard]] static std::filesystem::path of(std::string_view relative);
};

} // namespace workpane::tests
