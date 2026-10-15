#include "platform/NativeViews.h"
#include "support/InterfaceHarness.h"
#include "support/TemporaryDirectory.h"
#include "ui/NativeViewHost.h"
#include "ui/NativeWebView.h"
#include "ui/WebNavigation.h"

#if defined(_WIN32)
#include "platform/windows/WindowsTextHelper.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <windows.h>
#else
#include <GLFW/glfw3.h>
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#endif

#include <gtest/gtest.h>
#include <imgui_internal.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace workpane::platform {

class NativeBrowserInputTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_harness = std::make_unique<tests::InterfaceHarness>();
        ASSERT_EQ(glfwInit(), GLFW_TRUE);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#if defined(_WIN32)
        glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
#endif
        m_window = glfwCreateWindow(800, 600, "Workpane", nullptr, nullptr);
        ASSERT_NE(m_window, nullptr);
        m_host = NativeViews::create(m_window, false, m_data.path() / "web", m_data.path() / "downloads");
#if !defined(_WIN32)
        clipboard("Before browser");
        const std::unique_ptr<gchar, decltype(&g_free)> copied(gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD)), g_free);
        ASSERT_NE(copied, nullptr);
        ASSERT_STREQ(copied.get(), "Before browser");
#endif
        auto created = m_host->createWebView();
        ASSERT_TRUE(created.hasValue()) << created.error().code;
        m_view = std::move(created.value());
        m_view->place(ImRect(0.0F, 40.0F, 800.0F, 600.0F), true);
        // clang-format off
        m_view->setNavigationHandler([this](ui::WebNavigation value) { m_navigation = std::move(value); });
        m_view->setFailureHandler([this](const Error& error) { m_failure = error; });
        // clang-format on
        ASSERT_TRUE(m_view->setHtml("<title>Ready</title><input id='editor' value='Original' style='position:absolute;left:8px;top:8px;width:300px;height:40px'><script>editor.addEventListener('input',()=>document.title='Value:'+editor.value)</script>").hasValue());
        // clang-format off
        const bool ready = until([this]() { return m_failure.has_value() || (m_navigation.title == "Ready" && !m_navigation.loading); });
        // clang-format on
        ASSERT_FALSE(m_failure.has_value()) << m_failure->code << ": " << m_failure->detail;
        ASSERT_TRUE(ready) << "The page title is " << m_navigation.title;
        glfwShowWindow(m_window);
        glfwFocusWindow(m_window);
        focusPage();
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        ASSERT_TRUE(m_view->evaluate("editor.focus();editor.select();document.title='Focused:'+(document.activeElement===editor)").hasValue());
        // clang-format off
        ASSERT_TRUE(until([this]() { return m_navigation.title == "Focused:true"; }));
        // clang-format on
    }

    void TearDown() override {
        m_view.reset();
        m_host.reset();

        if (m_window != nullptr) {
            glfwDestroyWindow(m_window);
            glfwTerminate();
        }

        m_harness.reset();
    }

    [[nodiscard]] bool until(const std::function<bool()>& condition) const {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);

        while (!condition() && std::chrono::steady_clock::now() < deadline) {
            glfwWaitEventsTimeout(0.01);
            std::ignore = m_host->pump();
        }

        return condition();
    }

    void value(std::string_view expected) {
        // clang-format off
        ASSERT_TRUE(until([this, expected]() {
            if (m_navigation.title == "Value:" + std::string(expected)) {
                return true;
            }

            std::ignore = m_view->evaluate("document.title='Value:'+document.getElementById('editor').value");

            return false;
        })) << m_navigation.title;
        // clang-format on
    }

    [[nodiscard]] const char* clipboard() const {
#if defined(_WIN32)
        return glfwGetClipboardString(m_window);
#else
        return ImGui::GetClipboardText();
#endif
    }

    void clipboard(const char* text) const {
#if defined(_WIN32)
        glfwSetClipboardString(m_window, text);
#else
        ImGui::SetClipboardText(text);
#endif
    }

