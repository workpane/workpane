#pragma once

#include <cstdint>
#include <string>

namespace workpane::ui {

// A page asking to use the camera or the microphone, numbered by the view that waits for the answer and named by the origin of the page.
struct WebPermission final {
    std::uint64_t request{0};
    std::string origin;
    bool camera{false};
    bool microphone{false};
};

} // namespace workpane::ui
