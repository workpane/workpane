#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"

#include <imgui_internal.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace workpane::ui {

// A number steps through a minus and a plus beside its value, because stacked arrows are unreadable at this size.
class NumberField final : public Component {
  public:
    explicit NumberField(NodeId id);
    [[nodiscard]] std::string_view kind() const override;

  protected:
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] Result<void> validate() const override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    static constexpr float stepperSpacing{4.0F};

    [[nodiscard]] std::string formatted() const;
    void commit(RenderContext& context, double value);

    double m_value{0.0};
    double m_minimum{0.0};
    double m_maximum{100.0};
    double m_step{1.0};
    std::int64_t m_decimals{0};
    std::string m_text;
    bool m_editing{false};
};

} // namespace workpane::ui
