#pragma once

#include "json/ObjectReader.h"
#include "ui/IconCatalog.h"
#include "ui/WidgetHelper.h"
#include "ui/model/CommonProperties.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/model/TextValue.h"

#include <imgui_internal.h>

#include <optional>
#include <string_view>

namespace workpane::ui {

class Button final : public Component {
  public:
    explicit Button(NodeId id);
    [[nodiscard]] std::string_view kind() const override;

  protected:
    [[nodiscard]] Alignment defaultColumnAlignment() const override;
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    TextValue m_text;
    std::optional<Icon> m_icon;
    ButtonVariant m_variant{ButtonVariant::Default};
    bool m_checked{false};
};

} // namespace workpane::ui
