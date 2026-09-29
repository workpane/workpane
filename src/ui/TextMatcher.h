#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::ui {

// Finds a text in a line, ignoring the case of ASCII letters unless asked to match it and keeping only whole words when asked.
// A match is named by the bytes it covers, and matches may overlap, as a search that steps one character at a time finds them.
class TextMatcher final {
  public:
    struct Match final {
        std::size_t start{0};
        std::size_t end{0};
    };

    TextMatcher(std::string_view needle, bool caseSensitive, bool wholeWord);

    [[nodiscard]] bool empty() const;
    [[nodiscard]] std::vector<Match> find(std::string_view line, std::size_t limit) const;

  private:
    [[nodiscard]] static std::string folded(std::string_view text, bool caseSensitive);
    [[nodiscard]] static bool boundary(std::string_view text, std::size_t index);

    std::string m_needle;
    bool m_caseSensitive;
    bool m_wholeWord;
};

} // namespace workpane::ui
