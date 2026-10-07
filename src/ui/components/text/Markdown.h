#pragma once

#include "json/ObjectReader.h"
#include "ui/Color.h"
#include "ui/markdown/Block.h"
#include "ui/markdown/Layout.h"
#include "ui/markdown/LayoutBuilder.h"
#include "ui/markdown/Parser.h"
#include "ui/markdown/Run.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/model/TextValue.h"
#include "ui/theme/Theme.h"

#include <imgui_internal.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::ui {

// Formatted reading text written in Markdown, which reports a pressed link as an event instead of opening it on its own.
class Markdown final : public Component {
  public:
    explicit Markdown(NodeId id);
    [[nodiscard]] std::string_view kind() const override;

  protected:
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    static constexpr float codeSpanPadding{3.0F};
    static constexpr float codeSpanRadius{3.0F};

    [[nodiscard]] const markdown::Layout& layout(RenderContext& context, float limit);
    [[nodiscard]] Color ink(RenderContext& context, markdown::Ink role, bool hovered) const;

    TextValue m_text;
    std::optional<float> m_fontSize;
    std::optional<ThemeColor> m_color;
    std::string m_source;
    std::vector<markdown::Block> m_blocks;
    markdown::Layout m_layout;
    float m_layoutScale{0.0F};
    bool m_layoutValid{false};
    bool m_breaks{false};
    bool m_parsedBreaks{false};
};

} // namespace workpane::ui
