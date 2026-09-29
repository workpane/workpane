#pragma once

struct GLFWwindow;

namespace workpane::platform {

// The window chrome the platform draws follows the dark product surface, painted in the window color of the active theme.
class NativeWindowStyle final {
  public:
    static void applyDarkAppearance(GLFWwindow* window, float red, float green, float blue);
};

} // namespace workpane::platform
