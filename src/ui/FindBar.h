#pragma once

#include "ui/model/RenderContext.h"

#include <imgui_internal.h>

#include <cstddef>
#include <string>

namespace workpane::ui {

// The bar a view opens over its top right corner to find a text: the query with the count of its matches, case and whole words, the steps between matches and closing.
// A view that may be written adds a second row that replaces the current match or every match.
class FindBar final {
  public:
    struct Outcome final {
        bool searched{false};
        int step{0};
        bool closed{false};
        bool replaced{false};
        bool replacedAll{false};
    };

    struct Count final {
        std::size_t total{0};
        std::size_t position{0};
        bool bounded{false};
    };

    [[nodiscard]] static int stepping();
    [[nodiscard]] static ImRect area(const RenderContext& context, const ImRect& bounds, bool replacing);

    void focus();
    void release();
    void setQuery(std::string text, bool caseSensitive, bool wholeWord);
    [[nodiscard]] const std::string& text() const;
    [[nodiscard]] bool caseSensitive() const;
    [[nodiscard]] bool wholeWord() const;
    [[nodiscard]] const std::string& replacement() const;
    [[nodiscard]] bool typing() const;
    [[nodiscard]] Outcome draw(RenderContext& context, const ImRect& bounds, const Count& count, bool replacing);

  private:
    static constexpr float barWidth{360.0F};
    static constexpr float barGap{4.0F};

    [[nodiscard]] static std::string counted(const RenderContext& context, const Count& count);

    void drawReplace(RenderContext& context, const ImRect& row, Outcome& outcome);

    std::string m_text;
    std::string m_replacement;
    bool m_caseSensitive{false};
    bool m_wholeWord{false};
    bool m_typing{false};
    bool m_focusRequest{false};
};

} // namespace workpane::ui
