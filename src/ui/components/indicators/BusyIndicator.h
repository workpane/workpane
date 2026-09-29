#pragma once

#include "json/ObjectReader.h"
#include "ui/model/CommonProperties.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"

#include <imgui_internal.h>

#include <string_view>

namespace workpane::ui {

// Work that is still running says so, because a silence reads as a finished run.
class BusyIndicator final : public Component {
  public:
    explicit BusyIndicator(NodeId id);
    [[nodiscard]] std::string_view kind() const override;

  protected:
    [[nodiscard]] Alignment defaultColumnAlignment() const override;
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    float m_size{20.0F};
    bool m_running{true};
};

} // namespace workpane::ui
