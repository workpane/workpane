#include "ui/components/terminal/TerminalSelection.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

namespace workpane::ui {

void TerminalSelection::begin(TerminalPoint point, bool rectangular) {
    m_anchor = point;
    m_extent = point;
    m_rectangular = rectangular;
}

void TerminalSelection::extend(TerminalPoint point) {
    if (m_anchor.has_value()) {
        m_extent = point;
    }
}

void TerminalSelection::select(TerminalPoint from, TerminalPoint to) {
    m_anchor = from;
    m_extent = to;
    m_rectangular = false;
}

// A word runs across letters, digits and the punctuation of paths and addresses, so a double click takes a whole path at once.
void TerminalSelection::word(const TerminalScreen& screen, TerminalPoint point) {
    const long line = static_cast<long>(point.line - screen.dropped());

    if (line < 0 || line >= screen.lines() || !wordCharacter(screen.text(line, point.column, point.column + 1))) {
        begin(point, false);
        return;
    }

    int from = point.column;
    int to = from;

    while (from > 0 && wordCharacter(screen.text(line, from - 1, from))) {
        --from;
    }

    while (to + 1 < screen.columns() && wordCharacter(screen.text(line, to + 1, to + 2))) {
        ++to;
    }

    select({point.line, from}, {point.line, to});
}

// A line is the whole line the program wrote, so a line it wrapped across rows is taken with every row it spans.
void TerminalSelection::line(const TerminalScreen& screen, TerminalPoint point) {
    long first = static_cast<long>(point.line - screen.dropped());
    long last = first;

    if (first < 0 || first >= screen.lines()) {
        begin(point, false);
        return;
    }

    while (first > 0 && screen.wraps(first - 1)) {
        --first;
    }

    while (last + 1 < screen.lines() && screen.wraps(last)) {
        ++last;
    }

    select({screen.dropped() + static_cast<std::uint64_t>(first), 0}, {screen.dropped() + static_cast<std::uint64_t>(last), screen.columns() - 1});
}

void TerminalSelection::all(const TerminalScreen& screen) {
    select({screen.dropped(), 0}, {screen.dropped() + static_cast<std::uint64_t>(screen.lines() - 1), screen.columns() - 1});
}

void TerminalSelection::clear() {
    m_anchor.reset();
}

bool TerminalSelection::empty() const {
    return !m_anchor.has_value() || *m_anchor == m_extent;
}

bool TerminalSelection::contains(TerminalPoint point) const {
    if (empty()) {
        return false;
    }

    const auto [from, to] = ordered();

    if (m_rectangular) {
        return point.line >= from.line && point.line <= to.line && point.column >= std::min(from.column, to.column) && point.column <= std::max(from.column, to.column);
    }

    return point >= from && point <= to;
}

// Copying joins the lines the program wrapped and drops the blanks that padded the others to the width of the terminal.
std::string TerminalSelection::text(const TerminalScreen& screen) const {
    if (empty()) {
        return {};
    }

    const auto [from, to] = ordered();
    std::string copied;

    for (std::uint64_t stable = std::max(from.line, screen.dropped()); stable <= to.line; ++stable) {
        const long line = static_cast<long>(stable - screen.dropped());

        if (line >= screen.lines()) {
            break;
        }

        const int first = m_rectangular ? std::min(from.column, to.column) : stable == from.line ? from.column : 0;
        const int last = m_rectangular ? std::max(from.column, to.column) + 1 : stable == to.line ? to.column + 1 : screen.columns();
        const bool wraps = !m_rectangular && screen.wraps(line) && stable != to.line;
        std::string row = screen.text(line, first, last);

        if (!wraps) {
            row.erase(row.find_last_not_of(' ') + 1);
        }

        copied += row;

        if (!wraps && stable != to.line) {
            copied += '\n';
        }
    }

    return copied;
}

bool TerminalSelection::wordCharacter(const std::string& text) {
    if (text.empty() || text == " ") {
        return false;
    }

    const auto first = static_cast<unsigned char>(text.front());
    return first >= 0x80U || std::isalnum(first) != 0 || wordPunctuation.find(text.front()) != std::string_view::npos;
}

std::pair<TerminalPoint, TerminalPoint> TerminalSelection::ordered() const {
    return *m_anchor <= m_extent ? std::make_pair(*m_anchor, m_extent) : std::make_pair(m_extent, *m_anchor);
}

} // namespace workpane::ui
