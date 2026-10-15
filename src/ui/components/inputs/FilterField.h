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

// A caption over the field that narrows a list by what is typed into it.
class FilterField final : public Component {
  public:
    explicit FilterField(NodeId id);
    [[nodiscard]] std::string_view kind() const override;

  protected:
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    static constexpr float filterCaptionSpacing{6.0F};

    std::string m_value;
    bool m_reload{false};
    TextValue m_caption;
    TextValue m_placeholder;
};

} // namespace workpane::ui
