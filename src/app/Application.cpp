#include "app/Application.h"

#include "BuildInfo.h"
#include "app/ApplicationPathsResolver.h"
#include "app/FrameDemand.h"
#include "app/Product.h"
#include "audio/MiniaudioOutput.h"
#include "json/ObjectReader.h"
#include "localization/CoreCatalog.h"
#include "localization/Localization.h"
#include "logging/LogLevels.h"
#include "logging/LogService.h"
#include "persistence/PreferenceStore.h"
#include "platform/ApplicationMenu.h"
#include "platform/InputMethod.h"
#include "platform/NativeDialogService.h"
#include "platform/NativeProcesses.h"
#include "platform/NativePseudoTerminals.h"
#include "platform/NativeSystemInspector.h"
#include "platform/NativeSystemServices.h"
#include "platform/NativeViews.h"
#include "platform/NativeWindowStyle.h"
#include "platform/PlatformWindow.h"
#include "platform/ScrollWheel.h"
#include "ui/NativeViewHost.h"
#include "ui/WheelScale.h"
#include "ui/model/RenderContext.h"
#include "ui/theme/Theme.h"

#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace workpane::app {

// A stored geometry is used only when every value of it is present and sane, and the platform default applies otherwise.
std::optional<platform::WindowGeometry> Application::storedGeometry(const nlohmann::json& document) {
    const json::Json* window = &json::ObjectReader::absent();
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t width = 0;
    std::int64_t height = 0;
    bool maximized = false;
    json::ObjectReader reader(document, "workpane-window");
    reader.readObject("window", window, json::Presence::Optional);

    if (!reader.finish().hasValue() || window->is_null()) {
        return std::nullopt;
    }

    json::ObjectReader geometry(*window, "workpane-window.window");
    geometry.readInteger("x", x, -100000, 100000).readInteger("y", y, -100000, 100000).readInteger("width", width, platform::PlatformWindow::minimumWidth, 100000).readInteger("height", height, platform::PlatformWindow::minimumHeight, 100000).read("maximized", maximized);

    if (!geometry.finish().hasValue()) {
        return std::nullopt;
    }

    return platform::WindowGeometry{static_cast<int>(x), static_cast<int>(y), static_cast<int>(width), static_cast<int>(height), maximized};
}

Application::Application(CommandLineOptions options) : m_options(std::move(options)) {}

Application::~Application() = default;

int Application::run() {
    if (const auto opened = open(); !opened.hasValue()) {
        failStartup(opened.error());
        shutdown();
        return 1;
    }

    m_product->reportStartupNotices(glfwGetTime());
    loop();
    shutdown();

    // A restart asked for by an import starts the next process only after this one released its data directory.
    if (m_product->restarting()) {
        if (const auto relaunched = m_product->system().relaunch(m_options.arguments()); !relaunched.hasValue()) {
            platform::NativeDialogService::alertBlocking(m_product->localization().translate("workpane.startup.failed-title"), relaunched.error().message);
            return 1;
        }
    }

    return 0;
}

Result<void> Application::open() {
    auto paths = ApplicationPathsResolver::resolve(m_options.dataDirectory);

    if (!paths.hasValue()) {
        return Result<void>::failure(paths.error());
    }

    auto product = Product::open(std::move(paths.value()), std::make_unique<platform::NativeSystemServices>(), std::make_unique<platform::NativeSystemInspector>(), std::make_unique<platform::NativePseudoTerminals>(), std::make_unique<platform::NativeProcesses>(), std::make_unique<audio::MiniaudioOutput>(false));

    if (!product.hasValue()) {
        return Result<void>::failure(product.error());
    }

    m_product = std::move(product.value());

    if (const auto created = createWindow(); !created.hasValue()) {
        return created;
    }

    return m_product->start();
}

