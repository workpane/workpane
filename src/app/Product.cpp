#include "app/Product.h"

#include "BuildInfo.h"
#include "app/CorePreferences.h"
#include "audio/AudioOutput.h"
#include "execution/MainThreadQueue.h"
#include "execution/WorkerPool.h"
#include "localization/CoreCatalog.h"
#include "localization/Localization.h"
#include "logging/LogEntry.h"
#include "logging/LogLevels.h"
#include "logging/LogService.h"
#include "persistence/DatabaseBootstrap.h"
#include "persistence/DatabaseExecutor.h"
#include "persistence/InstanceLock.h"
#include "persistence/PreferenceStore.h"
#include "platform/PathText.h"
#include "platform/ProcessSignals.h"
#include "platform/SystemInspector.h"
#include "platform/SystemServices.h"
#include "process/ProcessLauncher.h"
#include "scripting/ApplicationHost.h"
#include "scripting/ApplicationInfo.h"
#include "scripting/AudioHost.h"
#include "scripting/FileHost.h"
#include "scripting/HostServices.h"
#include "scripting/HttpHost.h"
#include "scripting/InterfaceHost.h"
#include "scripting/PluginRegistry.h"
#include "scripting/ProcessHost.h"
#include "scripting/ReplyChannel.h"
#include "scripting/ScriptRuntime.h"
#include "scripting/StorageHost.h"
#include "scripting/SystemHost.h"
#include "time/Timestamps.h"
#include "ui/FontFamilies.h"
#include "ui/Fonts.h"
#include "ui/NativeViewHost.h"
#include "ui/PseudoTerminalHost.h"
#include "ui/TextureCache.h"
#include "ui/components/StandardComponents.h"
#include "ui/model/ComponentRegistry.h"
#include "ui/model/EventSink.h"
#include "ui/model/RenderContext.h"
#include "ui/model/SurfaceStore.h"
#include "ui/shell/DialogAnswer.h"
#include "ui/shell/DialogHost.h"
#include "ui/shell/DialogRequest.h"
#include "ui/shell/NavigationItem.h"
#include "ui/shell/SettingsGroup.h"
#include "ui/shell/SettingsSection.h"
#include "ui/shell/SettingsView.h"
#include "ui/shell/Shell.h"
#include "ui/shell/ToastOverlay.h"
#include "ui/theme/Style.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace workpane::app {

std::string Product::platformName() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

std::string Product::architectureName() {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#else
    return "x86_64";
#endif
}

std::string Product::luaString(const std::filesystem::path& path) {
    return "[==[" + platform::PathText::utf8(path) + "]==]";
}

nlohmann::json Product::logEntry(const logging::LogEntry& entry) {
    return {{"timestampUtc", time::Timestamps::storedTimestamp(entry.timestamp)}, {"source", entry.source}, {"level", logging::LogLevels::name(entry.level)}, {"category", entry.category}, {"message", entry.message}, {"details", entry.details}};
}

Product::Product(ApplicationPaths paths, std::unique_ptr<platform::SystemServices> system, std::unique_ptr<platform::SystemInspector> inspector, std::unique_ptr<ui::PseudoTerminalHost> terminals, std::unique_ptr<process::ProcessLauncher> processes, std::unique_ptr<audio::AudioOutput> audio) : m_paths(std::move(paths)), m_system(std::move(system)), m_inspector(std::move(inspector)), m_terminals(std::move(terminals)), m_processes(std::move(processes)), m_audio(std::move(audio)) {}

Product::~Product() = default;

Result<std::unique_ptr<Product>> Product::open(ApplicationPaths paths, std::unique_ptr<platform::SystemServices> system, std::unique_ptr<platform::SystemInspector> inspector, std::unique_ptr<ui::PseudoTerminalHost> terminals, std::unique_ptr<process::ProcessLauncher> processes, std::unique_ptr<audio::AudioOutput> audio) {
    std::unique_ptr<Product> product(new Product(std::move(paths), std::move(system), std::move(inspector), std::move(terminals), std::move(processes), std::move(audio)));

    if (const auto booted = product->bootstrap(); !booted.hasValue()) {
        return Result<std::unique_ptr<Product>>::failure(booted.error());
    }

    return Result<std::unique_ptr<Product>>::success(std::move(product));
}

