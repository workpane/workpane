#include "platform/NativeViews.h"

#include "platform/macos/MacNativeViewHost.h"

namespace workpane::platform {

std::unique_ptr<ui::NativeViewHost> NativeViews::create(GLFWwindow* window, bool debug, const std::filesystem::path& dataDirectory, const std::filesystem::path& downloadsDirectory) {
    return std::make_unique<MacNativeViewHost>(window, debug, dataDirectory, downloadsDirectory);
}

} // namespace workpane::platform
