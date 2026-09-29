#include "platform/NativeWindowStyle.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#include <windows.h>

#include <cmath>

namespace workpane::platform {

// The immersive dark attribute darkens the title bar, and the caption color matches the product surface where the system supports it.
void NativeWindowStyle::applyDarkAppearance(GLFWwindow* window, float red, float green, float blue) {
    HWND native = glfwGetWin32Window(window);

    if (native == nullptr) {
        return;
    }

    constexpr DWORD immersiveDarkMode = 20;
    constexpr DWORD captionColor = 35;
    const BOOL enabled = TRUE;
    const COLORREF caption = RGB(static_cast<BYTE>(std::lround(red * 255.0F)), static_cast<BYTE>(std::lround(green * 255.0F)), static_cast<BYTE>(std::lround(blue * 255.0F)));
    DwmSetWindowAttribute(native, immersiveDarkMode, &enabled, sizeof(enabled));
    DwmSetWindowAttribute(native, captionColor, &caption, sizeof(caption));
}

} // namespace workpane::platform
