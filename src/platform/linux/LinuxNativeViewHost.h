#pragma once

#include "Result.h"
#include "platform/WebViewTracker.h"
#include "platform/linux/LinuxEventWatcher.h"
#include "ui/NativeViewHost.h"
#include "ui/NativeWebView.h"

#include <filesystem>
#include <memory>
#include <vector>

struct GLFWwindow;

namespace workpane::platform {

// Embeds web views into the X11 product window through GTK plugs and runs the GTK events inside the frame loop of the product.
// Once GTK started, its events are watched while the loop sleeps, so a page wakes the loop only when GTK has something to run.
class LinuxNativeViewHost final : public ui::NativeViewHost {
  public:
    LinuxNativeViewHost(GLFWwindow* window, bool debug, std::filesystem::path dataDirectory, std::filesystem::path downloadsDirectory);

    [[nodiscard]] Result<std::unique_ptr<ui::NativeWebView>> createWebView() override;
    [[nodiscard]] bool pump() override;
    void watch() override;
    void endFrame(const std::vector<ImRect>& floating) override;

  private:
    GLFWwindow* m_window;
    bool m_debug;
    std::filesystem::path m_dataDirectory;
    std::filesystem::path m_downloadsDirectory;
    std::unique_ptr<LinuxEventWatcher> m_watcher;
    WebViewTracker m_tracker;
};

} // namespace workpane::platform
