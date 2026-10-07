#pragma once

#include <imgui_internal.h>

namespace workpane::ui::markdown {

enum class ShapeKind { Surface, Bar, Rule };

// A shape that reaches the edge is stretched to the width the document is drawn at, whatever width it was laid out for.
struct Shape final {
    ShapeKind kind{ShapeKind::Rule};
    ImRect rect;
    bool reachesEdge{false};
};

} // namespace workpane::ui::markdown
