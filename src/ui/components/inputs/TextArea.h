#pragma once

#include "json/ObjectReader.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/model/TextValue.h"

#include <imgui_internal.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace workpane::ui {

class TextArea final : public Component {
  public:
    explicit TextArea(NodeId id);
    [[nodiscard]] std::string_view kind() const override;

  protected:
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] FocusMode focusMode() const override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    std::string m_value;
    TextValue m_placeholder;
    std::int64_t m_rows{4};
    bool m_readOnly{false};
    bool m_wrap{true};
    bool m_submitOnEnter{false};
    bool m_reload{false};
};

} // namespace workpane::ui
