#include "platform/linux/LinuxNativeViewHost.h"

#include "platform/PlatformWindow.h"
#include "platform/linux/LinuxWebView.h"
#include "platform/posix/WebViewEngine.h"

#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <X11/Xlib.h>
#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#include <webkit2/webkit2.h>

#include <clocale>
#include <memory>
#include <utility>

namespace workpane::platform {

LinuxNativeViewHost::LinuxNativeViewHost(GLFWwindow* window, bool debug, std::filesystem::path dataDirectory, std::filesystem::path downloadsDirectory) : m_window(window), m_debug(debug), m_dataDirectory(std::move(dataDirectory)), m_downloadsDirectory(std::move(downloadsDirectory)) {}

Result<std::unique_ptr<ui::NativeWebView>> LinuxNativeViewHost::createWebView() {
    // The plug joins an X11 window, so GTK is started on its X11 backend whatever session the desktop runs, without changing the environment the programs of the reader inherit.
    if (m_watcher == nullptr) {
        gdk_set_allowed_backends("x11");
        const bool started = gtk_init_check(nullptr, nullptr) == TRUE;

        // GTK takes the whole locale of the reader, and numbers go back to the point before their decimals, which the product and Lua write and read everywhere.
        std::setlocale(LC_NUMERIC, "C");
        // clang-format off
        m_watcher = started ? std::make_unique<LinuxEventWatcher>([]() { PlatformWindow::wake(); }) : nullptr;
        // clang-format on
    }

    if (m_watcher == nullptr) {
        return Result<std::unique_ptr<ui::NativeWebView>>::failure({"webview_toolkit_unavailable", "GTK could not be started for the web view", {}});
    }

    GtkWidget* plug = gtk_plug_new(glfwGetX11Window(m_window));
    auto engine = WebViewEngine::create(plug, m_debug, m_dataDirectory);

    if (!engine.hasValue()) {
        gtk_widget_destroy(plug);
        return Result<std::unique_ptr<ui::NativeWebView>>::failure(engine.error());
    }

    auto* browser = WEBKIT_WEB_VIEW(engine.value()->controller());
    return Result<std::unique_ptr<ui::NativeWebView>>::success(std::make_unique<LinuxWebView>(m_tracker, plug, browser, std::move(engine.value()), glfwGetX11Window(m_window), m_downloadsDirectory));
}

// GTK runs its own events here on every iteration of the frame loop, because the product owns the only loop the process has.
bool LinuxNativeViewHost::pump() {
    if (m_watcher != nullptr) {
        m_watcher->dispatch();
    }

    return m_tracker.takeActivity();
}

// The web engine talks to its own processes through GTK, whose descriptors and timeouts are watched while the loop sleeps.
void LinuxNativeViewHost::watch() {
    if (m_watcher != nullptr) {
        m_watcher->watch();
    }
}

// X11 keeps the keyboard on the plug after the reader leaves it, so a click on the product gives the keyboard back to the product window.
void LinuxNativeViewHost::endFrame(const std::vector<ImRect>& floating) {
    if (m_tracker.clickedOutside()) {
        Display* display = glfwGetX11Display();
        XSetInputFocus(display, glfwGetX11Window(m_window), RevertToParent, CurrentTime);
        XFlush(display);
    }

    m_tracker.endFrame(floating);
}

} // namespace workpane::platform
