#pragma once

#include <string>
#include <vector>

namespace workpane::platform {

// The environment of the product as Windows hands it over, and the block a new process receives, sorted without regard to case as Windows keeps it.
class WindowsEnvironment final {
  public:
    [[nodiscard]] static std::vector<std::string> inherited();
    [[nodiscard]] static std::wstring block(const std::vector<std::string>& entries);
};

} // namespace workpane::platform
