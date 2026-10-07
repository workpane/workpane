#pragma once

#include "json/ObjectReader.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/model/TextValue.h"

#include <imgui_internal.h>

#include <string>
#include <string_view>

namespace workpane::ui {

// A secret is masked until its owner allows the reveal, because reading it over a shoulder must be a decision.
class SecretField final : public Component {
  public:
    explicit SecretField(NodeId id);
    [[nodiscard]] std::string_view kind() const override;

  protected:
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    static constexpr float revealSpacing{4.0F};

    std::string m_value;
    TextValue m_placeholder;
    bool m_revealed{false};
    bool m_confirmReveal{false};
    bool m_reload{false};
};

} // namespace workpane::ui
