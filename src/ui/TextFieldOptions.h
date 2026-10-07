#pragma once

#include <string_view>

namespace workpane::ui {

struct TextFieldOptions final {
    std::string_view placeholder;
    bool password{false};
    bool readOnly{false};
    bool clearButton{false};
    bool monospace{false};
    bool numeric{false};
    bool reload{false};
    bool focus{false};
    bool selectAll{false};
    bool arrows{false};
};

} // namespace workpane::ui