#if defined(_WIN32)
    [[nodiscard]] static HWND nativeFocus() {
        GUITHREADINFO information{};
        information.cbSize = static_cast<DWORD>(sizeof(information));

        return GetGUIThreadInfo(0, &information) != FALSE ? information.hwndFocus : nullptr;
    }

    [[nodiscard]] static std::string describe(HWND window) {
        std::array<wchar_t, 256> title{};
        std::array<wchar_t, 256> kind{};
        GetWindowTextW(window, title.data(), static_cast<int>(title.size()));
        GetClassNameW(window, kind.data(), static_cast<int>(kind.size()));
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);

        return WindowsTextHelper::narrow(title.data()) + " (" + WindowsTextHelper::narrow(kind.data()) + ", process " + std::to_string(process) + ")";
    }

    void focusPage() {
        const HWND window = glfwGetWin32Window(m_window);
        ASSERT_NE(window, nullptr);
        std::ignore = SetForegroundWindow(window);
        POINT point{30, 70};
        ASSERT_NE(ClientToScreen(window, &point), FALSE);
        ASSERT_NE(SetCursorPos(point.x, point.y), FALSE);
        const HWND target = WindowFromPoint(point);
        ASSERT_TRUE(target == window || IsChild(window, target) != FALSE) << "The pointer targets " << describe(target);
        std::array<INPUT, 2> events{};
        events[0].type = INPUT_MOUSE;
        events[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        events[1].type = INPUT_MOUSE;
        events[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        ASSERT_EQ(SendInput(static_cast<UINT>(events.size()), events.data(), static_cast<int>(sizeof(INPUT))), static_cast<UINT>(events.size()));
        // clang-format off
        ASSERT_TRUE(until([window]() { const HWND focused = nativeFocus(); return GetForegroundWindow() == window && focused != nullptr && focused != window && IsChild(window, focused) != FALSE; })) << "The foreground window is " << describe(GetForegroundWindow()) << ". The focused window is " << describe(nativeFocus()) << ". The host window is " << window;
        // clang-format on
    }

    void control(char key) {
        std::array<INPUT, 4> events{};

        for (auto& event : events) {
            event.type = INPUT_KEYBOARD;
        }

        events[0].ki.wVk = VK_CONTROL;
        events[1].ki.wVk = static_cast<WORD>(key);
        events[2].ki.wVk = static_cast<WORD>(key);
        events[2].ki.dwFlags = KEYEVENTF_KEYUP;
        events[3].ki.wVk = VK_CONTROL;
        events[3].ki.dwFlags = KEYEVENTF_KEYUP;
        ASSERT_EQ(SendInput(static_cast<UINT>(events.size()), events.data(), static_cast<int>(sizeof(INPUT))), static_cast<UINT>(events.size()));
    }
#else
    [[nodiscard]] static GtkWidget* browserBelow(GtkWidget* widget) {
        if (WEBKIT_IS_WEB_VIEW(widget)) {
            return widget;
        }

        if (!GTK_IS_CONTAINER(widget)) {
            return nullptr;
        }

        const std::unique_ptr<GList, decltype(&g_list_free)> children(gtk_container_get_children(GTK_CONTAINER(widget)), g_list_free);

        for (GList* child = children.get(); child != nullptr; child = child->next) {
            if (GtkWidget* found = browserBelow(GTK_WIDGET(child->data)); found != nullptr) {
                return found;
            }
        }

        return nullptr;
    }

    void focusPage() {
        const std::unique_ptr<GList, decltype(&g_list_free)> windows(gtk_window_list_toplevels(), g_list_free);

        for (GList* window = windows.get(); window != nullptr && m_browser == nullptr; window = window->next) {
            m_browser = browserBelow(GTK_WIDGET(window->data));
        }

        ASSERT_NE(m_browser, nullptr);
        gtk_widget_grab_focus(m_browser);
        GdkWindow* window = gtk_widget_get_window(m_browser);
        ASSERT_NE(window, nullptr);
        const std::unique_ptr<GdkEvent, decltype(&gdk_event_free)> click(gdk_event_new(GDK_BUTTON_PRESS), gdk_event_free);
        click->button.window = window;
        std::ignore = g_object_ref(window);
        click->button.time = GDK_CURRENT_TIME;
        click->button.x = 30.0;
        click->button.y = 30.0;
        click->button.button = 1;
        gdk_event_set_device(click.get(), gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_window_get_display(window))));
        std::ignore = gtk_widget_event(m_browser, click.get());
    }

    void control(char key, bool shift = false) {
        ASSERT_NE(m_browser, nullptr);
        GdkWindow* window = gtk_widget_get_window(m_browser);
        ASSERT_NE(window, nullptr);
        GdkDisplay* display = gdk_window_get_display(window);
        const guint keyval = gdk_unicode_to_keyval(static_cast<gunichar>(shift ? key : key - 'A' + 'a'));
        GdkKeymapKey* found = nullptr;
        gint count = 0;
        ASSERT_NE(gdk_keymap_get_entries_for_keyval(gdk_keymap_get_for_display(display), keyval, &found, &count), FALSE);
        ASSERT_GT(count, 0);
        const GdkKeymapKey mapping = found[0];
        g_free(found);

        for (const GdkEventType type : {GDK_KEY_PRESS, GDK_KEY_RELEASE}) {
            const std::unique_ptr<GdkEvent, decltype(&gdk_event_free)> event(gdk_event_new(type), gdk_event_free);
            event->key.window = window;
            std::ignore = g_object_ref(window);
            event->key.time = GDK_CURRENT_TIME;
            event->key.state = static_cast<guint>(GDK_CONTROL_MASK) | (shift ? static_cast<guint>(GDK_SHIFT_MASK) : 0U);
            event->key.keyval = keyval;
            event->key.hardware_keycode = static_cast<guint16>(mapping.keycode);
            event->key.group = static_cast<guint8>(mapping.group);
            event->key.string = g_strdup("");
            gdk_event_set_device(event.get(), gdk_seat_get_keyboard(gdk_display_get_default_seat(display)));
            std::ignore = gtk_widget_event(m_browser, event.get());
        }
    }

    GtkWidget* m_browser{nullptr};
