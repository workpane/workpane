#include "ui/components/terminal/TerminalFinder.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <tuple>
#include <utility>

namespace workpane::ui {

void TerminalFinder::search(const TerminalScreen& screen, std::string_view text, bool caseSensitive, bool wholeWord) {
    m_text = std::string(text);
    m_caseSensitive = caseSensitive;
    m_wholeWord = wholeWord;
    m_active = true;
    m_fresh = true;
    m_current = 0;
    m_matches.clear();
    m_scanned = 0;
    std::ignore = refresh(screen);
}

// The matches of lines the history dropped are forgotten, and every line from the first one that may have changed is read again, a bounded number of lines of the history at a time.
// A history that gave lines back to a taller screen is read again from the first of them, and the answer tells when a new search chose the match it reads.
bool TerminalFinder::refresh(const TerminalScreen& screen) {
    const std::optional<Match> previous = current();

    if (!m_active || m_text.empty()) {
        m_matches.clear();
        m_bounded = false;
        m_searching = false;
        m_fresh = false;
        return false;
    }

    const std::uint64_t first = screen.dropped();
    const std::uint64_t shown = first + static_cast<std::uint64_t>(screen.history());
    const std::uint64_t from = std::max(first, std::min(m_scanned, shown));
    const std::uint64_t until = std::min(shown, from + linesPerStep);
    // clang-format off
    std::erase_if(m_matches, [first, from](const Match& match) { return match.from.line < first || match.from.line >= from; });
    // clang-format on
    m_bounded = false;
    m_scanned = until;
    m_searching = until < shown;
    const TextMatcher matcher(m_text, m_caseSensitive, m_wholeWord);
    const long last = m_searching ? static_cast<long>(until - first) : screen.lines();

    // A line that reached the bound may hold more matches, so the next refresh reads it again.
    for (long line = static_cast<long>(from - first); line < last; ++line) {
        scan(screen, line, matcher);

        if (m_matches.size() >= maximumMatches) {
            m_bounded = true;
            m_searching = false;
            m_scanned = std::min(m_scanned, first + static_cast<std::uint64_t>(line));
            break;
        }
    }

    // A new search reads the newest match once it reached the end, and a refresh keeps the match being read where it was.
    if (m_fresh && !m_searching) {
        m_fresh = false;
        m_current = m_matches.empty() ? 0 : m_matches.size() - 1;
        return true;
    }

    // clang-format off
    const auto kept = previous.has_value() ? std::ranges::find_if(m_matches, [&previous](const Match& match) { return match.from == previous->from; }) : m_matches.end();
    // clang-format on
    m_current = kept != m_matches.end() ? static_cast<std::size_t>(kept - m_matches.begin()) : std::min(m_current, m_matches.empty() ? 0 : m_matches.size() - 1);

    return false;
}

// Each line is read cell by cell, so a match is named by the columns of the cells it covers, wide characters included.
void TerminalFinder::scan(const TerminalScreen& screen, long line, const TextMatcher& matcher) {
    std::string row;
    std::vector<int> columns;

    for (int column = 0; column < screen.columns(); ++column) {
        const std::string cell = screen.text(line, column, column + 1);
        row += cell;
        columns.insert(columns.end(), cell.size(), column);
    }

    const std::uint64_t stable = screen.dropped() + static_cast<std::uint64_t>(line);

    for (const TextMatcher::Match& match : matcher.find(row, maximumMatches - m_matches.size())) {
        m_matches.push_back({{stable, columns[match.start]}, {stable, columns[match.end - 1]}});
    }
}

void TerminalFinder::step(int direction) {
    if (m_matches.empty()) {
        return;
    }

    const auto size = static_cast<long>(m_matches.size());
    m_current = static_cast<std::size_t>(((static_cast<long>(m_current) + direction) % size + size) % size);
}

void TerminalFinder::close() {
    m_active = false;
    m_matches.clear();
    m_text.clear();
}

bool TerminalFinder::active() const {
    return m_active;
}

bool TerminalFinder::searching() const {
    return m_active && m_searching;
}

std::size_t TerminalFinder::count() const {
    return m_matches.size();
}

// A count still growing reads as a bound, since more matches may follow.
bool TerminalFinder::bounded() const {
    return m_bounded || searching();
}

std::optional<TerminalFinder::Match> TerminalFinder::current() const {
    if (m_matches.empty() || m_fresh) {
        return std::nullopt;
    }

    return m_matches[m_current];
}

std::size_t TerminalFinder::position() const {
    return m_matches.empty() || m_fresh ? 0 : m_current + 1;
}

// Matches are ordered by line and column, so the ones that could hold a cell are found by bisection.
bool TerminalFinder::marks(TerminalPoint point) const {
    // clang-format off
    const auto after = std::ranges::upper_bound(m_matches, point, {}, [](const Match& match) { return match.from; });
    // clang-format on

    if (after == m_matches.begin()) {
        return false;
    }

    const Match& candidate = *std::prev(after);
    return point >= candidate.from && point <= candidate.to;
}

} // namespace workpane::ui
