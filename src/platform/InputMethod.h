#pragma once

#include <cstddef>
#include <functional>
#include <string>

struct GLFWwindow;

namespace workpane::platform {

// The input method of the system, which composes text such as an accented letter or a Japanese word before it reaches the product.
// It is told where the caret of the focused text stands, so its candidate window opens beside it, and on macOS it hands over the text it composes, which the product draws at the caret because nothing else does.
class InputMethod final {
  public:
    using CompositionHandler = std::function<void(const std::string& text, std::size_t caret)>;

    InputMethod(GLFWwindow* window, CompositionHandler handler);
    ~InputMethod();

    InputMethod(const InputMethod&) = delete;
    InputMethod& operator=(const InputMethod&) = delete;

    void place(bool visible, float x, float y, float width, float height);

  private:
    GLFWwindow* m_window;
#if defined(__APPLE__)
    CompositionHandler m_handler;
#endif
};

} // namespace workpane::platform
