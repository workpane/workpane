#pragma once

#include "Result.h"
#include "app/ApplicationPaths.h"
#include "app/FrameDemand.h"
#include "app/ProductInterface.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace workpane::audio {
class AudioOutput;
}

namespace workpane::execution {
class MainThreadQueue;
class WorkerPool;
} // namespace workpane::execution

namespace workpane::localization {
class Localization;
}

namespace workpane::logging {
class LogService;
struct LogEntry;
} // namespace workpane::logging

namespace workpane::persistence {
class DatabaseExecutor;
class InstanceLock;
class PreferenceStore;
} // namespace workpane::persistence

namespace workpane::platform {
class DialogService;
class SystemInspector;
class SystemServices;
} // namespace workpane::platform

namespace workpane::process {
class ProcessLauncher;
}

namespace workpane::scripting {
class ApplicationHost;
class AudioHost;
class InterfaceHost;
class PluginRegistry;
class ReplyChannel;
class ScriptRuntime;
class StorageHost;
class FileHost;
class HttpHost;
class ProcessHost;
class SystemHost;
struct HostServices;
} // namespace workpane::scripting

namespace workpane::ui {
class ComponentRegistry;
class EventSink;
class FontFamilies;
class Fonts;
class NativeViewHost;
class PseudoTerminalHost;
class RenderContext;
class Shell;
class SurfaceStore;
class TextureCache;
class Theme;
class ThemeManager;
} // namespace workpane::ui

namespace workpane::app {

// The product without its window: every service, the shell and the Lua runtime driving the plugins, composed the same way by the application and by the suite.
class Product final {
  public:
    ~Product();

    Product(const Product&) = delete;
    Product& operator=(const Product&) = delete;

    [[nodiscard]] static Result<std::unique_ptr<Product>> open(ApplicationPaths paths, std::unique_ptr<platform::SystemServices> system, std::unique_ptr<platform::SystemInspector> inspector, std::unique_ptr<ui::PseudoTerminalHost> terminals, std::unique_ptr<process::ProcessLauncher> processes, std::unique_ptr<audio::AudioOutput> audio);
    [[nodiscard]] Result<void> attach(ProductInterface interface);
    [[nodiscard]] Result<void> start();
    void reportStartupNotices(double now);

    void pollScripts();
    [[nodiscard]] double scriptIdleSeconds() const;
    [[nodiscard]] bool undrawn() const;
    [[nodiscard]] bool dialogPending() const;
    void watchNativeViews();
    [[nodiscard]] bool service();
    void setScale(float scale);
    void draw(ImVec2 size, double now);
    [[nodiscard]] bool updateHidden(double now);
    void dropFiles(std::vector<std::filesystem::path> paths, ImVec2 position);
    void compose(std::string text, std::size_t caret);
    [[nodiscard]] FrameDemand finishFrame();

    void requestQuit();
    [[nodiscard]] bool quitting() const;
    [[nodiscard]] bool restarting() const;
    void stop();
    void releaseTextures();

    [[nodiscard]] const ApplicationPaths& paths() const;
    [[nodiscard]] localization::Localization& localization() const;
    [[nodiscard]] logging::LogService& logs() const;
    [[nodiscard]] persistence::PreferenceStore& preferences() const;
    [[nodiscard]] platform::SystemServices& system() const;
    [[nodiscard]] ui::Shell& shell() const;
    [[nodiscard]] ui::RenderContext& render() const;
    [[nodiscard]] ui::SurfaceStore& surfaces() const;
    [[nodiscard]] scripting::ScriptRuntime& runtime() const;
    [[nodiscard]] scripting::PluginRegistry& plugins() const;

  private:
    [[nodiscard]] bool deliverEvents();
    [[nodiscard]] bool changed() const;
    static constexpr double shutdownBudgetSeconds{3.0};
    static constexpr std::size_t minimumWorkers{2};

    [[nodiscard]] std::vector<std::filesystem::path> pluginFolders() const;
    [[nodiscard]] static std::string platformName();
    [[nodiscard]] static std::string architectureName();
    [[nodiscard]] static std::string luaString(const std::filesystem::path& path);
    [[nodiscard]] static nlohmann::json logEntry(const logging::LogEntry& entry);
    void deliverLogs();

    Product(ApplicationPaths paths, std::unique_ptr<platform::SystemServices> system, std::unique_ptr<platform::SystemInspector> inspector, std::unique_ptr<ui::PseudoTerminalHost> terminals, std::unique_ptr<process::ProcessLauncher> processes, std::unique_ptr<audio::AudioOutput> audio);

    [[nodiscard]] Result<void> bootstrap();
    void applyTheme();

    ApplicationPaths m_paths;
    std::unique_ptr<platform::SystemServices> m_system;
    std::unique_ptr<platform::SystemInspector> m_inspector;
    std::unique_ptr<ui::PseudoTerminalHost> m_terminals;
    std::unique_ptr<process::ProcessLauncher> m_processes;
    std::unique_ptr<audio::AudioOutput> m_audio;
    std::unique_ptr<logging::LogService> m_logs;
    std::unique_ptr<localization::Localization> m_localization;
    std::unique_ptr<persistence::InstanceLock> m_lock;
    std::unique_ptr<execution::MainThreadQueue> m_mainThread;
    std::unique_ptr<execution::WorkerPool> m_workers;
    std::unique_ptr<persistence::DatabaseExecutor> m_database;
    std::unique_ptr<persistence::PreferenceStore> m_preferences;
    std::unique_ptr<ui::ThemeManager> m_themes;
    std::unique_ptr<platform::DialogService> m_dialogs;
    std::unique_ptr<ui::NativeViewHost> m_nativeViews;
    std::function<void()> m_wake;
    std::function<void(const ui::Theme& theme)> m_themeApplied;
    std::function<nlohmann::json()> m_displays;
    std::unique_ptr<ui::Fonts> m_fonts;
    std::unique_ptr<ui::FontFamilies> m_families;
    std::unique_ptr<ui::TextureCache> m_textures;
    std::unique_ptr<ui::ComponentRegistry> m_components;
    std::unique_ptr<ui::SurfaceStore> m_surfaces;
    std::unique_ptr<ui::EventSink> m_events;
    std::unique_ptr<ui::RenderContext> m_render;
    std::unique_ptr<ui::Shell> m_shell;
    std::unique_ptr<scripting::PluginRegistry> m_plugins;
    std::unique_ptr<scripting::ScriptRuntime> m_runtime;
    std::unique_ptr<scripting::HostServices> m_hostServices;
    std::unique_ptr<scripting::ReplyChannel> m_replies;
    std::unique_ptr<scripting::ApplicationHost> m_applicationHost;
    std::unique_ptr<scripting::InterfaceHost> m_interfaceHost;
    std::unique_ptr<scripting::StorageHost> m_storageHost;
    std::unique_ptr<scripting::SystemHost> m_systemHost;
    std::unique_ptr<scripting::HttpHost> m_httpHost;
    std::unique_ptr<scripting::FileHost> m_fileHost;
    std::unique_ptr<scripting::ProcessHost> m_processHost;
    std::unique_ptr<scripting::AudioHost> m_audioHost;
    std::optional<std::filesystem::path> m_setAside;
    bool m_recovered{false};
    std::optional<Error> m_importFailure;
    std::uint64_t m_drawnChanges{0};
    bool m_redraw{false};
    float m_scale{1.0F};
    bool m_quitRequested{false};
    bool m_quit{false};
    bool m_restart{false};
    bool m_stopped{false};
    bool m_bootstrapped{false};
};

} // namespace workpane::app
