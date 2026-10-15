#include "platform/NativeWindowStyle.h"

#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <cstring>

namespace workpane::platform {

// Window managers that draw GTK decorations read the theme variant a window asks for, so the title bar follows the dark product surface.
void NativeWindowStyle::applyDarkAppearance(GLFWwindow* window, float, float, float) {
    Display* display = glfwGetX11Display();
    const Window native = glfwGetX11Window(window);

    if (display == nullptr || native == 0) {
        return;
    }

    const Atom variant = XInternAtom(display, "_GTK_THEME_VARIANT", False);
    const Atom utf8 = XInternAtom(display, "UTF8_STRING", False);
    const char* dark = "dark";
    XChangeProperty(display, native, variant, utf8, 8, PropModeReplace, reinterpret_cast<const unsigned char*>(dark), static_cast<int>(std::strlen(dark)));
    XFlush(display);
}

} // namespace workpane::platform
