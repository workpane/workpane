#pragma once

#include "ui/Color.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace workpane::ui {

// The ANSI color schemes of the terminal, which are content themes chosen apart from the theme of the product.
class TerminalPalette final {
  public:
    [[nodiscard]] static std::optional<TerminalPalette> named(std::string_view name);

    [[nodiscard]] Color foreground() const;
    [[nodiscard]] Color background() const;
    [[nodiscard]] Color cursor() const;
    [[nodiscard]] Color ansi(std::size_t index) const;

  private:
    using Colors = std::array<std::uint32_t, 19>;

    static constexpr Colors vivid{0xF2F2F2, 0x181818, 0xD8D8D8, 0x181818, 0xF14C4C, 0x23D18B, 0xF5F543, 0x3B8EEA, 0xD670D6, 0x29B8DB, 0xE5E5E5, 0x666666, 0xF14C4C, 0x23D18B, 0xF5F543, 0x3B8EEA, 0xD670D6, 0x29B8DB, 0xFFFFFF};
    static constexpr Colors balanced{0xE7E7E7, 0x181818, 0xC8C8C8, 0x202020, 0xD16969, 0x65B88A, 0xD7BA7D, 0x6CA0DC, 0xB983C7, 0x67B8C1, 0xD4D4D4, 0x666666, 0xDF7979, 0x74C99A, 0xE2C98C, 0x7DAEE4, 0xC693D3, 0x78C6CE, 0xF0F0F0};
    static constexpr Colors soft{0xD2D2D2, 0x181818, 0xB7B7B7, 0x282828, 0xB76E79, 0x7FA78C, 0xB9A978, 0x7E99B7, 0x9E83A8, 0x7CA3A6, 0xBDBDBD, 0x5D5D5D, 0xC47B85, 0x8DB69A, 0xC5B685, 0x8DA8C5, 0xAD92B7, 0x8BB1B4, 0xDDDDDD};

    explicit TerminalPalette(const Colors& colors);
    [[nodiscard]] static Color color(std::uint32_t rgb);

    Colors m_colors;
};

} // namespace workpane::ui
