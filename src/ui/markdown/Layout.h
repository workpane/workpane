#pragma once

#include "ui/markdown/Run.h"
#include "ui/markdown/Shape.h"

#include <imgui.h>

#include <string>
#include <vector>

namespace workpane::ui::markdown {

struct Layout final {
    float limit{0.0F};
    ImVec2 size;
    std::vector<Run> runs;
    std::vector<Shape> shapes;
    std::vector<std::string> links;
    std::string text;
};

} // namespace workpane::ui::markdown
