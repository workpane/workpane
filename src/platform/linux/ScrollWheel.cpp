#include "platform/ScrollWheel.h"

namespace workpane::platform {

// The window reports each notch of a wheel as one step, and the system scrolls three lines of text for it by default.
float ScrollWheel::points(float line) {
    return 3.0F * line;
}

} // namespace workpane::platform
