#pragma once

#include "Result.h"
#include "app/ApplicationIdentity.h"
#include "app/CommandLineOptions.h"
#include "app/FrameScheduler.h"
#include "app/GeometryRecorder.h"
#include "platform/WindowGeometry.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <optional>

struct GLFWwindow;
struct ImGuiContext;
struct ImGuiPlatformImeData;
struct ImGuiViewport;

namespace workpane::platform {
class InputMethod;
class PlatformWindow;
} // namespace workpane::platform

namespace workpane::app {

class Product;

// Gives the product a window: it opens it, runs the frame loop that draws the product into it and tears both down in reverse order.
class Application final {
  public:
    Application(CommandLineOptions options, ApplicationIdentity identity);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    [[nodiscard]] int run();
    [[nodiscard]] static int launch(int argc, char** argv, ApplicationIdentity identity);

  private:
    static constexpr const char* windowOwner{"workpane:window"};
    static constexpr const char* webDirectory{"web"};

    [[nodiscard]] static std::optional<platform::WindowGeometry> storedGeometry(const nlohmann::json& document);
    static void placeCaret(ImGuiContext* context, ImGuiViewport* viewport, ImGuiPlatformImeData* data);
    static void scrolled(GLFWwindow* window, double x, double y);

    [[nodiscard]] Result<void> open();
    [[nodiscard]] Result<void> createWindow();
    void loop();
    void drawFrame();
    void followKeyRepeat();
    void writeGeometry(const platform::WindowGeometry& geometry);
    void writeMenu();
    void shutdown();
    void failStartup(const Error& error);

    CommandLineOptions m_options;
    ApplicationIdentity m_identity;
    FrameScheduler m_scheduler;
    GeometryRecorder m_geometry;
    std::uint64_t m_menuGeneration{0};
    std::unique_ptr<Product> m_product;
    std::unique_ptr<platform::PlatformWindow> m_window;
    std::unique_ptr<platform::InputMethod> m_inputMethod;
    std::uint64_t m_seenInputEvents{0};
    bool m_composed{false};
    bool m_imguiReady{false};
    bool m_shown{false};
    bool m_windowSystem{false};
    bool m_hiddenWork{false};
    bool m_focused{false};
};

} // namespace workpane::app
