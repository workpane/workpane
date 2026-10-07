#include "platform/KeyRepeat.h"

#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <X11/XKBlib.h>

namespace workpane::platform {

std::optional<KeyRepeat> KeyRepeat::system() {
    Display* display = glfwGetX11Display();
    unsigned int delay = 0;
    unsigned int interval = 0;

    if (display == nullptr || XkbGetAutoRepeatRate(display, XkbUseCoreKbd, &delay, &interval) == False || interval == 0) {
        return std::nullopt;
    }

    return KeyRepeat{static_cast<float>(delay) / 1000.0F, static_cast<float>(interval) / 1000.0F};
}

} // namespace workpane::platform
