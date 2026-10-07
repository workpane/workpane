#include "ui/Color.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace workpane::ui {

struct Color::Hsv final {
    double hue{-1.0};
    double saturation{0.0};
    double value{0.0};
};

Color::Hsv Color::toHsv(const Color& color) {
    const double red = color.red / 255.0;
    const double green = color.green / 255.0;
    const double blue = color.blue / 255.0;
    const double maximum = std::max({red, green, blue});
    const double minimum = std::min({red, green, blue});
    const double delta = maximum - minimum;

    if (delta <= 0.0) {
        return {-1.0, 0.0, maximum};
    }

    double hue = 0.0;

    if (red == maximum) {
        hue = (green - blue) / delta;
    } else if (green == maximum) {
        hue = 2.0 + (blue - red) / delta;
    } else {
        hue = 4.0 + (red - green) / delta;
    }

    hue *= 60.0;

    return {hue < 0.0 ? hue + 360.0 : hue, delta / maximum, maximum};
}

Color Color::fromHsv(const Hsv& hsv, std::uint8_t alpha) {
    if (hsv.saturation <= 0.0 || hsv.hue < 0.0) {
        const std::uint8_t gray = channel(hsv.value);
        return {gray, gray, gray, alpha};
    }

    const double sector = hsv.hue >= 360.0 ? 0.0 : hsv.hue / 60.0;
    const int index = static_cast<int>(sector);
    const double fraction = sector - index;
    const double low = hsv.value * (1.0 - hsv.saturation);
    const double falling = hsv.value * (1.0 - hsv.saturation * fraction);
    const double rising = hsv.value * (1.0 - hsv.saturation * (1.0 - fraction));
    const std::array<std::array<double, 3>, 6> sectors{{{hsv.value, rising, low}, {falling, hsv.value, low}, {low, hsv.value, rising}, {low, falling, hsv.value}, {rising, low, hsv.value}, {hsv.value, low, falling}}};
    const auto& rgb = sectors.at(static_cast<std::size_t>(index));
    return {channel(rgb[0]), channel(rgb[1]), channel(rgb[2]), alpha};
}

std::uint8_t Color::channel(double value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
}

ImU32 Color::packed() const {
    return IM_COL32(red, green, blue, alpha);
}

ImVec4 Color::vector() const {
    return {red / 255.0F, green / 255.0F, blue / 255.0F, alpha / 255.0F};
}

Color Color::withAlpha(float opacity) const {
    return {red, green, blue, static_cast<std::uint8_t>(std::lround(std::clamp(opacity, 0.0F, 1.0F) * 255.0F))};
}

Color Color::darker(int factor) const {
    Hsv hsv = toHsv(*this);
    hsv.value = hsv.value * 100.0 / factor;

    return fromHsv(hsv, alpha);
}

// A value pushed past white gives up saturation instead, so a saturated accent still grows lighter.
Color Color::lighter(int factor) const {
    Hsv hsv = toHsv(*this);
    hsv.value = hsv.value * factor / 100.0;

    if (hsv.value > 1.0) {
        hsv.saturation = std::max(0.0, hsv.saturation - (hsv.value - 1.0));
        hsv.value = 1.0;
    }

    return fromHsv(hsv, alpha);
}

double Color::linear(std::uint8_t value) {
    const double unit = static_cast<double>(value) / 255.0;
    return unit <= 0.04045 ? unit / 12.92 : std::pow((unit + 0.055) / 1.055, 2.4);
}

// The relative luminance of the Web Content Accessibility Guidelines, from the linear light of each channel.
double Color::luminance() const {
    return 0.2126 * linear(red) + 0.7152 * linear(green) + 0.0722 * linear(blue);
}

// The contrast ratio of the Web Content Accessibility Guidelines, from one for equal colors to twenty one for black on white.
double Color::contrast(const Color& other) const {
    const double first = luminance();
    const double second = other.luminance();
    return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
}

std::string Color::hex() const {
    std::array<char, 8> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "#%02x%02x%02x", red, green, blue);

    return buffer.data();
}

Color Color::rgb(int red, int green, int blue) {
    return {static_cast<std::uint8_t>(red), static_cast<std::uint8_t>(green), static_cast<std::uint8_t>(blue), 255};
}

Color Color::blend(Color from, Color to, float amount) {
    const float clamped = std::clamp(amount, 0.0F, 1.0F);
    // clang-format off
    const auto mix = [clamped](std::uint8_t start, std::uint8_t end) { return static_cast<std::uint8_t>(std::lround(start + (end - start) * clamped)); };
    // clang-format on
    return {mix(from.red, to.red), mix(from.green, to.green), mix(from.blue, to.blue), mix(from.alpha, to.alpha)};
}

// A color is written as a number sign followed by six hexadecimal digits, which is how colors cross from Lua.
std::optional<Color> Color::parse(std::string_view hex) {
    if (hex.size() != 7 || hex.front() != '#') {
        return std::nullopt;
    }

    std::array<unsigned int, 3> channels{};

    for (std::size_t index = 0; index < channels.size(); ++index) {
        const char* begin = hex.data() + 1 + index * 2;
        const auto result = std::from_chars(begin, begin + 2, channels[index], 16);

        if (result.ec != std::errc{} || result.ptr != begin + 2) {
            return std::nullopt;
        }
    }

    return Color::rgb(static_cast<int>(channels[0]), static_cast<int>(channels[1]), static_cast<int>(channels[2]));
}

} // namespace workpane::ui
