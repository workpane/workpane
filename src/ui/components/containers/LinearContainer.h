#pragma once

#include "json/ObjectReader.h"
#include "ui/components/containers/Axis.h"
#include "ui/components/containers/Borders.h"
#include "ui/model/CommonProperties.h"
#include "ui/model/Component.h"
#include "ui/model/Insets.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/theme/Theme.h"

#include <imgui_internal.h>

#include <cstddef>
#include <optional>
#include <vector>

namespace workpane::ui {

enum class Justify { Start, Center, End, SpaceBetween };

// Places its children one after another along one axis, giving what is left to the children that grow.
class LinearContainer : public Component {
  public:
    LinearContainer(NodeId id, Axis axis);

    [[nodiscard]] std::size_t childLimit() const override;

  protected:
    [[nodiscard]] Alignment defaultRowAlignment() const override;
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;
    virtual void paintSurface(RenderContext& context, const ImRect& bounds);
    void paintBorders(RenderContext& context, const ImRect& bounds) const;
    [[nodiscard]] virtual Insets contentInsets(RenderContext& context) const;

    [[nodiscard]] ImVec2 measureVertical(RenderContext& context, float availableWidth);
    [[nodiscard]] ImVec2 measureHorizontal(RenderContext& context, float availableWidth);
    void renderVertical(RenderContext& context, const ImRect& content);
    void renderHorizontal(RenderContext& context, const ImRect& content);
    [[nodiscard]] std::vector<Component*> fitting(RenderContext& context, float available);
    [[nodiscard]] std::vector<float> horizontalWidths(RenderContext& context, const std::vector<Component*>& children, float available);
    [[nodiscard]] static std::vector<float> verticalHeights(const RenderContext& context, const std::vector<Component*>& children, const std::vector<ImVec2>& sizes, float free);

    Axis m_axis;
    Insets m_padding;
    float m_spacing{0.0F};
    Justify m_justify{Justify::Start};
    std::optional<ThemeColor> m_background;
    Borders m_borders;
};

} // namespace workpane::ui
