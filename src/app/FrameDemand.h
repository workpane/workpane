#pragma once

#include <limits>

namespace workpane::app {

struct FrameDemand final {
    bool input{false};
    bool animating{false};
    bool changed{false};
    double deadline{std::numeric_limits<double>::infinity()};
};

} // namespace workpane::app
