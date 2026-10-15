#pragma once

namespace workpane::ui {

struct ButtonState final {
    bool pressed{false};
    bool hovered{false};
    bool held{false};

    [[nodiscard]] bool doubleClicked() const;
};

} // namespace workpane::ui