Result<void> Product::bootstrap() {
    platform::ProcessSignals::ignoreBrokenPipes();
    m_logs = std::make_unique<logging::LogService>();
    m_localization = std::make_unique<localization::Localization>();

    if (const auto registered = m_localization->registerCatalog(localization::Localization::coreOwner, localization::CoreCatalog::catalog()); !registered.hasValue()) {
        return registered;
    }

    // The language of the system speaks until the stored preference is known, so every failure below is told in the language of the reader.
    std::ignore = m_localization->selectLanguage(localization::Localization::resolveLanguage(m_system->locale()));
    std::error_code error;
    std::filesystem::create_directories(m_paths.data, error);

    if (error) {
        return Result<void>::failure({"data_directory_unwritable", "The data directory could not be created", m_paths.data.string() + ": " + error.message()});
    }

    // The lock is taken before the database is touched, so a second instance never opens what the first one is writing.
    auto lock = persistence::InstanceLock::acquire(m_paths.data);

    if (!lock.hasValue()) {
        const bool running = lock.error().code == "instance_already_running";
        return Result<void>::failure({lock.error().code, running ? m_localization->translate("workpane.startup.already-running") : lock.error().message, lock.error().detail});
    }

    m_recovered = lock.value().recovered();
    m_lock = std::make_unique<persistence::InstanceLock>(std::move(lock.value()));
    auto opened = persistence::DatabaseBootstrap::open(m_paths.data);

    if (!opened.hasValue()) {
        return Result<void>::failure(opened.error());
    }

    m_setAside = opened.value().setAside;
    m_importFailure = opened.value().importFailure;
    auto loaded = persistence::PreferenceStore::load(opened.value().database);

    if (!loaded.hasValue()) {
        return Result<void>::failure(loaded.error());
    }

    for (const auto& owner : loaded.value().discarded) {
        m_logs->write(logging::LogLevel::Warning, std::string(localization::Localization::coreOwner), "preferences", "A stored preference document could not be read and starts empty", {{"owner", owner}});
    }

    m_mainThread = std::make_unique<execution::MainThreadQueue>();
    // clang-format off
    m_terminals->listen([queue = m_mainThread.get()]() { queue->post([]() {}); });
    // clang-format on
    m_workers = std::make_unique<execution::WorkerPool>(std::max<std::size_t>(minimumWorkers, std::thread::hardware_concurrency() / 2));
    m_database = std::make_unique<persistence::DatabaseExecutor>(std::move(opened.value().database), *m_mainThread);
    m_preferences = std::make_unique<persistence::PreferenceStore>(*m_database, std::move(loaded.value().documents));

    // The core document carries the language and the theme the reader chose, and a missing or unknown value keeps the rule of the first run.
    const CorePreferences core(m_preferences->document(localization::Localization::coreOwner));
    m_themes = std::make_unique<ui::ThemeManager>();
    m_themes->loadStoredTheme(core.theme());

    if (localization::Localization::supportedLanguage(core.language())) {
        std::ignore = m_localization->selectLanguage(core.language());
    }

    return Result<void>::success();
}

