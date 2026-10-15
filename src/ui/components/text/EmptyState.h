#pragma once

#include "json/ObjectReader.h"
#include "ui/IconCatalog.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/model/TextValue.h"

#include <imgui_internal.h>

#include <optional>
#include <string_view>

namespace workpane::ui {

class EmptyState final : public Component {
  public:
    explicit EmptyState(NodeId id);
    [[nodiscard]] std::string_view kind() const override;

  protected:
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    static constexpr float emptyStateIconSize{32.0F};
    static constexpr float emptyStateIconSpacing{12.0F};

    TextValue m_text;
    std::optional<Icon> m_icon;
};

} // namespace workpane::ui
