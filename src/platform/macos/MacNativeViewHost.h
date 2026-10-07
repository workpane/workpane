#pragma once

#include "Result.h"
#include "platform/WebViewTracker.h"
#include "ui/NativeViewHost.h"
#include "ui/NativeWebView.h"

#include <filesystem>
#include <memory>
#include <vector>

struct GLFWwindow;

namespace workpane::platform {

// Embeds web views as subviews of the content view of the product window, whose engine runs on the event loop the product window already pumps.
class MacNativeViewHost final : public ui::NativeViewHost {
  public:
    MacNativeViewHost(GLFWwindow* window, bool debug, std::filesystem::path dataDirectory, std::filesystem::path downloadsDirectory);

    [[nodiscard]] Result<std::unique_ptr<ui::NativeWebView>> createWebView() override;
    [[nodiscard]] bool pump() override;
    void watch() override;
    void endFrame(const std::vector<ImRect>& floating) override;

  private:
    GLFWwindow* m_window;
    bool m_debug;
    std::filesystem::path m_dataDirectory;
    std::filesystem::path m_downloadsDirectory;
    WebViewTracker m_tracker;
};

} // namespace workpane::platform
