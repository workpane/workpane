#pragma once

#include "scripting/ApplicationInfo.h"

#include <nlohmann/json.hpp>

#include <functional>

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
}

namespace workpane::persistence {
class DatabaseExecutor;
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

namespace workpane::ui {
class RenderContext;
class Shell;
class SurfaceStore;
class ThemeManager;
} // namespace workpane::ui

namespace workpane::scripting {

class PluginRegistry;

// What the application hands the host functions, so they answer Lua without reaching anything global.
struct HostServices final {
    ApplicationInfo info;
    localization::Localization& localization;
    ui::ThemeManager& themes;
    persistence::PreferenceStore& preferences;
    persistence::DatabaseExecutor& database;
    audio::AudioOutput& audio;
    logging::LogService& logs;
    platform::DialogService& dialogs;
    platform::SystemServices& system;
    platform::SystemInspector& inspector;
    process::ProcessLauncher& processes;
    std::function<nlohmann::json()> displays;
    execution::WorkerPool& workers;
    execution::MainThreadQueue& mainThread;
    ui::SurfaceStore& surfaces;
    ui::RenderContext& render;
    ui::Shell& shell;
    PluginRegistry& plugins;
    std::function<void()> applyTheme;
    std::function<void()> requestQuit;
    std::function<void()> requestRestart;
    std::function<void()> stopped;
};

} // namespace workpane::scripting
