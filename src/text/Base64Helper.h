#pragma once

#include <optional>
#include <string_view>
#include <vector>

namespace workpane::text {

// The standard alphabet of Base64, in which data addresses carry the bytes of an image.
class Base64Helper final {
  public:
    [[nodiscard]] static std::optional<std::vector<unsigned char>> decode(std::string_view text);

  private:
    [[nodiscard]] static int value(char character);
};

} // namespace workpane::text
