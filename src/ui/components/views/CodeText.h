#pragma once

#include <TextEditor.h>
#include <imgui_internal.h>

#include <cstddef>
#include <string_view>
#include <vector>

namespace workpane::ui {

// The editor of the library with the syntax roles of ranges a plugin names painted over the coloring of its language.
class CodeText final : public TextEditor {
  public:
    struct Highlight final {
        std::size_t line;
        std::size_t column;
        std::size_t length;
        TextEditor::Color role;
    };

    void load(std::string_view text);
    void settleLoad();
    [[nodiscard]] bool holds(TextEditor::DocPos position) const;
    void paint(const std::vector<Highlight>& highlights, bool recolor);
    [[nodiscard]] float textLeft() const;
    [[nodiscard]] bool changePending() const;
    [[nodiscard]] double secondsUntilChange() const;
    void settleChange();
    void replaceEvery(const std::vector<TextEditor::DocSelection>& ranges, std::string_view text);
    [[nodiscard]] std::vector<ImRect> rangeRects(TextEditor::DocPos start, TextEditor::DocPos end) const;

  private:
    static constexpr std::size_t rowEndColumn{1U << 30U};
    static constexpr double reportMargin{0.001};

    void recolorDocument();

    bool m_loaded{false};
};

} // namespace workpane::ui
