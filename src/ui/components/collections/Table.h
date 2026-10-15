#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/components/collections/TableCell.h"
#include "ui/components/collections/TableColumn.h"
#include "ui/components/collections/TableRow.h"
#include "ui/model/CommonProperties.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/theme/FontRole.h"

#include <imgui_internal.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::ui {

// The data grid sizes its columns from their content, gives the remaining width to its one stretch column and wraps that column so no message is cut.
class Table final : public Component {
  public:
    explicit Table(NodeId id);
    [[nodiscard]] std::string_view kind() const override;

  protected:
    [[nodiscard]] Alignment defaultRowAlignment() const override;
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] Result<void> validate() const override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    struct Offsets final {
        std::uint64_t key{0};
        std::uint64_t generation{0};
        float stretch{-1.0F};
        std::vector<float> tops;
    };

    enum class Selection { Accent, Subtle };

    static constexpr float glyphSpacing{6.0F};
    static constexpr float cellHorizontalPadding{7.0F};
    static constexpr float cellVerticalPadding{8.0F};
    static constexpr float minimumStretchWidth{80.0F};
    static constexpr float actionSpacing{2.0F};

    [[nodiscard]] Result<std::vector<TableColumn>> parseColumns(const json::Json& columns) const;
    [[nodiscard]] Result<std::vector<TableRow>> parseRows(const json::Json& rows) const;
    [[nodiscard]] const std::vector<float>& contentWidths(RenderContext& context);
    [[nodiscard]] std::vector<float> columnWidths(RenderContext& context, float available);
    [[nodiscard]] const std::vector<float>& rowOffsets(RenderContext& context, const std::vector<float>& widths);
    [[nodiscard]] float actionsWidth(RenderContext& context) const;
    [[nodiscard]] FontRole cellFont(RenderContext& context, const TableCell& cell) const;
    void drawHeader(RenderContext& context, const ImRect& bounds, const std::vector<float>& widths);
    void drawRow(RenderContext& context, std::size_t index, const ImRect& row, const std::vector<float>& widths);
    void moveSelection(RenderContext& context, int step);

    std::vector<TableColumn> m_columns;
    std::vector<TableRow> m_rows;
    std::size_t m_mostActions{0};
    std::string m_selected;
    Selection m_selection{Selection::Accent};
    bool m_alternate{false};
    bool m_header{true};
    std::uint64_t m_version{1};
    std::uint64_t m_widthsKey{0};
    std::uint64_t m_widthsGeneration{0};
    float m_widthsScale{0.0F};
    std::vector<float> m_contentWidths;
    std::array<Offsets, 2> m_offsets;
    std::size_t m_offsetsSlot{0};
    std::optional<std::string> m_scrollTo;
};

} // namespace workpane::ui
