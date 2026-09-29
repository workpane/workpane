#pragma once

#include "json/ObjectReader.h"
#include "ui/model/CommonProperties.h"
#include "ui/model/Component.h"
#include "ui/model/Insets.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"

#include <imgui_internal.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace workpane::ui {

class Grid final : public Component {
  public:
    explicit Grid(NodeId id);
    [[nodiscard]] std::string_view kind() const override;
    [[nodiscard]] std::size_t childLimit() const override;

  protected:
    [[nodiscard]] Alignment defaultRowAlignment() const override;
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    [[nodiscard]] std::vector<float> rowHeights(RenderContext& context, float cellWidth);
    [[nodiscard]] float cellWidth(RenderContext& context, float available) const;

    std::int64_t m_columns{2};
    float m_columnSpacing{8.0F};
    float m_rowSpacing{8.0F};
    Insets m_padding;
};

} // namespace workpane::ui