// The window opens where the reader left it, and the product is attached to it with the dialogs and native views of the platform.
Result<void> Application::createWindow() {
    // clang-format off
    const auto initialized = platform::PlatformWindow::initialize([this](int code, std::string description) { m_product->logs().write(logging::LogLevel::Error, std::string(localization::Localization::coreOwner), "window", std::move(description), {{"code", code}}); });
    // clang-format on

    if (!initialized.hasValue()) {
        return initialized;
    }

    m_windowSystem = true;
    auto window = platform::PlatformWindow::create(m_product->localization().translate("workpane.window.title"), storedGeometry(m_product->preferences().document(windowOwner)));

    if (!window.hasValue()) {
        return Result<void>::failure(window.error());
    }

    m_window = std::move(window.value());
    m_geometry.start(m_window->geometry());
    writeMenu();
    // clang-format off
    m_window->setCloseHandler([this]() { m_product->requestQuit(); });
    m_window->setDropHandler([this](std::vector<std::filesystem::path> paths, double x, double y) { m_product->dropFiles(std::move(paths), ImVec2(static_cast<float>(x), static_cast<float>(y))); });
    // clang-format on

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplGlfw_InitForOpenGL(m_window->handle(), true);
    glfwSetScrollCallback(m_window->handle(), &Application::scrolled);
    ImGui_ImplOpenGL3_Init("#version 150");
    m_imguiReady = true;

    GLFWwindow* handle = m_window->handle();
    // clang-format off
    m_inputMethod = std::make_unique<platform::InputMethod>(handle, [this](const std::string& text, std::size_t caret) {
        m_product->compose(text, caret);
        m_composed = true;
    });
    // clang-format on

    ImGui::GetPlatformIO().Platform_SetImeDataFn = &Application::placeCaret;
    ImGui::GetPlatformIO().Platform_ImeUserData = m_inputMethod.get();
    // clang-format off
    return m_product->attach({*io.Fonts, std::make_unique<platform::NativeDialogService>(), platform::NativeViews::create(handle, BuildInfo::debugBuild, m_product->paths().data / webDirectory, m_product->system().downloads()), m_window->contentScale() / m_window->framebufferScale(), []() { platform::PlatformWindow::wake(); }, [handle](const ui::Theme& theme) {
        const ImVec4 color = theme.color(ui::ThemeColor::Window).vector();
        platform::NativeWindowStyle::applyDarkAppearance(handle, color.x, color.y, color.z);
    }, []() { return platform::PlatformWindow::displays(); }});
    // clang-format on
}

// Lua is advanced before every sleep and after every wake, and whatever it changed through the host is drawn before the loop sleeps again.
// The loop sleeps until the next timer of Lua, and work that reaches Lua from another thread wakes it, while components of a hidden window that still work keep a short tick.
void Application::loop() {
    while (!m_product->quitting()) {
        m_product->pollScripts();
        const double work = std::min(m_product->scriptIdleSeconds(), m_hiddenWork ? FrameScheduler::hiddenTickSeconds : std::numeric_limits<double>::infinity());
        const bool shown = !m_window->minimized();
        const double wait = std::min(m_scheduler.waitSeconds(glfwGetTime(), work, m_product->dialogPending(), m_product->undrawn(), shown), m_geometry.waitSeconds(glfwGetTime()));

        if (wait > 0.0) {
            m_product->watchNativeViews();
        }

        if (wait <= 0.0) {
            m_window->pollEvents();
        } else if (std::isinf(wait)) {
            m_window->waitEvents();
        } else {
            m_window->waitEvents(wait);
        }

        const bool input = m_window->inputEvents() != m_seenInputEvents || std::exchange(m_composed, false);
        m_seenInputEvents = m_window->inputEvents();
        const bool changed = m_product->service();

        // A minimized window or one never shown has no place of its own, so only the place of a window on screen is remembered.
        if (shown && m_shown) {
            m_geometry.observe(m_window->geometry(), glfwGetTime());
        }

        if (const auto settled = m_geometry.due(glfwGetTime()); settled.has_value()) {
            writeGeometry(*settled);
        }

        if (m_product->localization().generation() != m_menuGeneration) {
            writeMenu();
        }

        // A minimized window draws nothing, which pauses every animation and every canvas, and it draws again as it is restored.
        // Its components keep working meanwhile, so a terminal keeps reading its shell and events keep reaching the plugins.
        const bool restored = std::exchange(m_minimized, !shown) && shown;
        m_hiddenWork = !shown && m_product->updateHidden(glfwGetTime());

        if (shown && m_scheduler.shouldDraw({input || restored, false, changed}, glfwGetTime())) {
            drawFrame();
        }
    }
}

