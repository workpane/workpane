#pragma once

#include "json/ObjectReader.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/model/TextValue.h"

#include <imgui_internal.h>

#include <string_view>

namespace workpane::ui {

// The heading that opens a group of related content inside a view.
class SectionTitle final : public Component {
  public:
    explicit SectionTitle(NodeId id);
    [[nodiscard]] std::string_view kind() const override;

  protected:
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    TextValue m_text;
};

} // namespace workpane::ui
