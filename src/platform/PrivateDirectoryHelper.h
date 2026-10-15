#pragma once

#include <filesystem>
#include <system_error>

namespace workpane::platform {

class PrivateDirectoryHelper final {
  public:
    [[nodiscard]] static std::error_code create(const std::filesystem::path& path);
    [[nodiscard]] static bool restricted(const std::filesystem::path& path);

  private:
    struct Identity;
};

} // namespace workpane::platform
