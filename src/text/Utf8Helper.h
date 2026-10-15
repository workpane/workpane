#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace workpane::text {

// The rules of UTF-8 the product reads bytes by: the length a lead byte announces, the bytes that may follow it, whole texts and cuts at a character boundary.
class Utf8Helper final {
  public:
    [[nodiscard]] static std::size_t sequenceLength(unsigned char lead);
    [[nodiscard]] static bool follows(unsigned char lead, std::size_t offset, unsigned char byte);
    [[nodiscard]] static bool valid(std::string_view text);
    [[nodiscard]] static std::string truncated(std::string_view text, std::size_t characters);
};

} // namespace workpane::text
