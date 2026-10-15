#pragma once

#include "json/ObjectReader.h"
#include "ui/components/indicators/Tones.h"
#include "ui/model/CommonProperties.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"

#include <imgui_internal.h>

#include <string_view>

namespace workpane::ui {

class StatusIndicator final : public Component {
  public:
    explicit StatusIndicator(NodeId id);
    [[nodiscard]] std::string_view kind() const override;

  protected:
    [[nodiscard]] Alignment defaultColumnAlignment() const override;
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    Tone m_tone{Tone::Accent};
    float m_size{7.0F};
};

} // namespace workpane::ui
