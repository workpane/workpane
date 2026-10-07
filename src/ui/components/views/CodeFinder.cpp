#include "ui/components/views/CodeFinder.h"

#include <algorithm>
#include <string>
#include <tuple>

namespace workpane::ui {

// The match being read is the first one at or after the position the search starts from, and the first of the document when none follows it.
void CodeFinder::search(const TextEditor& editor, std::string_view text, bool caseSensitive, bool wholeWord, TextEditor::DocPos from) {
    m_matcher.emplace(text, caseSensitive, wholeWord);
    m_from = from;
    m_nextLine = 0;
    m_matches.clear();
    m_current = 0;
    m_placed = false;
    m_active = true;
    m_bounded = false;
    std::ignore = advance(editor);
}

// Reads the next lines of a search and answers whether the match being read was found in them.
bool CodeFinder::advance(const TextEditor& editor) {
    if (!searching()) {
        return false;
    }

    const bool placed = m_placed;
    const std::size_t last = std::min(editor.GetLineCount(), m_nextLine + linesPerStep);

    for (; m_nextLine < last && !m_bounded; ++m_nextLine) {
        scan(editor, m_nextLine);
        m_bounded = m_matches.size() >= maximumMatches;
    }

    if (!m_bounded && m_nextLine < editor.GetLineCount()) {
        return m_placed && !placed;
    }

    // A search that reached its end without a match after its start reads the first match of the document.
    m_matcher.reset();

    if (!m_placed) {
        m_current = 0;
        m_placed = true;
    }

    return !placed;
}

// Reads the rest of a search at once, which a replacement of every match needs.
void CodeFinder::finish(const TextEditor& editor) {
    while (searching()) {
        std::ignore = advance(editor);
    }
}

// Glyphs are counted from one match to the next, since the matches of a line come in order.
void CodeFinder::scan(const TextEditor& editor, std::size_t line) {
    const std::string written = editor.GetLineText(line);
    std::size_t startByte = 0;
    std::size_t startGlyph = 0;
    std::size_t endByte = 0;
    std::size_t endGlyph = 0;
    // clang-format off
    const auto glyphsUntil = [&written](std::size_t& byte, std::size_t& glyph, std::size_t until) {
        for (; byte < until; ++byte) {
            glyph += (static_cast<unsigned char>(written[byte]) & 0xC0U) != 0x80U ? std::size_t{1} : std::size_t{0};
        }

        return glyph;
    };
    // clang-format on

    for (const TextMatcher::Match& match : m_matcher->find(written, maximumMatches - m_matches.size())) {
        const TextEditor::DocPos start(line, glyphsUntil(startByte, startGlyph, match.start));
        const TextEditor::DocPos end(line, glyphsUntil(endByte, endGlyph, match.end));
        m_matches.emplace_back(start, end);

        if (!m_placed && start >= m_from) {
            m_current = m_matches.size() - 1;
            m_placed = true;
        }
    }
}

void CodeFinder::step(int direction) {
    if (m_matches.empty()) {
        return;
    }

    const auto size = static_cast<long>(m_matches.size());
    m_current = static_cast<std::size_t>(((static_cast<long>(m_current) + direction) % size + size) % size);
}

void CodeFinder::close() {
    m_active = false;
    m_matcher.reset();
    m_matches.clear();
}

bool CodeFinder::active() const {
    return m_active;
}

bool CodeFinder::searching() const {
    return m_active && m_matcher.has_value() && !m_matcher->empty();
}

std::size_t CodeFinder::count() const {
    return m_matches.size();
}

// A count still growing reads as a bound, since more matches may follow.
bool CodeFinder::bounded() const {
    return m_bounded || searching();
}

std::size_t CodeFinder::position() const {
    return m_matches.empty() || !m_placed ? 0 : m_current + 1;
}

std::optional<TextEditor::DocSelection> CodeFinder::current() const {
    if (m_matches.empty() || !m_placed) {
        return std::nullopt;
    }

    return m_matches[m_current];
}

const std::vector<TextEditor::DocSelection>& CodeFinder::matches() const {
    return m_matches;
}

} // namespace workpane::ui
