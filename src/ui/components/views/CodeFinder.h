#pragma once

#include "ui/TextMatcher.h"

#include <TextEditor.h>

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace workpane::ui {

// Finds a text in the lines of an editor, steps through the matches wrapping past the last one, and counts them up to a bound.
// A match is named by the glyphs it covers, so characters of several bytes count once, as the editor counts them.
// A search reads a bounded number of lines at each step, so a large document never holds a frame, and it goes on at the next step until it reaches the end.
class CodeFinder final {
  public:
    void search(const TextEditor& editor, std::string_view text, bool caseSensitive, bool wholeWord, TextEditor::DocPos from);
    [[nodiscard]] bool advance(const TextEditor& editor);
    void finish(const TextEditor& editor);
    void step(int direction);
    void close();

    [[nodiscard]] bool active() const;
    [[nodiscard]] bool searching() const;
    [[nodiscard]] std::size_t count() const;
    [[nodiscard]] bool bounded() const;
    [[nodiscard]] std::size_t position() const;
    [[nodiscard]] std::optional<TextEditor::DocSelection> current() const;
    [[nodiscard]] const std::vector<TextEditor::DocSelection>& matches() const;

  private:
    static constexpr std::size_t maximumMatches{9999};
    static constexpr std::size_t linesPerStep{4000};

    void scan(const TextEditor& editor, std::size_t line);

    std::optional<TextMatcher> m_matcher;
    TextEditor::DocPos m_from;
    std::size_t m_nextLine{0};
    std::vector<TextEditor::DocSelection> m_matches;
    std::size_t m_current{0};
    bool m_placed{false};
    bool m_active{false};
    bool m_bounded{false};
};

} // namespace workpane::ui
