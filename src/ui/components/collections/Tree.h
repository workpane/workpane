#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/components/collections/TreeItem.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"

#include <imgui_internal.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::ui {

// A tree of items the reader opens and closes, where a draggable item is dropped into a droppable one or between its children.
class Tree final : public Component {
  public:
    explicit Tree(NodeId id);
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
    static constexpr float treeRowPadding{8.0F};
    static constexpr float treeIndent{18.0F};
    static constexpr float glyphSpacing{6.0F};
    static constexpr float chevronWidth{8.0F};
    static constexpr std::size_t maximumTreeDepth{64};

    static constexpr float dropMarkerThickness{2.0F};
    static constexpr float dropIntoBand{0.25F};

    struct VisibleRow final {
        const TreeItem* item{nullptr};
        const TreeItem* parent{nullptr};
        std::size_t depth{0};
    };

    struct DropTarget final {
        std::string parent;
        std::size_t index{0};
        ImRect marker;
        bool into{false};
    };

    [[nodiscard]] Result<std::vector<TreeItem>> parseItems(const json::Json& items, std::size_t depth, std::set<std::string, std::less<>>& seen);
    void collect(const std::vector<TreeItem>& items, const TreeItem* parent, std::size_t depth, std::vector<VisibleRow>& rows) const;
    [[nodiscard]] const std::vector<VisibleRow>& visibleRows();
    void applyExpansion(const std::vector<TreeItem>& items);
    [[nodiscard]] float rowHeight(RenderContext& context) const;
    [[nodiscard]] static bool holds(const TreeItem& item, std::string_view id);
    [[nodiscard]] static std::size_t position(const TreeItem& parent, std::string_view child, std::string_view skipped);
    [[nodiscard]] std::optional<DropTarget> dropTarget(RenderContext& context, const ImRect& bounds, const std::vector<VisibleRow>& rows) const;
    void finishDrag(RenderContext& context, const ImRect& bounds, const std::vector<VisibleRow>& rows);
    void drawItemMenu(RenderContext& context);
    [[nodiscard]] const TreeItem* find(const std::vector<TreeItem>& items, std::string_view id) const;

    std::vector<TreeItem> m_items;
    std::set<std::string, std::less<>> m_expanded;
    std::vector<VisibleRow> m_rows;
    bool m_rowsStale{true};
    std::string m_selected;
    std::string m_dragged;
    std::string m_menuRow;
    std::string m_reveal;
};

} // namespace workpane::ui
