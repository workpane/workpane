#pragma once

#include <string_view>

namespace workpane::ui {

// How a text area is drawn: its placeholder, whether it may be edited, wraps and submits on Enter, and whether its value was replaced from outside.
struct TextAreaOptions final {
    std::string_view placeholder;
    bool readOnly{false};
    bool wrap{true};
    bool submitOnEnter{false};
    bool reload{false};
};

} // namespace workpane::ui
