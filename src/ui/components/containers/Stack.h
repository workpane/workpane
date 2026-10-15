#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/model/CommonProperties.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"

#include <imgui_internal.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace workpane::ui {

// Mutually exclusive pages share one place, and only the current one is measured and drawn.
class Stack final : public Component {
  public:
    explicit Stack(NodeId id);
    [[nodiscard]] std::string_view kind() const override;
    [[nodiscard]] std::size_t childLimit() const override;

  protected:
    [[nodiscard]] Alignment defaultRowAlignment() const override;
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] Result<void> validateChildren() const override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    [[nodiscard]] Component* current() const;

    std::int64_t m_current{0};
};

} // namespace workpane::ui
