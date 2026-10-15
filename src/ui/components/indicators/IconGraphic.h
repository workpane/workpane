#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/IconCatalog.h"
#include "ui/model/CommonProperties.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/theme/Theme.h"

#include <imgui_internal.h>

#include <optional>
#include <string_view>

namespace workpane::ui {

class IconGraphic final : public Component {
  public:
    explicit IconGraphic(NodeId id);
    [[nodiscard]] std::string_view kind() const override;

  protected:
    [[nodiscard]] Alignment defaultColumnAlignment() const override;
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] Result<void> validate() const override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    std::optional<Icon> m_icon;
    std::optional<ThemeColor> m_color;
    float m_size{16.0F};
};

} // namespace workpane::ui