Result<void> Product::attach(ProductInterface interface) {
    m_dialogs = std::move(interface.dialogs);
    m_nativeViews = std::move(interface.nativeViews);
    m_wake = std::move(interface.wake);
    m_themeApplied = std::move(interface.themeApplied);
    m_displays = std::move(interface.displays);
    m_scale = interface.scale;
    m_mainThread->setWakeHandler(m_wake);
    m_audio->setWakeHandler(m_wake);
    m_fonts = std::make_unique<ui::Fonts>();

    if (const auto loaded = m_fonts->load(interface.fonts, m_paths.fonts()); !loaded.hasValue()) {
        return loaded;
    }

    m_families = std::make_unique<ui::FontFamilies>(*m_fonts, interface.fonts, *m_system, *m_workers, *m_mainThread);
    // clang-format off
    m_families->setChangeHandler([this]() { m_redraw = true; });
    // clang-format on
    // clang-format off
    m_families->setFailureHandler([logs = m_logs.get()](const Error& error) {
        logs->write(logging::LogLevel::Warning, std::string(localization::Localization::coreOwner), "fonts", error.message, {{"code", error.code}, {"detail", error.detail}});
    });
    // clang-format on

    m_textures = std::make_unique<ui::TextureCache>(*m_workers, *m_mainThread);
    // clang-format off
    m_textures->setChangeHandler([this]() { m_redraw = true; });
    // clang-format on
    m_components = std::make_unique<ui::ComponentRegistry>();
    ui::StandardComponents::registerAll(*m_components);
    m_surfaces = std::make_unique<ui::SurfaceStore>(*m_components);
    m_events = std::make_unique<ui::EventSink>();
    m_render = std::make_unique<ui::RenderContext>(m_themes->theme(), *m_fonts, *m_families, *m_localization, *m_textures, *m_events, *m_nativeViews, *m_terminals, m_scale);

    // clang-format off
    const ui::Shell::ViewRequest viewRequest = [this](const ui::NavigationItem& item) { m_runtime->emit("workpane.view.open", {{"plugin", item.plugin}, {"item", item.id}, {"surface", item.surface()}}); };
    const ui::Shell::BandRequest bandRequest = [this](const ui::BandItem& item) { m_runtime->emit("workpane.band.open", {{"plugin", item.plugin}, {"item", item.id}, {"surface", item.surface()}}); };
    const ui::SettingsView::SectionRequest sectionRequest = [this](const ui::SettingsGroup& group, const ui::SettingsSection& section) { m_runtime->emit("workpane.settings.open", {{"plugin", group.owner}, {"group", group.id}, {"section", section.id}, {"surface", group.surface(section)}}); };
    const ui::Shell::ShortcutRequest shortcutRequest = [this](const ui::NavigationItem& item, const std::string& shortcut) { m_runtime->emit("workpane.shortcut", {{"plugin", item.plugin}, {"item", item.id}, {"shortcut", shortcut}}); };
    m_shell = std::make_unique<ui::Shell>(viewRequest, bandRequest, sectionRequest, [this]() { requestQuit(); }, shortcutRequest);
    // clang-format on
    applyTheme();

    return Result<void>::success();
}

