#include "platform/macos/MacNativeViewHost.h"

#include "platform/macos/MacWebView.h"
#include "platform/posix/WebViewEngine.h"

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

#include <utility>

namespace workpane::platform {

MacNativeViewHost::MacNativeViewHost(GLFWwindow* window, bool debug, std::filesystem::path dataDirectory, std::filesystem::path downloadsDirectory) : m_window(window), m_debug(debug), m_dataDirectory(std::move(dataDirectory)), m_downloadsDirectory(std::move(downloadsDirectory)) {}

Result<std::unique_ptr<ui::NativeWebView>> MacNativeViewHost::createWebView() {
    NSWindow* product = glfwGetCocoaWindow(m_window);
    NSWindow* holder = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1, 1) styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:YES];
    holder.releasedWhenClosed = NO;
    auto engine = WebViewEngine::create((__bridge void*)holder, m_debug, m_dataDirectory);

    if (!engine.hasValue()) {
        return Result<std::unique_ptr<ui::NativeWebView>>::failure(engine.error());
    }

    WKWebView* webView = (__bridge WKWebView*)engine.value()->controller();
    return Result<std::unique_ptr<ui::NativeWebView>>::success(std::make_unique<MacWebView>(m_tracker, product.contentView, webView, std::move(engine.value()), holder, m_downloadsDirectory));
}

bool MacNativeViewHost::pump() {
    return m_tracker.takeActivity();
}

// WebKit runs its events on the run loop the product window already waits on, so nothing more is watched.
void MacNativeViewHost::watch() {}

// The window hands the keyboard to whichever view the reader clicks, so only the placement needs following here.
void MacNativeViewHost::endFrame(const std::vector<ImRect>& floating) {
    m_tracker.endFrame(floating);
}

} // namespace workpane::platform
