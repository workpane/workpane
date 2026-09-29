#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/IconCatalog.h"
#include "ui/Widgets.h"
#include "ui/model/CommonProperties.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/model/TextValue.h"

#include <imgui_internal.h>

#include <cstddef>
#include <optional>
#include <string_view>

namespace workpane::ui {

// A button that opens a floating panel below itself holding one child, such as a grid of choices, until the reader clicks elsewhere or the plugin closes it.
// A plugin opens it too, which is how a shortcut shows the same choices.
class Popover final : public Component {
  public:
    explicit Popover(NodeId id);

    [[nodiscard]] std::string_view kind() const override;
    [[nodiscard]] std::size_t childLimit() const override;
    [[nodiscard]] Result<void> validateChildren() const override;
    [[nodiscard]] Result<void> command(RenderContext& context, std::string_view name, const json::Json& arguments) override;

  protected:
    [[nodiscard]] Alignment defaultColumnAlignment() const override;
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    static constexpr float panelInset{5.0F};
    static constexpr float panelMaximumWidth{640.0F};

    TextValue m_text;
    std::optional<Icon> m_icon;
    ButtonVariant m_variant{ButtonVariant::Default};
    bool m_openRequested{false};
    bool m_closeRequested{false};
};

} // namespace workpane::ui
