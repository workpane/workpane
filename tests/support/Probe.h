#pragma once

#include "json/ObjectReader.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"

#include <imgui_internal.h>

#include <map>
#include <string_view>

namespace workpane::tests {

// A leaf of a declared content size that remembers the rectangle the layout gave it, which is what the layout tests measure.
class Probe final : public ui::Component {
  public:
    explicit Probe(ui::NodeId id);

    [[nodiscard]] std::string_view kind() const override;
    static std::map<ui::NodeId, ImRect>& placed();

  protected:
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] ImVec2 measureContent(ui::RenderContext&, float) override;
    void render(ui::RenderContext&, const ImRect& bounds) override;

  private:
    float m_contentWidth{10.0F};
    float m_contentHeight{10.0F};
};

} // namespace workpane::tests
