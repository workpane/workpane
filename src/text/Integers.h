#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace workpane::text {

// Reads a whole number the system wrote as text, in decimal or in hexadecimal with or without its prefix, and answers nothing for any other text instead of throwing.
class Integers final {
  public:
    [[nodiscard]] static std::optional<std::int64_t> parse(std::string_view text, int base = 10);
    [[nodiscard]] static std::optional<std::int64_t> leading(std::string_view text);
};

} // namespace workpane::text
