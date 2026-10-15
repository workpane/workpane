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

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::ui {

struct Texture;

// Formatted reading text written in Markdown, which reports a pressed link as an event instead of opening it on its own.
class Markdown final : public Component {
  public:
    explicit Markdown(NodeId id);
    [[nodiscard]] std::string_view kind() const override;
    [[nodiscard]] Result<void> command(RenderContext& context, std::string_view name, const json::Json& arguments) override;

  protected:
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    static constexpr float codeSpanPadding{3.0F};
    static constexpr float codeSpanRadius{3.0F};
    static constexpr std::size_t maximumImageBytes{(4U << 20U) * 4U / 3U + 128U};

    struct ImageResource final {
        std::string file;
        std::string data;
        bool requested{false};
        bool failed{false};
        std::int64_t request{0};
        ImVec2 size;
    };

    [[nodiscard]] static bool filePath(std::string_view text);
    static void linkPaths(std::vector<markdown::Span>& spans);

    [[nodiscard]] const markdown::Layout& layout(RenderContext& context, float limit);
    [[nodiscard]] Color ink(RenderContext& context, markdown::Ink role, bool hovered) const;
    [[nodiscard]] std::size_t textPosition(RenderContext& context, const markdown::Layout& laid, ImVec2 point) const;
    void select(RenderContext& context, const ImRect& bounds, const markdown::Layout& laid);
    static void gatherImages(const std::vector<markdown::Span>& spans, std::set<std::string, std::less<>>& sources);
    void prepareImages(RenderContext& context);
    [[nodiscard]] const Texture* imageTexture(RenderContext& context, const std::string& source) const;

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
    bool m_pathLinks{false};
    bool m_parsedPathLinks{false};
    std::size_t m_anchor{0};
    std::size_t m_caret{0};
    bool m_selecting{false};
    std::string m_imageRoot;
    std::string m_parsedImageRoot;
    std::map<std::string, ImageResource, std::less<>> m_images;
    markdown::LayoutBuilder::ImageSizes m_imageSizes;
    std::int64_t m_nextImageRequest{0};
};

} // namespace workpane::ui
