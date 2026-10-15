#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/components/collections/ListItem.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"

#include <imgui_internal.h>

#include <string>
#include <string_view>
#include <vector>

namespace workpane::ui {

enum class ListStyle { Default, Navigation };

// A selected row is painted in the accent with the ink made for it, or marked by an accent bar at its edge in the navigation style.
class List final : public Component {
  public:
    explicit List(NodeId id);
    [[nodiscard]] std::string_view kind() const override;
    [[nodiscard]] Result<void> command(RenderContext& context, std::string_view name, const json::Json& arguments) override;

  protected:
    [[nodiscard]] bool revealing() const override;
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] Result<void> validate() const override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    std::vector<ListItem> m_items;
    std::string m_selected;
    ListStyle m_style{ListStyle::Default};
    std::string m_reveal;
};

} // namespace workpane::ui
