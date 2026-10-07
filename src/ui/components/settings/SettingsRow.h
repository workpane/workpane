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

// A row spans the whole width with its caption on the left and its control against the right edge, and a hint sits under the field it explains.
class SettingsRow final : public Component {
  public:
    explicit SettingsRow(NodeId id);
    [[nodiscard]] std::string_view kind() const override;
    [[nodiscard]] std::size_t childLimit() const override;
    [[nodiscard]] Result<void> validateChildren() const override;

  protected:
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    static constexpr float captionSpacing{16.0F};

    struct Geometry final {
        float captionWidth{0.0F};
        float captionHeight{0.0F};
        float controlWidth{0.0F};
        ImVec2 control;
        float line{0.0F};
        float hintWidth{0.0F};
        float hintHeight{0.0F};
    };

    [[nodiscard]] Geometry geometry(RenderContext& context, float availableWidth);

    TextValue m_label;
    TextValue m_hint;
};

} // namespace workpane::ui
