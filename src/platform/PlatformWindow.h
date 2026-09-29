#pragma once

#include "Result.h"
#include "platform/WindowGeometry.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace workpane::platform {

// The one product window with its OpenGL context, created hidden and shown once its first frame is drawn so nothing flashes white.
class PlatformWindow final {
  public:
    static constexpr int minimumWidth{960};
    static constexpr int minimumHeight{600};

    using CloseHandler = std::function<void()>;
    using DropHandler = std::function<void(std::vector<std::filesystem::path> paths, double x, double y)>;
    using ErrorHandler = std::function<void(int code, std::string description)>;

    [[nodiscard]] static Result<void> initialize(ErrorHandler errors);
    static void terminate();
    [[nodiscard]] static Result<std::unique_ptr<PlatformWindow>> create(const std::string& title, const std::optional<WindowGeometry>& geometry);
    static void wake();
    [[nodiscard]] static nlohmann::json displays();

    ~PlatformWindow();
    PlatformWindow(const PlatformWindow&) = delete;
    PlatformWindow& operator=(const PlatformWindow&) = delete;

    [[nodiscard]] GLFWwindow* handle() const;
    [[nodiscard]] WindowGeometry geometry() const;
    void setCloseHandler(CloseHandler handler);
    void setDropHandler(DropHandler handler);
    void show();
    [[nodiscard]] bool minimized() const;
    void waitEvents();
    void waitEvents(double timeoutSeconds);
    void pollEvents();
    void swapBuffers();
    [[nodiscard]] float contentScale() const;
    [[nodiscard]] float framebufferScale() const;
    [[nodiscard]] std::uint64_t inputEvents() const;

  private:
    static void reportError(int code, const char* description);
    [[nodiscard]] static ErrorHandler& errors();

    explicit PlatformWindow(GLFWwindow* window);
    static void closeRequested(GLFWwindow* window);
    static void countInput(GLFWwindow* window);
    static void dropped(GLFWwindow* window, int count, const char** paths);

    GLFWwindow* m_window{nullptr};
    CloseHandler m_closeHandler;
    DropHandler m_dropHandler;
    std::uint64_t m_inputEvents{0};
};

} // namespace workpane::platform
