#include "platform/InputMethod.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <imm.h>

namespace workpane::platform {

// The input method of Windows draws what it composes in its own window, so the product only tells it where the caret stands.
// Windows draws the text it composes itself, so the handler is never called here.
InputMethod::InputMethod(GLFWwindow* window, CompositionHandler) : m_window(window) {}

InputMethod::~InputMethod() = default;

// The composition starts at the caret and the candidates open under it without covering its line, and a caret that went away cancels what was being composed.
void InputMethod::place(bool visible, float x, float y, float width, float height) {
    HWND window = glfwGetWin32Window(m_window);
    HIMC context = ImmGetContext(window);

    if (context == nullptr) {
        return;
    }

    if (!visible) {
        ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
        ImmReleaseContext(window, context);
        return;
    }

    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_FORCE_POSITION;
    composition.ptCurrentPos = {static_cast<LONG>(x), static_cast<LONG>(y)};
    ImmSetCompositionWindow(context, &composition);
    CANDIDATEFORM candidates{};
    candidates.dwStyle = CFS_EXCLUDE;
    candidates.ptCurrentPos = {static_cast<LONG>(x), static_cast<LONG>(y + height)};
    candidates.rcArea = {static_cast<LONG>(x), static_cast<LONG>(y), static_cast<LONG>(x + width), static_cast<LONG>(y + height)};
    ImmSetCandidateWindow(context, &candidates);
    ImmReleaseContext(window, context);
}

} // namespace workpane::platform
