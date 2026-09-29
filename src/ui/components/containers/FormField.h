#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/model/TextValue.h"

#include <imgui_internal.h>

#include <cstddef>
#include <string_view>

namespace workpane::ui {

// A field of a form writes its label above its control followed by a colon, so a label of any length never decides the width of the control, and a hint sits under the control.
class FormField final : public Component {
  public:
    explicit FormField(NodeId id);
    [[nodiscard]] std::string_view kind() const override;
    [[nodiscard]] std::size_t childLimit() const override;
    [[nodiscard]] Result<void> validateChildren() const override;

  protected:
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    struct Geometry final {
        float label{0.0F};
        ImVec2 control;
        float hint{0.0F};
    };

    [[nodiscard]] Geometry geometry(RenderContext& context, float availableWidth);
    [[nodiscard]] std::string caption(RenderContext& context) const;

    TextValue m_label;
    TextValue m_hint;
};

} // namespace workpane::ui
