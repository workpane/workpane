#pragma once

#include <filesystem>

namespace workpane::tests {

// The resources the build staged for the product, which the suites read so they test exactly what ships.
class Resources final {
  public:
    [[nodiscard]] static std::filesystem::path staged();
    [[nodiscard]] static std::filesystem::path fonts();
};

} // namespace workpane::tests
