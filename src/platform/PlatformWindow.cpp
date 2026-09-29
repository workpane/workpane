#include "platform/PlatformWindow.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace workpane::platform {

PlatformWindow::ErrorHandler& PlatformWindow::errors() {
    static PlatformWindow::ErrorHandler handler;
    return handler;
}

void PlatformWindow::reportError(int code, const char* description) {
    if (errors()) {
        errors()(code, description == nullptr ? std::string() : std::string(description));
    }
}

// The window system reports its errors through one process wide callback, which reaches the handler given here.
Result<void> PlatformWindow::initialize(ErrorHandler errors) {
    PlatformWindow::errors() = std::move(errors);
    glfwSetErrorCallback(&PlatformWindow::reportError);

    // The product writes the application menu of macOS itself, in the language of the reader, so the window system builds none.
    glfwInitHint(GLFW_COCOA_MENUBAR, GLFW_FALSE);

#if defined(__linux__)
    // Native web views join the product window as X11 children, so the window system is X11 and a Wayland session reaches it through XWayland.
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif

    if (glfwInit() != GLFW_TRUE) {
        return Result<void>::failure({"window_system_unavailable", "The window system could not be initialized", {}});
    }

    return Result<void>::success();
}

void PlatformWindow::terminate() {
    glfwTerminate();
    errors() = nullptr;
}

Result<std::unique_ptr<PlatformWindow>> PlatformWindow::create(const std::string& title, const std::optional<WindowGeometry>& geometry) {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_MAXIMIZED, geometry.has_value() && geometry->maximized ? GLFW_TRUE : GLFW_FALSE);

    const WindowGeometry initial;
    GLFWwindow* window = glfwCreateWindow(initial.width, initial.height, title.c_str(), nullptr, nullptr);

    // The window system names why it refused the window, which the alert of a failed start shows, and a display without the OpenGL the product draws with is told apart.
    if (const char* reason = nullptr; window == nullptr) {
        const int code = glfwGetError(&reason);
        const bool graphics = code == GLFW_API_UNAVAILABLE || code == GLFW_VERSION_UNAVAILABLE || code == GLFW_FORMAT_UNAVAILABLE;
        return Result<std::unique_ptr<PlatformWindow>>::failure({graphics ? "window_opengl_unavailable" : "window_create_failed", graphics ? "The display offers no OpenGL 3.2 to draw the product window" : "The product window could not be created", reason != nullptr ? reason : ""});
    }

    // A stored size was measured on its monitor already, so it is set after creation, which scales only the size a window is created with.
    // A stored position is only restored while it still lands on a connected display, so a window never opens where nobody can see it.
    if (geometry.has_value() && !geometry->maximized) {
        glfwSetWindowSize(window, std::max(geometry->width, minimumWidth), std::max(geometry->height, minimumHeight));

        int count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&count);

        for (int index = 0; index < count; ++index) {
            int left = 0;
            int top = 0;
            int width = 0;
            int height = 0;
            glfwGetMonitorWorkarea(monitors[index], &left, &top, &width, &height);

            if (geometry->x >= left && geometry->y >= top && geometry->x < left + width && geometry->y < top + height) {
                glfwSetWindowPos(window, geometry->x, geometry->y);
                break;
            }
        }
    }

    glfwSetWindowSizeLimits(window, minimumWidth, minimumHeight, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    auto created = std::unique_ptr<PlatformWindow>(new PlatformWindow(window));
    glfwSetWindowUserPointer(window, created.get());
    glfwSetWindowCloseCallback(window, &PlatformWindow::closeRequested);
    glfwSetDropCallback(window, &PlatformWindow::dropped);

    // These are installed before the ImGui backend, which chains every callback it finds, so both see the same input.
    // clang-format off
    glfwSetCursorPosCallback(window, [](GLFWwindow* source, double, double) { countInput(source); });
    glfwSetMouseButtonCallback(window, [](GLFWwindow* source, int, int, int) { countInput(source); });
    glfwSetScrollCallback(window, [](GLFWwindow* source, double, double) { countInput(source); });
    glfwSetKeyCallback(window, [](GLFWwindow* source, int, int, int, int) { countInput(source); });
    glfwSetCharCallback(window, [](GLFWwindow* source, unsigned int) { countInput(source); });
    glfwSetCursorEnterCallback(window, [](GLFWwindow* source, int) { countInput(source); });
    glfwSetWindowFocusCallback(window, [](GLFWwindow* source, int) { countInput(source); });
    glfwSetWindowSizeCallback(window, [](GLFWwindow* source, int, int) { countInput(source); });
    glfwSetWindowRefreshCallback(window, [](GLFWwindow* source) { countInput(source); });
    glfwSetWindowContentScaleCallback(window, [](GLFWwindow* source, float, float) { countInput(source); });
    // clang-format on
    return Result<std::unique_ptr<PlatformWindow>>::success(std::move(created));
}

// Wakes a wait from any thread, which is how finished background work reaches a sleeping frame loop.
void PlatformWindow::wake() {
    glfwPostEmptyEvent();
}

