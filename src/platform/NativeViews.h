#pragma once

#include "ui/NativeViewHost.h"

#include <filesystem>
#include <memory>

struct GLFWwindow;

namespace workpane::platform {

// Creates the native view host of the running platform for the product window, whose web views keep their data under the directory given and save downloads in the downloads folder given.
class NativeViews final {
  public:
    [[nodiscard]] static std::unique_ptr<ui::NativeViewHost> create(GLFWwindow* window, bool debug, const std::filesystem::path& dataDirectory, const std::filesystem::path& downloadsDirectory);
};

} // namespace workpane::platform
