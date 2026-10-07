#pragma once

#include "ui/markdown/Block.h"
#include "ui/markdown/Layout.h"
#include "ui/markdown/Run.h"
#include "ui/markdown/Span.h"
#include "ui/theme/FontRole.h"

#include <array>
#include <string_view>
#include <vector>

namespace workpane::ui {
class RenderContext;
}

namespace workpane::ui::markdown {

// Places the blocks of a document into runs of text and shapes, breaking lines between words within the width it is given.
class LayoutBuilder final {
  public:
    LayoutBuilder(const RenderContext& context, FontRole body, float limit);
    [[nodiscard]] Layout build(const std::vector<Block>& blocks);

  private:
    static constexpr float headingGap{1.1F};
    static constexpr float blockGap{0.75F};
    static constexpr float itemGap{0.35F};
    static constexpr float levelIndent{1.5F};
    static constexpr float markerColumn{1.5F};
    static constexpr float markerSpacing{0.45F};
    static constexpr float quoteIndent{1.0F};
    static constexpr float quoteBar{3.0F};
    static constexpr float codePadding{0.75F};
    static constexpr float ruleHeight{1.0F};
    static constexpr int tabWidth{4};
    static constexpr std::array<float, 3> headingScales{1.6F, 1.35F, 1.15F};

    [[nodiscard]] float gap(const Block& previous, const Block& block) const;
    [[nodiscard]] FontRole heading(int level) const;
    [[nodiscard]] static FontRole face(const Span& span, FontRole role);
    [[nodiscard]] float flow(const std::vector<Span>& spans, float left, float top, FontRole role, Ink ink);
    [[nodiscard]] float item(const Block& block, float top);
    [[nodiscard]] float quote(const Block& block, float top);
    [[nodiscard]] float code(const Block& block, float top);
    [[nodiscard]] float rule(float top);
    void place(std::string_view text, FontRole font, Ink ink, bool code, int link);
    void append(std::string_view text, float space, float width, FontRole font, Ink ink, bool code, int link);
    void breakLine();

    const RenderContext& m_context;
    FontRole m_body;
    float m_unit{0.0F};
    Layout m_layout;
    float m_left{0.0F};
    float m_x{0.0F};
    float m_top{0.0F};
    float m_line{0.0F};
    float m_widest{0.0F};
    bool m_space{false};
    int m_current{-1};
};

} // namespace workpane::ui::markdown
