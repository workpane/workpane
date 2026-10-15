#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/components/choices/ChoiceOption.h"
#include "ui/components/containers/Axis.h"
#include "ui/model/CommonProperties.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"

#include <imgui_internal.h>

#include <string>
#include <string_view>
#include <vector>

namespace workpane::ui {

class RadioGroup final : public Component {
  public:
    explicit RadioGroup(NodeId id);
    [[nodiscard]] std::string_view kind() const override;

  protected:
    [[nodiscard]] Alignment defaultColumnAlignment() const override;
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] Result<void> validate() const override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    static constexpr float radioSpacing{16.0F};

    [[nodiscard]] float optionWidth(RenderContext& context, const ChoiceOption& option) const;

    std::vector<ChoiceOption> m_options;
    std::string m_value;
    Axis m_axis{Axis::Vertical};
};

} // namespace workpane::ui
