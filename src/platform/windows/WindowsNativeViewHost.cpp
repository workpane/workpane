#include "platform/windows/WindowsNativeViewHost.h"

#include "platform/windows/WindowsWebView.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <ole2.h>
#include <windows.h>

#include <memory>
#include <utility>

namespace workpane::platform {

// Web views need a single threaded apartment with OLE on the interface thread, to be created and to take what the reader drags into a page.
WindowsNativeViewHost::WindowsNativeViewHost(GLFWwindow* window, bool debug, std::filesystem::path dataDirectory, std::filesystem::path downloadsDirectory) : m_window(window), m_apartment(OleInitialize(nullptr)), m_debug(debug), m_downloadsDirectory(std::move(downloadsDirectory)), m_environment(std::make_unique<WindowsWebEnvironment>(std::move(dataDirectory))) {}

// The environment of the views goes before the apartment it lives in.
WindowsNativeViewHost::~WindowsNativeViewHost() {
    m_environment.reset();

    if (SUCCEEDED(m_apartment)) {
        OleUninitialize();
    }
}

// The view answers at once and reports later when it could not be built.
Result<std::unique_ptr<ui::NativeWebView>> WindowsNativeViewHost::createWebView() {
    auto view = std::make_unique<WindowsWebView>(m_tracker, keyboard(), glfwGetWin32Window(m_window), m_downloadsDirectory, m_debug);
    view->build(*m_environment);

    return Result<std::unique_ptr<ui::NativeWebView>>::success(std::move(view));
}

bool WindowsNativeViewHost::pump() {
    return m_tracker.takeActivity();
}

// WebView2 posts its events to the message queue the product window already waits on, so nothing more is watched.
void WindowsNativeViewHost::watch() {}

void WindowsNativeViewHost::focusProduct() {
    SetFocus(glfwGetWin32Window(m_window));
    keyboard().release();
}

void WindowsNativeViewHost::endFrame(const std::vector<ImRect>& floating) {
    HWND window = glfwGetWin32Window(m_window);

    if (m_tracker.clickedOutside() && GetFocus() != window) {
        focusProduct();
    }

    m_tracker.endFrame(floating);
}

} // namespace workpane::platform
