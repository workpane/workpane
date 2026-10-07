#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/components/choices/ChoiceOption.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/model/TextValue.h"

#include <imgui_internal.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::ui {

// The selectable field presents its entries alphabetically unless the owner declares them a scale or a progression.
class Combo final : public Component {
  public:
    explicit Combo(NodeId id);
    [[nodiscard]] std::string_view kind() const override;

  protected:
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] FocusMode focusMode() const override;
    [[nodiscard]] Result<void> validate() const override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    [[nodiscard]] const std::vector<std::size_t>& order(RenderContext& context);

    std::vector<ChoiceOption> m_options;
    std::string m_value;
    TextValue m_placeholder;
    bool m_sorted{true};
    std::vector<std::size_t> m_order;
    std::uint64_t m_orderGeneration{0};
};

} // namespace workpane::ui
