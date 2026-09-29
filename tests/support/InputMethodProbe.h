#pragma once

#include <optional>

struct GLFWwindow;

namespace workpane::tests {

// Asks the input method of Windows or X11 where it was told the caret of a window stands, keeping their headers away from the tests.
class InputMethodProbe final {
  public:
    struct Placement final {
        int x;
        int bottom;
    };

    [[nodiscard]] static bool running(GLFWwindow* window);
    [[nodiscard]] static std::optional<Placement> placement(GLFWwindow* window);
};

} // namespace workpane::tests
