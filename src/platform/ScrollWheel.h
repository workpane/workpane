#pragma once

namespace workpane::platform {

// How far the system scrolls its own views for each step the window reports from a wheel or a trackpad.
class ScrollWheel final {
  public:
    [[nodiscard]] static float points(float line);
};

} // namespace workpane::platform
