#pragma once

#include "json/ObjectReader.h"
#include "ui/components/containers/LinearContainer.h"
#include "ui/model/Insets.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/model/TextValue.h"

#include <imgui_internal.h>

#include <string_view>

namespace workpane::ui {

// The header every full view starts with: the page title, an optional caption and the actions of the page, closed by one divider.
class PageHeader final : public LinearContainer {
  public:
    explicit PageHeader(NodeId id);
    [[nodiscard]] std::string_view kind() const override;

  protected:
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void paintSurface(RenderContext& context, const ImRect& bounds) override;
    [[nodiscard]] Insets contentInsets(RenderContext& context) const override;

  private:
    static constexpr float pageHeaderTrailing{8.0F};
    static constexpr float pageHeaderSpacing{8.0F};

    TextValue m_title;
    TextValue m_caption;
};

} // namespace workpane::ui
