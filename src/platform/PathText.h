#pragma once

#include <filesystem>
#include <string>

namespace workpane::platform {

// Writes a path in UTF-8, replacing what UTF-8 cannot carry, such as a lone surrogate in a Windows name, so a name read from the disk never ends the product.
// The generic form uses forward slashes on every platform, so a plugin compares and joins paths one way, and a network share reads as two slashes and its server.
class PathText final {
  public:
    [[nodiscard]] static std::string utf8(const std::filesystem::path& path);
    [[nodiscard]] static std::string generic(const std::filesystem::path& path);
};

} // namespace workpane::platform
