#pragma once

#include <imgui_internal.h>

namespace workpane::ui {

struct Insets final {
    float top{0.0F};
    float right{0.0F};
    float bottom{0.0F};
    float left{0.0F};

    [[nodiscard]] float horizontal() const;
    [[nodiscard]] float vertical() const;
    [[nodiscard]] ImRect shrink(const ImRect& bounds) const;
};

} // namespace workpane::ui
