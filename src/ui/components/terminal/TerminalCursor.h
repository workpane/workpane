#pragma once

namespace workpane::ui {

enum class CursorShape { Block, Underline, Bar };

struct TerminalCursor final {
    int row{0};
    int column{0};
    bool visible{true};
    bool blinking{true};
    CursorShape shape{CursorShape::Block};
};

} // namespace workpane::ui
