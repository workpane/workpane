#include "support/InputMethodProbe.h"

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#else
#define GLFW_EXPOSE_NATIVE_X11
#endif
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#if defined(_WIN32)
#include <imm.h>
#else
#include <X11/Xlib.h>
#endif

namespace workpane::tests {

#if defined(_WIN32)
bool InputMethodProbe::running(GLFWwindow* window) {
    HWND handle = glfwGetWin32Window(window);
    HIMC context = ImmGetContext(handle);

    if (context == nullptr) {
        return false;
    }

    ImmReleaseContext(handle, context);

    return true;
}

// The composition starts at the caret, and the candidates avoid the rectangle of its line.
std::optional<InputMethodProbe::Placement> InputMethodProbe::placement(GLFWwindow* window) {
    HWND handle = glfwGetWin32Window(window);
    HIMC context = ImmGetContext(handle);
    COMPOSITIONFORM composition{};
    CANDIDATEFORM candidates{};
    const bool read = context != nullptr && ImmGetCompositionWindow(context, &composition) && ImmGetCandidateWindow(context, 0, &candidates) && candidates.dwStyle == static_cast<DWORD>(CFS_EXCLUDE);

    if (context != nullptr) {
        ImmReleaseContext(handle, context);
    }

    if (!read) {
        return std::nullopt;
    }

    return Placement{static_cast<int>(composition.ptCurrentPos.x), static_cast<int>(candidates.rcArea.bottom)};
}
#else
bool InputMethodProbe::running(GLFWwindow* window) {
    return glfwGetX11InputContext(window) != nullptr;
}

// The spot of the input context is the baseline of the caret.
std::optional<InputMethodProbe::Placement> InputMethodProbe::placement(GLFWwindow* window) {
    XIC context = glfwGetX11InputContext(window);

    if (context == nullptr) {
        return std::nullopt;
    }

    XPoint* spot = nullptr;
    XVaNestedList attributes = XVaCreateNestedList(0, XNSpotLocation, &spot, nullptr);
    const bool read = XGetICValues(context, XNPreeditAttributes, attributes, nullptr) == nullptr && spot != nullptr;
    XFree(attributes);

    if (!read) {
        return std::nullopt;
    }

    const Placement placed{spot->x, spot->y};
    XFree(spot);

    return placed;
}
#endif

} // namespace workpane::tests