#endif

    tests::TemporaryDirectory m_data;
    std::unique_ptr<tests::InterfaceHarness> m_harness;
    GLFWwindow* m_window{nullptr};
    std::unique_ptr<ui::NativeViewHost> m_host;
    std::unique_ptr<ui::NativeWebView> m_view;
    ui::WebNavigation m_navigation;
    std::optional<Error> m_failure;
};

TEST_F(NativeBrowserInputTest, EditsPageTextWithNativeClipboardAndPlatformKeys) {
    control('C');
    // clang-format off
    ASSERT_TRUE(until([this]() { const char* copied = clipboard(); return copied != nullptr && std::string_view(copied) == "Original"; }));
    // clang-format on
    clipboard("Native paste");
    control('V');
    value("Native paste");
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    control('Z');
    value("Original");
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
#if defined(_WIN32)
    control('Y');
#else
    control('Z', true);
#endif
    value("Native paste");
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
#if !defined(_WIN32)
    control('Z');
    value("Original");
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    control('Y');
    value("Native paste");
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
#endif
    control('A');
    control('X');
    value("");
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    control('V');
    value("Native paste");
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_TRUE(m_view->evaluate("editor.onkeydown=event=>{if(event.ctrlKey&&event.key.toLowerCase()==='z'){event.preventDefault();document.title='Page handled undo'}};document.title='Undo handler ready'").hasValue());
    // clang-format off
    ASSERT_TRUE(until([this]() { return m_navigation.title == "Undo handler ready"; }));
    // clang-format on
    control('Z');
    // clang-format off
    ASSERT_TRUE(until([this]() { return m_navigation.title == "Page handled undo"; }));
    // clang-format on
    value("Native paste");
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_TRUE(m_view->evaluate("editor.onkeydown=null").hasValue());
    m_host->focusProduct();
    // clang-format off
    EXPECT_TRUE(until([this]() { return glfwGetWindowAttrib(m_window, GLFW_FOCUSED) == GLFW_TRUE; }));
    // clang-format on
}

} // namespace workpane::platform
