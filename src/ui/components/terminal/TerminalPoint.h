#pragma once

#include <compare>
#include <cstdint>

namespace workpane::ui {

// A cell of the terminal named by its line counted since the terminal started, so it keeps naming the same text while the history scrolls.
struct TerminalPoint final {
    std::uint64_t line{0};
    int column{0};

    friend auto operator<=>(const TerminalPoint&, const TerminalPoint&) = default;
};

} // namespace workpane::ui
