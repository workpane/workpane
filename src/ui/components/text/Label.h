#pragma once

#include "json/ObjectReader.h"
#include "ui/Color.h"
#include "ui/WidgetHelper.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/model/TextValue.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

#include <imgui_internal.h>

#include <optional>
#include <string>
#include <string_view>

namespace workpane::ui {

enum class LabelStyle { Body, Strong, Muted, Caption, Title, Heading, Monospace };

class Label final : public Component {
  public:
    explicit Label(NodeId id);
    [[nodiscard]] std::string_view kind() const override;

  protected:
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    [[nodiscard]] FontRole role(RenderContext& context) const;
    [[nodiscard]] Color ink(RenderContext& context) const;

    TextValue m_text;
    LabelStyle m_style{LabelStyle::Body};
    std::optional<ThemeColor> m_color;
    std::optional<float> m_size;
    TextAlign m_textAlign{TextAlign::Start};
    bool m_wrap{true};
    bool m_selectable{false};
    std::string m_selectionBuffer;
};

} // namespace workpane::ui