Result<void> Product::start() {
    auto runtime = scripting::ScriptRuntime::create();

    if (!runtime.hasValue()) {
        return Result<void>::failure(runtime.error());
    }

    m_runtime = std::move(runtime.value());
    m_runtime->setWakeHandler(m_wake);
    m_plugins = std::make_unique<scripting::PluginRegistry>();
    // clang-format off
    scripting::ScriptRuntime::setConsole([logs = m_logs.get()](int level, std::string message) {
        const std::array<logging::LogLevel, 4> levels{logging::LogLevel::Debug, logging::LogLevel::Info, logging::LogLevel::Warning, logging::LogLevel::Error};
        logs->write(levels[static_cast<std::size_t>(std::clamp(level, 0, 3))], "lua", "console", std::move(message));
    });
    // clang-format on

    const scripting::ApplicationInfo info{std::string(BuildInfo::version), BuildInfo::debugBuild, platformName(), architectureName(), m_paths.resources, pluginFolders(), m_paths.data};
    // clang-format off
    m_hostServices = std::make_unique<scripting::HostServices>(scripting::HostServices{info, *m_localization, *m_themes, *m_preferences, *m_database, *m_audio, *m_logs, *m_dialogs, *m_system, *m_inspector, *m_processes, m_displays, *m_workers, *m_mainThread, *m_surfaces, *m_render, *m_shell, *m_plugins, [this]() { applyTheme(); }, [this]() { requestQuit(); }, [this]() { m_restart = true; m_quit = true; }, [this]() { m_stopped = true; }});
    // clang-format on
    m_replies = std::make_unique<scripting::ReplyChannel>(*m_runtime);
    m_applicationHost = std::make_unique<scripting::ApplicationHost>(*m_hostServices, *m_runtime);
    m_interfaceHost = std::make_unique<scripting::InterfaceHost>(*m_hostServices, *m_runtime, *m_replies);
    m_storageHost = std::make_unique<scripting::StorageHost>(*m_hostServices, *m_runtime, *m_replies);
    m_systemHost = std::make_unique<scripting::SystemHost>(*m_hostServices, *m_runtime, *m_replies);
    m_httpHost = std::make_unique<scripting::HttpHost>(*m_hostServices, *m_runtime, *m_replies);
    m_processHost = std::make_unique<scripting::ProcessHost>(*m_hostServices, *m_runtime, *m_replies);
    m_audioHost = std::make_unique<scripting::AudioHost>(*m_hostServices, *m_runtime);
    m_fileHost = std::make_unique<scripting::FileHost>(*m_hostServices, *m_runtime, *m_replies);

    for (const auto& registered : {m_applicationHost->registerFunctions(), m_interfaceHost->registerFunctions(), m_storageHost->registerFunctions(), m_systemHost->registerFunctions(), m_httpHost->registerFunctions(), m_processHost->registerFunctions(), m_audioHost->registerFunctions(), m_fileHost->registerFunctions()}) {
        if (!registered.hasValue()) {
            return registered;
        }
    }

    // The SDK is found beside the product resources, and the bootstrap it carries discovers and starts every plugin.
    const std::string path = "package.path = " + luaString(m_paths.lua() / "?.lua") + " .. ';' .. " + luaString(m_paths.lua() / "?" / "init.lua") + " .. ';' .. package.path";

    if (const auto configured = m_runtime->loadString(path, "=workpane.path"); !configured.hasValue()) {
        return configured;
    }

    // clang-format off
    m_logs->setDelivery([this]() { m_mainThread->post([this]() { deliverLogs(); }); });
    // clang-format on
    const auto loaded = m_runtime->loadFile(m_paths.lua() / "workpane" / "bootstrap.lua");
    m_bootstrapped = loaded.hasValue();

    return loaded;
}

// A database set aside or an import refused at startup is named to the reader once, because the data they held is somewhere else now.
void Product::reportStartupNotices(double now) {
    if (m_recovered) {
        m_shell->toasts().show(m_localization->translate("workpane.recovery.title"), m_localization->translate("workpane.recovery.message"), ui::Severity::Warning, now);
        m_logs->write(logging::LogLevel::Warning, std::string(localization::Localization::coreOwner), "startup", "The previous session did not close cleanly", {});
    }

    if (m_setAside.has_value()) {
        const std::vector<std::string> arguments{m_setAside->string()};
        m_shell->toasts().show(m_localization->translate("workpane.database.title"), m_localization->translate("workpane.database.replaced", arguments), ui::Severity::Warning, now);
        m_logs->write(logging::LogLevel::Warning, std::string(localization::Localization::coreOwner), "database", "The stored database could not be read and was set aside", {{"path", m_setAside->string()}});
    }

    if (m_importFailure.has_value()) {
        m_shell->toasts().show(m_localization->translate("workpane.database.title"), m_localization->translate("workpane.database.import-rejected"), ui::Severity::Error, now);
        m_logs->write(logging::LogLevel::Error, std::string(localization::Localization::coreOwner), "database", m_importFailure->message, {{"code", m_importFailure->code}, {"detail", m_importFailure->detail}});
    }
}

// Entries reach Lua together once per turn of the loop, so a subscriber that writes to the log while it reads it never feeds itself inside one poll.
void Product::deliverLogs() {
    const auto entries = m_logs->take();

    if (m_runtime == nullptr || entries.empty()) {
        return;
    }

    nlohmann::json batch = nlohmann::json::array();

    for (const auto& entry : entries) {
        batch.push_back(logEntry(entry));
    }

    m_runtime->emit("workpane.log.entries", batch);
}

void Product::pollScripts() {
    m_runtime->poll();
}

