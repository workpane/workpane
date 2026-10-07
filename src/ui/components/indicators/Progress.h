#pragma once

#include "json/ObjectReader.h"
#include "ui/components/indicators/Tones.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/model/TextValue.h"

#include <imgui_internal.h>

#include <string_view>

namespace workpane::ui {

class Progress final : public Component {
  public:
    explicit Progress(NodeId id);
    [[nodiscard]] std::string_view kind() const override;

  protected:
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    static constexpr float progressHeight{22.0F};

    double m_value{0.0};
    TextValue m_text;
    Tone m_tone{Tone::Accent};
    bool m_showText{true};
};

} // namespace workpane::ui
