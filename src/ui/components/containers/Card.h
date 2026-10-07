#pragma once

#include "json/ObjectReader.h"
#include "ui/IconCatalog.h"
#include "ui/components/containers/LinearContainer.h"
#include "ui/model/Insets.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/model/TextValue.h"
#include "ui/theme/Theme.h"

#include <imgui_internal.h>

#include <optional>
#include <string_view>

namespace workpane::ui {

// The painted rounded surface, optionally headed by an icon and a title the way the system information cards are.
class Card final : public LinearContainer {
  public:
    explicit Card(NodeId id);
    [[nodiscard]] std::string_view kind() const override;

  protected:
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void paintSurface(RenderContext& context, const ImRect& bounds) override;
    [[nodiscard]] Insets contentInsets(RenderContext& context) const override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    static constexpr float cardHeaderSpacing{10.0F};
    static constexpr float cardIconSpacing{8.0F};

    [[nodiscard]] float headerHeight(RenderContext& context) const;

    TextValue m_title;
    std::optional<Icon> m_icon;
    float m_radius{4.0F};
    std::optional<ThemeColor> m_outline{ThemeColor::Border};
};

} // namespace workpane::ui
