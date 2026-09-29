#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/components/containers/TabItem.h"
#include "ui/model/CommonProperties.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"

#include <imgui_internal.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace workpane::ui {

// The shared tab strip: the current tab carries the accent indicator, a closable one carries the danger circle and one divider closes the strip.
// A movable strip lets the reader drag a tab along it, and its page moves with it.
class Tabs final : public Component {
  public:
    explicit Tabs(NodeId id);
    [[nodiscard]] std::string_view kind() const override;
    [[nodiscard]] std::size_t childLimit() const override;
    [[nodiscard]] Result<void> validateChildren() const override;

  protected:
    [[nodiscard]] Alignment defaultRowAlignment() const override;
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] Result<void> validate() const override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    static constexpr float tabIconSpacing{6.0F};
    static constexpr std::size_t largestImage{65536};
    static constexpr std::string_view imagePrefix{"data:image/"};
    static constexpr float tabCloseReserve{10.0F};
    static constexpr float addButtonMargin{6.0F};

    struct Strip final {
        std::optional<std::string> selected;
        std::optional<std::string> closed;
        std::optional<std::string> activated;
        std::optional<std::pair<std::size_t, std::size_t>> moved;
        bool added{false};
    };

    [[nodiscard]] Strip drawStrip(RenderContext& context, const ImRect& bounds);
    void move(std::size_t from, std::size_t to);
    [[nodiscard]] static float tabWidth(RenderContext& context, const TabItem& item, bool closable);
    [[nodiscard]] static bool lands(const std::vector<ImRect>& slots, std::size_t dragged, std::size_t target, float pointer);

    std::vector<TabItem> m_items;
    json::Json m_itemSpecs = json::Json::array();
    std::string m_current;
    bool m_closable{false};
    bool m_addButton{false};
    bool m_movable{false};
    float m_offset{0.0F};
};

} // namespace workpane::ui
