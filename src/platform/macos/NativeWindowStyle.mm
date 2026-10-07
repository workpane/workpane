#include "platform/NativeWindowStyle.h"

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Cocoa/Cocoa.h>

namespace workpane::platform {

// The title bar follows the dark aqua appearance and the window paints the product surface before its first frame arrives.
void NativeWindowStyle::applyDarkAppearance(GLFWwindow* window, float red, float green, float blue) {
    NSWindow* native = glfwGetCocoaWindow(window);

    if (native == nil) {
        return;
    }

    native.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    native.backgroundColor = [NSColor colorWithSRGBRed:red green:green blue:blue alpha:1.0];
}

} // namespace workpane::platform
