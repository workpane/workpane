#include "platform/InputMethod.h"

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <utility>

namespace workpane::platform {

InputMethod::InputMethod(GLFWwindow* window, CompositionHandler handler) : m_window(window), m_handler(std::move(handler)) {
    // clang-format off
    glfwSetCocoaCompositionCallback(m_window, [](void* receiver, const char* text, int caret) {
        static_cast<InputMethod*>(receiver)->m_handler(std::string(text), static_cast<std::size_t>(caret));
    }, this);
    // clang-format on
}

InputMethod::~InputMethod() {
    glfwSetCocoaCompositionCallback(m_window, nullptr, nullptr);
}

// The candidate window opens under the caret, and a caret that went away ends what was being composed.
void InputMethod::place(bool visible, float x, float y, float width, float height) {
    glfwSetCocoaTextCursor(m_window, visible ? 1 : 0, static_cast<double>(x), static_cast<double>(y), static_cast<double>(width), static_cast<double>(height));
}

} // namespace workpane::platform
