#pragma once

#include <chrono>
#include <filesystem>
#include <system_error>

namespace workpane::platform {

// Replaces a file with its POSIX permissions or Windows access list preserved, waiting within a bound while another program holds it on Windows.
class FileReplacement final {
  public:
    [[nodiscard]] static std::error_code replace(const std::filesystem::path& from, const std::filesystem::path& to);
    [[nodiscard]] static std::error_code create(const std::filesystem::path& from, const std::filesystem::path& to);

  private:
    struct Creation;

    static constexpr std::chrono::milliseconds busyBound{2000};
    static constexpr std::chrono::milliseconds retryPause{20};
};

} // namespace workpane::platform
