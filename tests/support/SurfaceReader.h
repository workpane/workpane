#pragma once

#include "support/DeclaredSurface.h"
#include "ui/model/NodeId.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string_view>
#include <vector>

namespace workpane::tests {

// Reads what a mounted surface shows through the kinds and properties of its nodes, which is how a product test finds a control without positions.
class SurfaceReader final {
  public:
    explicit SurfaceReader(const DeclaredSurface* surface);

    [[nodiscard]] bool mounted() const;
    [[nodiscard]] bool showsKey(std::string_view key) const;
    [[nodiscard]] bool showsText(std::string_view literal) const;
    [[nodiscard]] std::optional<ui::NodeId> nodeShowing(std::string_view key) const;
    [[nodiscard]] std::vector<ui::NodeId> nodes(std::string_view kind) const;
    [[nodiscard]] nlohmann::json properties(ui::NodeId node) const;

  private:
    const DeclaredSurface* m_surface;
};

} // namespace workpane::tests
