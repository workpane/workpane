#pragma once

#include "ui/TextMatcher.h"
#include "ui/components/terminal/TerminalPoint.h"
#include "ui/components/terminal/TerminalScreen.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::ui {

// Finds a text over the rows on screen and the history behind them, marks every match and steps through them, wrapping past the last one.
// Counting stops at a bound, and the finder says when it stopped there.
// A refresh reads again only the rows on screen and the lines the history took since the last one, since a line in the history never changes.
// A search reads a bounded number of lines of the history at each refresh, so a long history never holds a frame, and the match it reads is chosen once it reached the rows on screen.
class TerminalFinder final {
  public:
    struct Match final {
        TerminalPoint from;
        TerminalPoint to;
    };

    void search(const TerminalScreen& screen, std::string_view text, bool caseSensitive, bool wholeWord);
    [[nodiscard]] bool refresh(const TerminalScreen& screen);
    void step(int direction);
    void close();

    [[nodiscard]] bool active() const;
    [[nodiscard]] bool searching() const;
    [[nodiscard]] std::size_t count() const;
    [[nodiscard]] bool bounded() const;
    [[nodiscard]] std::optional<Match> current() const;
    [[nodiscard]] std::size_t position() const;
    [[nodiscard]] bool marks(TerminalPoint point) const;

  private:
    static constexpr std::size_t maximumMatches{999};
    static constexpr std::uint64_t linesPerStep{2000};

    void scan(const TerminalScreen& screen, long line, const TextMatcher& matcher);

    std::string m_text;
    bool m_caseSensitive{false};
    bool m_wholeWord{false};
    bool m_active{false};
    bool m_bounded{false};
    bool m_searching{false};
    bool m_fresh{false};
    std::vector<Match> m_matches;
    std::size_t m_current{0};
    std::uint64_t m_scanned{0};
};

} // namespace workpane::ui
