#pragma once

#include "ui/components/terminal/TerminalPoint.h"
#include "ui/components/terminal/TerminalScreen.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace workpane::ui {

// The text the reader selected in a terminal, held by the emulator lines rather than by the rectangle on screen, so it follows the history as it scrolls.
// A rectangular selection takes the same columns of every line it crosses instead of running from one point to the other.
class TerminalSelection final {
  public:
    void begin(TerminalPoint point, bool rectangular);
    void extend(TerminalPoint point);
    void select(TerminalPoint from, TerminalPoint to);
    void word(const TerminalScreen& screen, TerminalPoint point);
    void line(const TerminalScreen& screen, TerminalPoint point);
    void all(const TerminalScreen& screen);
    void clear();

    [[nodiscard]] bool empty() const;
    [[nodiscard]] bool contains(TerminalPoint point) const;
    [[nodiscard]] std::string text(const TerminalScreen& screen) const;

  private:
    static constexpr std::string_view wordPunctuation{"-_./~:@%+=?&#"};

    [[nodiscard]] static bool wordCharacter(const std::string& text);
    [[nodiscard]] std::pair<TerminalPoint, TerminalPoint> ordered() const;

    std::optional<TerminalPoint> m_anchor;
    TerminalPoint m_extent;
    bool m_rectangular{false};
};

} // namespace workpane::ui
