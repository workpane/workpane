#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace workpane::platform {

// Reads the one line values the kernel publishes as files, answering an empty text or zero for a file this machine does not have.
class LinuxSysfs final {
  public:
    [[nodiscard]] static std::string text(const std::filesystem::path& file);
    [[nodiscard]] static std::int64_t integer(const std::filesystem::path& file);
    [[nodiscard]] static std::int64_t size(const std::filesystem::path& file);
};

} // namespace workpane::platform
