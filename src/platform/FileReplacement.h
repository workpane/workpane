#pragma once

#include <chrono>
#include <filesystem>
#include <system_error>

namespace workpane::platform {

// Moves a file over another one in a single step of the file system, waiting on Windows while another program holds either of them for a moment.
class FileReplacement final {
  public:
    [[nodiscard]] static std::error_code replace(const std::filesystem::path& from, const std::filesystem::path& to);

  private:
    static constexpr std::chrono::milliseconds busyBound{2000};
    static constexpr std::chrono::milliseconds retryPause{20};
};

} // namespace workpane::platform
