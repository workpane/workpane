#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/components/containers/Axis.h"
#include "ui/model/CommonProperties.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"

#include <imgui_internal.h>

#include <cstddef>
#include <string_view>

namespace workpane::ui {

class Splitter final : public Component {
  public:
    explicit Splitter(NodeId id);
    [[nodiscard]] std::string_view kind() const override;
    [[nodiscard]] std::size_t childLimit() const override;
    [[nodiscard]] Result<void> validateChildren() const override;

  protected:
    [[nodiscard]] Alignment defaultRowAlignment() const override;
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    static constexpr float splitterHitWidth{6.0F};

    [[nodiscard]] float firstExtent(const RenderContext& context, float total) const;

    Axis m_axis{Axis::Horizontal};
    double m_ratio{0.5};
    float m_firstMinimum{80.0F};
    float m_secondMinimum{80.0F};
    bool m_dragging{false};
};

} // namespace workpane::ui
