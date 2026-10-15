#pragma once

#include "ui/WidgetHelper.h"
#include "ui/markdown/Span.h"

#include <vector>

namespace workpane::ui::markdown {

struct Cell final {
    std::vector<Span> spans;
    TextAlign align{TextAlign::Start};
};

} // namespace workpane::ui::markdown
