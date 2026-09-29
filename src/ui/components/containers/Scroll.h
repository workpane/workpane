#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/components/containers/Axis.h"
#include "ui/model/CommonProperties.h"
#include "ui/model/Component.h"
#include "ui/model/Insets.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"

#include <imgui_internal.h>

#include <cstddef>
#include <string_view>

namespace workpane::ui {

class Scroll final : public Component {
  public:
    explicit Scroll(NodeId id);
    [[nodiscard]] std::string_view kind() const override;
    [[nodiscard]] std::size_t childLimit() const override;

  protected:
    [[nodiscard]] Result<void> validateChildren() const override;
    [[nodiscard]] Alignment defaultRowAlignment() const override;
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    static constexpr float followDistance{80.0F};

    [[nodiscard]] ImVec2 measureAcross(RenderContext& context, float availableWidth);
    void renderAcross(RenderContext& context, const ImRect& bounds);

    Insets m_padding;
    Axis m_axis{Axis::Vertical};
    bool m_follow{false};
    bool m_atTop{true};
};

} // namespace workpane::ui
