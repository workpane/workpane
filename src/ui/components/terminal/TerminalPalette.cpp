#include "ui/components/terminal/TerminalPalette.h"

namespace workpane::ui {

TerminalPalette::TerminalPalette(const Colors& colors) : m_colors(colors) {}

std::optional<TerminalPalette> TerminalPalette::named(std::string_view name) {
    if (name == "vivid") {
        return TerminalPalette(vivid);
    }

    if (name == "balanced") {
        return TerminalPalette(balanced);
    }

    if (name == "soft") {
        return TerminalPalette(soft);
    }

    return std::nullopt;
}

Color TerminalPalette::foreground() const {
    return color(m_colors[0]);
}

Color TerminalPalette::background() const {
    return color(m_colors[1]);
}

Color TerminalPalette::cursor() const {
    return color(m_colors[2]);
}

// The sixteen ANSI colors follow the foreground, the background and the cursor in each scheme.
Color TerminalPalette::ansi(std::size_t index) const {
    return color(m_colors[3 + index]);
}

Color TerminalPalette::color(std::uint32_t rgb) {
    return Color::rgb(static_cast<int>((rgb >> 16U) & 0xFFU), static_cast<int>((rgb >> 8U) & 0xFFU), static_cast<int>(rgb & 0xFFU));
}

} // namespace workpane::ui
