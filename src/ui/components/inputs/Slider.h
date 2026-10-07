#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"

#include <imgui_internal.h>

#include <cstdint>
#include <string_view>

namespace workpane::ui {

class Slider final : public Component {
  public:
    explicit Slider(NodeId id);
    [[nodiscard]] std::string_view kind() const override;

  protected:
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] Result<void> validate() const override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    static constexpr float sliderValueSpacing{10.0F};

    double m_value{0.0};
    double m_minimum{0.0};
    double m_maximum{100.0};
    double m_step{1.0};
    std::int64_t m_decimals{0};
    bool m_showValue{true};
};

} // namespace workpane::ui