double Product::scriptIdleSeconds() const {
    return m_runtime->idleSeconds();
}

// Work queued for the main thread may still change the screen, and a change made since the last frame is something the reader has not seen yet.
bool Product::undrawn() const {
    return !m_mainThread->empty() || changed();
}

// Only a host call that can change the screen, a picture or a font that arrived and a dialog the product opened itself ask for a frame, so a poll that only logs, stores or reads draws nothing.
bool Product::changed() const {
    return m_redraw || m_runtime->changes() != m_drawnChanges;
}

bool Product::dialogPending() const {
    return m_dialogs->pending();
}

// Gives every source of work its turn after the loop wakes, and answers whether any of it changed what the next frame shows.
bool Product::service() {
    m_mainThread->drain();
    m_dialogs->poll();
    const bool native = m_nativeViews->pump();
    m_runtime->poll();
    m_audioHost->update();

    return native || changed();
}

void Product::watchNativeViews() {
    m_nativeViews->watch();
}

void Product::setScale(float scale) {
    if (scale == m_scale) {
        return;
    }

    m_scale = scale;
    m_render->setScale(scale);
    applyTheme();
}

void Product::draw(ImVec2 size, double now) {
    ui::Style::matchDensity(ImGui::GetIO().DisplayFramebufferScale.x);
    m_textures->update();
    m_drawnChanges = m_runtime->changes();
    m_redraw = false;
    m_render->beginFrame(now);
    m_render->resetFrameRequest();
    m_shell->draw(*m_render, *m_surfaces, size);
    m_surfaces->update(*m_render);
}

// A window that is not shown draws nothing, while its components keep working and their events keep reaching the plugins, and it answers whether a component asked for more work.
bool Product::updateHidden(double now) {
    m_render->beginFrame(now);
    m_render->resetFrameRequest();
    m_surfaces->update(*m_render);
    const bool delivered = deliverEvents();

    return m_render->frameRequested() || delivered;
}

void Product::dropFiles(std::vector<std::filesystem::path> paths, ImVec2 position) {
    m_render->dropFiles(std::move(paths), position);
}

void Product::compose(std::string text, std::size_t caret) {
    m_render->compose(std::move(text), caret);
}

// Native views learn which of them were placed, and the events of the frame reach Lua in one batch.
FrameDemand Product::finishFrame() {
    m_nativeViews->endFrame(ui::Shell::floatingWindows());
    const bool delivered = deliverEvents();

    // ImGui spreads input that arrives at once over several frames, so input still in its queue asks for the next frame at once.
    return {!GImGui->InputEventsQueue.empty(), m_render->frameRequested(), delivered, m_render->frameDeadline()};
}

// The events raised since the last delivery reach Lua in one batch, each with the properties it changed and the order of children it set.
bool Product::deliverEvents() {
    auto events = m_events->take();

    if (events.empty()) {
        return false;
    }

    nlohmann::json batch = nlohmann::json::array();

    for (auto& event : events) {
        nlohmann::json delivered{{"surface", std::move(event.surface)}, {"node", event.node}, {"name", std::move(event.name)}, {"value", std::move(event.value)}};

        if (!event.state.empty()) {
            delivered["state"] = std::move(event.state);
        }

        if (!event.order.empty()) {
            delivered["order"] = std::move(event.order);
        }

        batch.push_back(std::move(delivered));
    }

    m_runtime->emit("workpane.ui.events", {{"events", std::move(batch)}});

    return true;
}

// Every normal exit asks through one confirmation, and a window that never finished loading has nothing to confirm.
void Product::requestQuit() {
    if (!m_shell->ready()) {
        m_quit = true;
        return;
    }

    if (m_quitRequested) {
        return;
    }

    m_quitRequested = true;
    ui::DialogRequest request;
    request.kind = ui::DialogKind::Confirm;
    request.title = m_localization->translate("workpane.exit.title");
    request.message = m_localization->translate("workpane.exit.message");
    request.confirmText = m_localization->translate("workpane.exit.action");
    request.cancelText = m_localization->translate("workpane.actions.cancel");
    request.destructive = true;
    // clang-format off
    request.answer = [this](const ui::DialogAnswer& answer) {
        m_quitRequested = false;
        m_quit = answer.button == ui::DialogHost::confirmButton;
    };
    // clang-format on

    m_shell->dialogs().open(std::move(request));
    m_redraw = true;
    m_wake();
}

