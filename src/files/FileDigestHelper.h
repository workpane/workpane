#pragma once

#include "Result.h"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace workpane::files {

class FileDigestHelper final {
  public:
    [[nodiscard]] static Result<std::string> sha256(const std::filesystem::path& path, std::size_t maximum, const std::atomic<bool>& stopping);
    [[nodiscard]] static Result<std::string> sha256(std::string_view bytes);

  private:
    [[nodiscard]] static std::string encode(std::span<const unsigned char> digest);
};

} // namespace workpane::files