// A display is described in screen coordinates as the window system sees it, with the scale it applies and the density of its physical pixels.
nlohmann::json PlatformWindow::displays() {
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    GLFWmonitor* primary = glfwGetPrimaryMonitor();
    nlohmann::json found = nlohmann::json::array();

    for (int index = 0; index < count; ++index) {
        GLFWmonitor* monitor = monitors[index];
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        float scale = 1.0F;
        int widthMillimeters = 0;
        int heightMillimeters = 0;
        glfwGetMonitorContentScale(monitor, &scale, nullptr);
        glfwGetMonitorPhysicalSize(monitor, &widthMillimeters, &heightMillimeters);

        if (mode == nullptr) {
            continue;
        }

        // A mode of macOS counts points, which its scale turns into pixels, while every other system counts pixels already.
#if defined(__APPLE__)
        const double pixels = static_cast<double>(mode->width) * static_cast<double>(scale);
#else
        const double pixels = static_cast<double>(mode->width);
#endif
        const double density = widthMillimeters > 0 ? pixels / (static_cast<double>(widthMillimeters) / 25.4) : 0.0;
        const char* name = glfwGetMonitorName(monitor);
        found.push_back({{"name", name == nullptr ? "" : name}, {"width", mode->width}, {"height", mode->height}, {"scale", scale}, {"density", density}, {"refreshRate", mode->refreshRate}, {"primary", monitor == primary}});
    }

    return found;
}

PlatformWindow::PlatformWindow(GLFWwindow* window) : m_window(window) {}

PlatformWindow::~PlatformWindow() {
    glfwDestroyWindow(m_window);
}

GLFWwindow* PlatformWindow::handle() const {
    return m_window;
}

WindowGeometry PlatformWindow::geometry() const {
    WindowGeometry geometry;
    glfwGetWindowPos(m_window, &geometry.x, &geometry.y);
    glfwGetWindowSize(m_window, &geometry.width, &geometry.height);
    geometry.maximized = glfwGetWindowAttrib(m_window, GLFW_MAXIMIZED) == GLFW_TRUE;

    return geometry;
}

// The platform close button and the quit command ask the handler instead of closing, so the reader can still cancel.
void PlatformWindow::setCloseHandler(CloseHandler handler) {
    m_closeHandler = std::move(handler);
}

void PlatformWindow::setDropHandler(DropHandler handler) {
    m_dropHandler = std::move(handler);
}

void PlatformWindow::show() {
    glfwShowWindow(m_window);
    glfwFocusWindow(m_window);
}

bool PlatformWindow::minimized() const {
    return glfwGetWindowAttrib(m_window, GLFW_ICONIFIED) != 0;
}

// Waiting without a deadline sleeps until an event or a wake arrives, which a timeout cannot express, since GLFW turns the largest finite one into a poll interval the system refuses and returns at once.
void PlatformWindow::waitEvents() {
    glfwWaitEvents();
}

void PlatformWindow::waitEvents(double timeoutSeconds) {
    glfwWaitEventsTimeout(timeoutSeconds);
}

void PlatformWindow::pollEvents() {
    glfwPollEvents();
}

void PlatformWindow::swapBuffers() {
    glfwSwapBuffers(m_window);
}

float PlatformWindow::contentScale() const {
    float horizontal = 1.0F;
    float vertical = 1.0F;
    glfwGetWindowContentScale(m_window, &horizontal, &vertical);

    return horizontal;
}

float PlatformWindow::framebufferScale() const {
    int windowWidth = 0;
    int windowHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetWindowSize(m_window, &windowWidth, &windowHeight);
    glfwGetFramebufferSize(m_window, &framebufferWidth, &framebufferHeight);

    return windowWidth > 0 ? static_cast<float>(framebufferWidth) / static_cast<float>(windowWidth) : 1.0F;
}

// Every input the window system delivers advances this count, which is how the frame loop tells a wake with input from a timeout.
std::uint64_t PlatformWindow::inputEvents() const {
    return m_inputEvents;
}

void PlatformWindow::countInput(GLFWwindow* window) {
    if (auto* owner = static_cast<PlatformWindow*>(glfwGetWindowUserPointer(window)); owner != nullptr) {
        ++owner->m_inputEvents;
    }
}

// Files dropped on the window arrive as UTF-8 paths with the pointer where they were let go, which is the component they were dropped on.
void PlatformWindow::dropped(GLFWwindow* window, int count, const char** paths) {
    auto* owner = static_cast<PlatformWindow*>(glfwGetWindowUserPointer(window));

    if (owner == nullptr || !owner->m_dropHandler) {
        return;
    }

    std::vector<std::filesystem::path> files;

    for (int index = 0; index < count; ++index) {
        const std::string_view text(paths[index]);
        files.emplace_back(std::u8string(text.begin(), text.end()));
    }

    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    ++owner->m_inputEvents;
    owner->m_dropHandler(std::move(files), x, y);
}

void PlatformWindow::closeRequested(GLFWwindow* window) {
    glfwSetWindowShouldClose(window, GLFW_FALSE);
    auto* owner = static_cast<PlatformWindow*>(glfwGetWindowUserPointer(window));

    if (owner != nullptr && owner->m_closeHandler) {
        owner->m_closeHandler();
    }
}

} // namespace workpane::platform
