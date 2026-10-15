#include "platform/NativeViews.h"

#include "platform/linux/LinuxNativeViewHost.h"

namespace workpane::platform {

std::unique_ptr<ui::NativeViewHost> NativeViews::create(GLFWwindow* window, bool debug, const std::filesystem::path& dataDirectory, const std::filesystem::path& downloadsDirectory) {
    return std::make_unique<LinuxNativeViewHost>(window, debug, dataDirectory, downloadsDirectory);
}

} // namespace workpane::platform
