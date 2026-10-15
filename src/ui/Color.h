#pragma once

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace workpane::ui {

// An opaque or translucent RGBA color that steps darker and lighter by a percentage of at least one hundred.
struct Color final {
    std::uint8_t red{0};
    std::uint8_t green{0};
    std::uint8_t blue{0};
    std::uint8_t alpha{255};

    [[nodiscard]] ImU32 packed() const;
    [[nodiscard]] ImVec4 vector() const;
    [[nodiscard]] Color withAlpha(float opacity) const;
    [[nodiscard]] Color darker(int factor) const;
    [[nodiscard]] Color lighter(int factor) const;
    [[nodiscard]] std::string hex() const;
    [[nodiscard]] double contrast(const Color& other) const;

    [[nodiscard]] static Color rgb(int red, int green, int blue);
    [[nodiscard]] static std::optional<Color> parse(std::string_view hex);
    [[nodiscard]] static Color blend(Color from, Color to, float amount);

    friend bool operator==(const Color&, const Color&) = default;

  private:
    struct Hsv;

    [[nodiscard]] static Hsv toHsv(const Color& color);
    [[nodiscard]] static Color fromHsv(const Hsv& hsv, std::uint8_t alpha);
    [[nodiscard]] static std::uint8_t channel(double value);
    [[nodiscard]] static double linear(std::uint8_t value);
    [[nodiscard]] double luminance() const;
};

} // namespace workpane::ui