bool Product::quitting() const {
    return m_quit;
}

bool Product::restarting() const {
    return m_restart;
}

// Plugins stop while Lua still runs, because what they write as the product closes is what the next start reads, and the data directory is released last.
void Product::stop() {
    if (m_shell != nullptr) {
        m_shell->dialogs().cancelAll();
    }

    // Only a bootstrap that ran can answer the shutdown, so a start that failed before it quits at once, and what was logged before the shutdown reaches the plugins first.
    if (m_runtime != nullptr && m_bootstrapped) {
        deliverLogs();
        m_runtime->emit("workpane.shutdown", nlohmann::json::object());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(shutdownBudgetSeconds);

        // What the stops write to the log still reaches the Logs plugin, which stops last and drains it into the database.
        while (!m_stopped && std::chrono::steady_clock::now() < deadline) {
            m_mainThread->drain();
            deliverLogs();
            m_runtime->poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        m_runtime->emit("workpane.close", nlohmann::json::object());
        m_runtime->poll();
    }

    if (m_runtime != nullptr) {
        m_logs->clearDelivery();
        m_runtime->close();
    }

    if (m_surfaces != nullptr) {
        m_surfaces->unmountAll(*m_render);
    }

    m_fileHost.reset();
    m_audioHost.reset();
    m_processHost.reset();
    m_httpHost.reset();
    m_systemHost.reset();
    m_storageHost.reset();
    m_interfaceHost.reset();
    m_applicationHost.reset();
    m_runtime.reset();
    scripting::ScriptRuntime::setConsole(nullptr);

    if (m_database != nullptr) {
        m_database->shutdown();
    }

    if (m_workers != nullptr) {
        m_workers->shutdown();
    }

    if (m_audio != nullptr) {
        m_audio->setWakeHandler(nullptr);
    }

    if (m_mainThread != nullptr) {
        m_mainThread->setWakeHandler(nullptr);
        m_mainThread->drain();
    }

    m_nativeViews.reset();
    m_lock.reset();
}

// Textures leave the renderer while it still exists, which is before the window tears ImGui down.
void Product::releaseTextures() {
    if (m_textures != nullptr) {
        m_textures->release();
    }
}

// The bundled plugins come first, then the folders the reader added in the settings, as they stood when the product started.
std::vector<std::filesystem::path> Product::pluginFolders() const {
    std::vector<std::filesystem::path> folders{m_paths.plugins()};

    for (auto& folder : CorePreferences(m_preferences->document(localization::Localization::coreOwner)).pluginFolders()) {
        if (std::ranges::find(folders, folder) == folders.end()) {
            folders.push_back(std::move(folder));
        }
    }

    return folders;
}

void Product::applyTheme() {
    const ui::Theme& theme = m_themes->theme();
    ui::Style::apply(theme, *m_fonts, m_scale, ImGui::GetStyle());
    m_render->setTheme(theme);

    if (m_themeApplied) {
        m_themeApplied(theme);
    }
}

const ApplicationPaths& Product::paths() const {
    return m_paths;
}

localization::Localization& Product::localization() const {
    return *m_localization;
}

logging::LogService& Product::logs() const {
    return *m_logs;
}

persistence::PreferenceStore& Product::preferences() const {
    return *m_preferences;
}

platform::SystemServices& Product::system() const {
    return *m_system;
}

ui::Shell& Product::shell() const {
    return *m_shell;
}

ui::RenderContext& Product::render() const {
    return *m_render;
}

ui::SurfaceStore& Product::surfaces() const {
    return *m_surfaces;
}

scripting::ScriptRuntime& Product::runtime() const {
    return *m_runtime;
}

scripting::PluginRegistry& Product::plugins() const {
    return *m_plugins;
}

} // namespace workpane::app
