#pragma once

#include "Result.h"

#include <filesystem>
#include <string>

namespace workpane::tests {

class WindowsFileAccessHelper final {
  public:
    [[nodiscard]] static bool privateDirectory(const std::filesystem::path& path);
    [[nodiscard]] static Result<void> protect(const std::filesystem::path& path);
    [[nodiscard]] static Result<std::string> snapshot(const std::filesystem::path& path);
};

} // namespace workpane::tests
