#include "ui/TextMatcher.h"

namespace workpane::ui {

TextMatcher::TextMatcher(std::string_view needle, bool caseSensitive, bool wholeWord) : m_needle(folded(needle, caseSensitive)), m_caseSensitive(caseSensitive), m_wholeWord(wholeWord) {}

bool TextMatcher::empty() const {
    return m_needle.empty();
}

std::vector<TextMatcher::Match> TextMatcher::find(std::string_view line, std::size_t limit) const {
    std::vector<Match> matches;

    if (m_needle.empty()) {
        return matches;
    }

    const std::string haystack = folded(line, m_caseSensitive);

    for (std::size_t found = haystack.find(m_needle); found != std::string::npos && matches.size() < limit; found = haystack.find(m_needle, found + 1)) {
        const std::size_t end = found + m_needle.size();

        if (!m_wholeWord || (boundary(haystack, found) && boundary(haystack, end))) {
            matches.push_back({found, end});
        }
    }

    return matches;
}

std::string TextMatcher::folded(std::string_view text, bool caseSensitive) {
    std::string result(text);

    if (caseSensitive) {
        return result;
    }

    for (char& character : result) {
        character = character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
    }

    return result;
}

// A word is made of letters, digits and underscores, and every byte of a character beyond ASCII counts as a letter, so an accented word is never split.
bool TextMatcher::boundary(std::string_view text, std::size_t index) {
    // clang-format off
    const auto word = [&text](std::size_t at) { const auto byte = static_cast<unsigned char>(text[at]); return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || byte == '_' || byte >= 0x80U; };
    // clang-format on
    return index == 0 || index >= text.size() || !word(index - 1) || !word(index);
}

} // namespace workpane::ui
