#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/Color.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"

#include <imgui_internal.h>

#include <optional>
#include <string_view>

namespace workpane::ui {

class ColorField final : public Component {
  public:
    explicit ColorField(NodeId id);
    [[nodiscard]] std::string_view kind() const override;

  protected:
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] Result<void> validate() const override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    static constexpr float pickerMargin{8.0F};
    static constexpr float swatchSize{16.0F};
    static constexpr float swatchSpacing{8.0F};
    static constexpr float pickerWidth{220.0F};

    Color m_value;
    bool m_given{false};
};

} // namespace workpane::ui