// A step of a wheel or a trackpad reaches ImGui in the units it scrolls by, so every view moves as far as the views of the system do.
void Application::scrolled(GLFWwindow* window, double x, double y) {
    const ImVec2 units = ui::WheelScale::units(ImVec2(static_cast<float>(x), static_cast<float>(y)), platform::ScrollWheel::points(ImGui::GetStyle().FontSizeBase));
    ImGui_ImplGlfw_ScrollCallback(window, static_cast<double>(units.x), static_cast<double>(units.y));
}

// ImGui tells where the caret of the focused text stands whenever it moves, appears or goes away, and the input method of the system follows it.
void Application::placeCaret(ImGuiContext*, ImGuiViewport*, ImGuiPlatformImeData* data) {
    auto* method = static_cast<platform::InputMethod*>(ImGui::GetPlatformIO().Platform_ImeUserData);
    method->place(data->WantVisible, data->InputPos.x, data->InputPos.y, 1.0F, data->InputLineHeight);
}

void Application::drawFrame() {
    m_product->setScale(m_window->contentScale() / m_window->framebufferScale());
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    m_product->draw(ImGui::GetIO().DisplaySize, glfwGetTime());
    ImGui::Render();
    const FrameDemand demand = m_product->finishFrame();

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_window->handle(), &width, &height);
    const ImVec4 background = m_product->render().theme().color(ui::ThemeColor::Window).vector();
    glViewport(0, 0, width, height);
    glClearColor(background.x, background.y, background.z, background.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    m_window->swapBuffers();

    // The first frame is drawn before the window appears, so the reader never sees an empty or white surface.
    if (!m_shown) {
        m_window->show();
        m_shown = true;
    }

    m_scheduler.drew(demand);
}

void Application::writeGeometry(const platform::WindowGeometry& geometry) {
    // clang-format off
    m_product->preferences().write(windowOwner, {{"window", {{"x", geometry.x}, {"y", geometry.y}, {"width", geometry.width}, {"height", geometry.height}, {"maximized", geometry.maximized}}}}, [](Result<void>) {});
    // clang-format on
}

// The application menu follows the language of the reader, and it is written again whenever that language changes.
void Application::writeMenu() {
    const localization::Localization& words = m_product->localization();
    platform::ApplicationMenu::install({words.translate("workpane.menu.about"), words.translate("workpane.menu.services"), words.translate("workpane.menu.hide"), words.translate("workpane.menu.hide-others"), words.translate("workpane.menu.show-all"), words.translate("workpane.menu.quit"), words.translate("workpane.menu.window"), words.translate("workpane.menu.minimize"), words.translate("workpane.menu.zoom"), words.translate("workpane.menu.front")});
    m_menuGeneration = words.generation();
}

// The geometry of the window is written before the product stops, because the product closes the database it goes to, and a window closed while minimized keeps the last place it had on screen.
void Application::shutdown() {
    if (m_window != nullptr && m_shown && !m_window->minimized()) {
        m_geometry.observe(m_window->geometry(), glfwGetTime());
    }

    if (const auto unwritten = m_geometry.unwritten(); unwritten.has_value() && m_product != nullptr) {
        writeGeometry(*unwritten);
    }

    if (m_product != nullptr) {
        m_product->stop();
    }

    if (m_imguiReady) {
        ImGui_ImplOpenGL3_Shutdown();
        m_product->releaseTextures();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        m_imguiReady = false;
    }

    m_inputMethod.reset();

    m_window.reset();

    if (m_windowSystem) {
        platform::PlatformWindow::terminate();
        m_windowSystem = false;
    }
}

// A product that could not open has no stored language yet, so its failure is told in the language of the system.
void Application::failStartup(const Error& error) {
    localization::Localization system;
    std::ignore = system.registerCatalog(localization::Localization::coreOwner, localization::CoreCatalog::catalog());
    std::ignore = system.selectLanguage(localization::Localization::resolveLanguage(platform::NativeSystemServices().locale()));
    const localization::Localization& words = m_product != nullptr ? m_product->localization() : system;
    const std::string detail = error.message + " (code \"" + error.code + "\")" + (error.detail.empty() ? std::string() : "\n" + error.detail);
    platform::NativeDialogService::alertBlocking(words.translate("workpane.startup.failed-title"), words.translate("workpane.startup.failed-message") + "\n\n" + detail);
}

} // namespace workpane::app
