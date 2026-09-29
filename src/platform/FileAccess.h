#pragma once

#include <filesystem>

namespace workpane::platform {

// Answers whether the account running the product may read or write a file or a folder, as the system itself decides it.
class FileAccess final {
  public:
    [[nodiscard]] static bool readable(const std::filesystem::path& path);
    [[nodiscard]] static bool writable(const std::filesystem::path& path);

  private:
    static constexpr int readMode{4};
    static constexpr int writeMode{2};
};

} // namespace workpane::platform
