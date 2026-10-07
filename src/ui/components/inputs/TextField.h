#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/model/TextValue.h"

#include <imgui_internal.h>

#include <string>
#include <string_view>

namespace workpane::ui {

// A field keeps the text being typed on this side, so a keystroke never waits for Lua, and reports every change and every submission.
class TextField final : public Component {
  public:
    explicit TextField(NodeId id);
    [[nodiscard]] std::string_view kind() const override;
    [[nodiscard]] Result<void> command(RenderContext& context, std::string_view name, const json::Json& arguments) override;

  protected:
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    std::string m_value;
    TextValue m_placeholder;
    bool m_password{false};
    bool m_readOnly{false};
    bool m_clearButton{false};
    bool m_monospace{false};
    bool m_reload{false};
    bool m_focus{false};
    bool m_selectAll{false};
    bool m_arrows{false};
};

} // namespace workpane::ui
