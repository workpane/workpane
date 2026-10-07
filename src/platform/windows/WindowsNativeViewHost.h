#pragma once

#include "Result.h"
#include "platform/WebViewTracker.h"
#include "platform/windows/WindowsWebEnvironment.h"
#include "ui/NativeViewHost.h"
#include "ui/NativeWebView.h"

#include <windows.h>

#include <filesystem>
#include <memory>
#include <vector>

struct GLFWwindow;

namespace workpane::platform {

// Embeds web views as child windows of the product window, whose engine runs on the message loop the product window already pumps.
// Every view is built in the background in one environment the host starts for the first view, so building a view never holds a frame.
class WindowsNativeViewHost final : public ui::NativeViewHost {
  public:
    WindowsNativeViewHost(GLFWwindow* window, bool debug, std::filesystem::path dataDirectory, std::filesystem::path downloadsDirectory);
    ~WindowsNativeViewHost() override;
    WindowsNativeViewHost(const WindowsNativeViewHost&) = delete;
    WindowsNativeViewHost& operator=(const WindowsNativeViewHost&) = delete;

    [[nodiscard]] Result<std::unique_ptr<ui::NativeWebView>> createWebView() override;
    [[nodiscard]] bool pump() override;
    void watch() override;
    void endFrame(const std::vector<ImRect>& floating) override;

  private:
    GLFWwindow* m_window;
    HRESULT m_apartment;
    bool m_debug;
    std::filesystem::path m_downloadsDirectory;
    std::unique_ptr<WindowsWebEnvironment> m_environment;
    WebViewTracker m_tracker;
};

} // namespace workpane::platform
