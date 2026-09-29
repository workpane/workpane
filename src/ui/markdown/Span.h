#pragma once

#include <string>

namespace workpane::ui::markdown {

struct Span final {
    std::string text;
    bool strong{false};
    bool emphasis{false};
    bool code{false};
    std::string link;

    [[nodiscard]] bool sameStyle(const Span& other) const;
};

} // namespace workpane::ui::markdown
