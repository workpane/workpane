#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/model/CommonProperties.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"

#include <imgui_internal.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace workpane::ui {

// A small picture of cells arranged on a grid that the reader presses to choose that arrangement, drawn in the accent while it is the chosen one.
class LayoutSwatch final : public Component {
  public:
    explicit LayoutSwatch(NodeId id);

    [[nodiscard]] std::string_view kind() const override;

  protected:
    [[nodiscard]] Alignment defaultColumnAlignment() const override;
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] Result<void> validate() const override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    struct Cell final {
        std::int64_t column{0};
        std::int64_t row{0};
        std::int64_t columnSpan{1};
        std::int64_t rowSpan{1};
    };

    static constexpr std::int64_t largestGrid{12};
    static constexpr float swatchWidth{48.0F};
    static constexpr float swatchHeight{38.0F};
    static constexpr float pictureWidth{34.0F};
    static constexpr float pictureHeight{20.0F};
    static constexpr float cellGap{1.5F};

    std::int64_t m_columns{1};
    std::int64_t m_rows{1};
    std::vector<Cell> m_cells;
    bool m_checked{false};
};

} // namespace workpane::ui
