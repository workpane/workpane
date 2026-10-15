#include "platform/ScrollWheel.h"

namespace workpane::platform {

// The window reports a trackpad in tenths of the points the fingers travel and a wheel in lines, and AppKit scrolls ten points for each step of either.
float ScrollWheel::points(float) {
    return 10.0F;
}

} // namespace workpane::platform
