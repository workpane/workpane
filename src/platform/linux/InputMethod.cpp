#include "platform/InputMethod.h"

#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <X11/Xlib.h>

#include <tuple>

namespace workpane::platform {

// The input method of X11 draws what it composes in its own window, so the product only tells it where the caret stands.
// X11 draws the text it composes itself, so the handler is never called here.
InputMethod::InputMethod(GLFWwindow* window, CompositionHandler) : m_window(window) {}

InputMethod::~InputMethod() = default;

// The spot is the baseline of the caret, where the input method opens its window, and a caret that went away resets what was being composed.
void InputMethod::place(bool visible, float x, float y, [[maybe_unused]] float width, float height) {
    XIC context = glfwGetX11InputContext(m_window);

    if (context == nullptr) {
        return;
    }

    if (!visible) {
        XFree(Xutf8ResetIC(context));
        return;
    }

    XPoint spot{static_cast<short>(x), static_cast<short>(y + height)};
    XVaNestedList attributes = XVaCreateNestedList(0, XNSpotLocation, &spot, nullptr);
    std::ignore = XSetICValues(context, XNPreeditAttributes, attributes, nullptr);
    XFree(attributes);
}

} // namespace workpane::platform
