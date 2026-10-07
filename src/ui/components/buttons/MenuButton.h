#pragma once

#include "json/ObjectReader.h"
#include "ui/IconCatalog.h"
#include "ui/MenuItem.h"
#include "ui/WidgetHelper.h"
#include "ui/model/CommonProperties.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/model/TextValue.h"

#include <imgui_internal.h>

#include <optional>
#include <string_view>
#include <vector>

namespace workpane::ui {

// A button that opens a flat menu of actions below itself and reports the one the reader picked.
class MenuButton final : public Component {
  public:
    explicit MenuButton(NodeId id);
    [[nodiscard]] std::string_view kind() const override;

  protected:
    [[nodiscard]] Alignment defaultColumnAlignment() const override;
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    static constexpr const char* itemsPopup{"##menuButtonItems"};

    TextValue m_text;
    std::optional<Icon> m_icon;
    ButtonVariant m_variant{ButtonVariant::Default};
    std::vector<MenuItem> m_items;
};

} // namespace workpane::ui
