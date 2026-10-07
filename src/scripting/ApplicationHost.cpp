#include "scripting/ApplicationHost.h"

#include "json/ObjectReader.h"
#include "localization/Localization.h"
#include "localization/TextArgument.h"
#include "logging/LogLevels.h"
#include "logging/LogService.h"
#include "persistence/DatabaseBootstrap.h"
#include "platform/PathTextHelper.h"
#include "platform/SystemServices.h"
#include "scripting/ApplicationInfo.h"
#include "scripting/HostOwners.h"
#include "scripting/HostReply.h"
#include "scripting/Identifier.h"
#include "scripting/PluginRegistry.h"
#include "time/TimestampHelper.h"
#include "ui/IconCatalog.h"
#include "ui/model/RenderContext.h"
#include "ui/model/SurfaceStore.h"
#include "ui/shell/Shell.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeColorNames.h"
#include "ui/theme/ThemeManager.h"

#include <cstdint>
#include <limits>
#include <tuple>
#include <utility>
#include <vector>

namespace workpane::scripting {

std::optional<ui::Severity> ApplicationHost::severity(std::string_view name) {
    if (name == "information") {
        return ui::Severity::Information;
    }

    if (name == "success") {
        return ui::Severity::Success;
    }

    if (name == "warning") {
        return ui::Severity::Warning;
    }

    if (name == "error") {
        return ui::Severity::Error;
    }

    return std::nullopt;
}

ApplicationHost::ApplicationHost(HostServices& services, ScriptRuntime& runtime) : m_services(services), m_runtime(runtime) {}

Result<void> ApplicationHost::registerFunctions() {
    // clang-format off
    const std::vector<std::tuple<std::string, ScriptRuntime::Effect, ScriptRuntime::HostFunction>> functions{
        {"workpane_app_info", ScriptRuntime::Effect::Background, [this](const nlohmann::json& argument) { return info(argument); }},
        {"workpane_plugin_catalog", ScriptRuntime::Effect::Screen, [this](const nlohmann::json& argument) { return installCatalog(argument); }},
        {"workpane_plugin_register", ScriptRuntime::Effect::Screen, [this](const nlohmann::json& argument) { return registerPlugin(argument); }},
        {"workpane_plugin_remove", ScriptRuntime::Effect::Screen, [this](const nlohmann::json& argument) { return removePlugin(argument); }},
        {"workpane_core_settings", ScriptRuntime::Effect::Screen, [this](const nlohmann::json& argument) { return registerCoreSettings(argument); }},
        {"workpane_band_resize", ScriptRuntime::Effect::Screen, [this](const nlohmann::json& argument) { return resizeBand(argument); }},
        {"workpane_ready", ScriptRuntime::Effect::Screen, [this](const nlohmann::json& argument) { return ready(argument); }},
        {"workpane_stopped", ScriptRuntime::Effect::Screen, [this](const nlohmann::json& argument) { return stopped(argument); }},
        {"workpane_translate", ScriptRuntime::Effect::Background, [this](const nlohmann::json& argument) { return translate(argument); }},
        {"workpane_language_select", ScriptRuntime::Effect::Screen, [this](const nlohmann::json& argument) { return selectLanguage(argument); }},
        {"workpane_theme_select", ScriptRuntime::Effect::Screen, [this](const nlohmann::json& argument) { return selectTheme(argument); }},
        {"workpane_log", ScriptRuntime::Effect::Background, [this](const nlohmann::json& argument) { return log(argument); }},
        {"workpane_notify", ScriptRuntime::Effect::Screen, [this](const nlohmann::json& argument) { return notify(argument); }},
        {"workpane_quit", ScriptRuntime::Effect::Screen, [this](const nlohmann::json& argument) { return quit(argument); }},
        {"workpane_restart", ScriptRuntime::Effect::Screen, [this](const nlohmann::json& argument) { return restart(argument); }},
        {"workpane_time_local", ScriptRuntime::Effect::Background, [this](const nlohmann::json& argument) { return localTime(argument); }},
        {"workpane_time_now", ScriptRuntime::Effect::Background, [this](const nlohmann::json& argument) { return now(argument); }},
        {"workpane_time_zone", ScriptRuntime::Effect::Background, [this](const nlohmann::json& argument) { return timeZone(argument); }},
        {"workpane_time_offset", ScriptRuntime::Effect::Background, [this](const nlohmann::json& argument) { return zoneOffset(argument); }},
    };
    // clang-format on

    for (const auto& [name, effect, function] : functions) {
        if (const auto registered = m_runtime.registerFunction(name, effect, function); !registered.hasValue()) {
            return registered;
        }
    }

    return Result<void>::success();
}

nlohmann::json ApplicationHost::info(const nlohmann::json&) const {
    const ApplicationInfo& info = m_services.info;
    nlohmann::json languages = nlohmann::json::array();
    nlohmann::json themes = nlohmann::json::array();

    for (const auto& language : localization::Localization::languages()) {
        languages.push_back({{"id", language.id}, {"titleKey", language.titleKey}});
    }

    for (const auto& theme : m_services.themes.catalog().themes()) {
        themes.push_back({{"id", theme->id()}, {"titleKey", theme->titleKey()}});
    }

    // The painted icons and the color roles are named here, so a plugin offers exactly what a component accepts.
    nlohmann::json icons = nlohmann::json::array();
    nlohmann::json colors = nlohmann::json::array();

    for (const auto name : ui::IconCatalog::productNames()) {
        icons.push_back(name);
    }

    for (const auto& [role, name] : ui::ThemeColorNames::all()) {
        colors.push_back(name);
    }

    nlohmann::json plugins = nlohmann::json::array();

    for (const auto& folder : info.plugins) {
        plugins.push_back(platform::PathTextHelper::generic(folder));
    }

    return HostReply::success({{"version", info.version}, {"debug", info.debug}, {"platform", info.platform}, {"architecture", info.architecture}, {"processId", m_services.system.processId()}, {"paths", {{"resources", platform::PathTextHelper::generic(info.resources)}, {"plugins", plugins}, {"data", platform::PathTextHelper::generic(info.data)}, {"database", platform::PathTextHelper::generic(info.data / persistence::DatabaseBootstrap::databaseName)}}}, {"language", m_services.localization.language()}, {"theme", m_services.themes.theme().id()}, {"languages", languages}, {"themes", themes}, {"icons", icons}, {"colors", colors}});
}

// The catalog of a discovered plugin is installed before the plugin runs and stays while it is disabled, so the list of plugins speaks its title in the language of the reader, and a new reading of the plugin replaces it.
nlohmann::json ApplicationHost::installCatalog(const nlohmann::json& argument) {
    std::string plugin;
    const json::Json* translations = nullptr;
    json::ObjectReader reader(argument, "plugin.catalog");
    reader.readText("plugin", plugin).readObject("translations", translations);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (!Identifier::valid(plugin) || plugin == localization::Localization::coreOwner) {
        return HostReply::failure({"plugin_identifier_invalid", "A catalog is installed for the identifier of a plugin", plugin});
    }

    if (m_services.plugins.find(plugin) != nullptr) {
        return HostReply::failure({"plugin_running", "The catalog of a running plugin is not replaced", plugin});
    }

    auto catalog = PluginRegistry::parseCatalog(*translations);

    if (!catalog.hasValue()) {
        return HostReply::failure(catalog.error());
    }

    m_services.localization.unregisterCatalog(plugin);
    const auto installed = m_services.localization.registerCatalog(plugin, catalog.value());

    return installed.hasValue() ? HostReply::success() : HostReply::failure(installed.error());
}

nlohmann::json ApplicationHost::registerPlugin(const nlohmann::json& argument) {
    auto manifest = PluginRegistry::parse(argument, m_services.info.plugins, m_services.localization);

    if (!manifest.hasValue()) {
        return HostReply::failure(manifest.error());
    }

    if (const auto added = m_services.plugins.add(std::move(manifest.value())); !added.hasValue()) {
        return HostReply::failure(added.error());
    }

    publishContributions();

    return HostReply::success();
}

// A plugin that stops takes its destinations, bands, settings, dialogs and surfaces with it, while its catalog stays for the list of plugins.
nlohmann::json ApplicationHost::removePlugin(const nlohmann::json& argument) {
    std::string plugin;
    json::ObjectReader reader(argument, "plugin.remove");
    reader.readText("plugin", plugin);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (m_services.plugins.find(plugin) == nullptr) {
        return HostReply::failure({"plugin_unknown", "Only a registered plugin can be removed", plugin});
    }

    m_services.plugins.remove(plugin);
    m_services.shell.dialogs().dismissOwner(plugin);
    m_services.surfaces.unmountOwner(plugin, m_services.render);
    publishContributions();

    return HostReply::success();
}

nlohmann::json ApplicationHost::registerCoreSettings(const nlohmann::json& argument) {
    const json::Json* groups = nullptr;
    json::ObjectReader reader(argument, "core.settings");
    reader.readArray("settings", groups);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto added = m_services.plugins.addCoreSettings(*groups, m_services.localization); !added.hasValue()) {
        return HostReply::failure(added.error());
    }

    publishContributions();

    return HostReply::success();
}

// A plugin changes the height of its own band, and a height of zero hides the band while its components keep running.
nlohmann::json ApplicationHost::resizeBand(const nlohmann::json& argument) {
    std::string plugin;
    std::string item;
    std::int64_t height = 0;
    json::ObjectReader reader(argument, "band.resize");
    reader.readText("plugin", plugin).readText("item", item).readInteger("height", height, std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max());

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    if (const auto resized = m_services.plugins.resizeBand(plugin, item, height); !resized.hasValue()) {
        return HostReply::failure(resized.error());
    }

    publishContributions();

    return HostReply::success();
}

nlohmann::json ApplicationHost::ready(const nlohmann::json&) {
    m_services.shell.setReady(true);

    return HostReply::success();
}

nlohmann::json ApplicationHost::stopped(const nlohmann::json&) {
    m_services.stopped();

    return HostReply::success();
}

nlohmann::json ApplicationHost::translate(const nlohmann::json& argument) const {
    std::string key;
    const json::Json* values = &json::ObjectReader::emptyList();
    json::ObjectReader reader(argument, "translate");
    reader.readText("key", key).readArray("args", values, json::Presence::Optional);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    std::vector<std::string> arguments;

    for (const auto& value : *values) {
        const auto parsed = localization::TextArgument::parse(value, "translate.args");

        if (!parsed.hasValue()) {
            return HostReply::failure(parsed.error());
        }

        arguments.push_back(parsed.value().resolve(m_services.localization));
    }

    return HostReply::success(m_services.localization.translate(key, arguments));
}

nlohmann::json ApplicationHost::selectLanguage(const nlohmann::json& argument) {
    std::string language;
    json::ObjectReader reader(argument, "language.select");
    reader.readText("language", language);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto selected = m_services.localization.selectLanguage(language); !selected.hasValue()) {
        return HostReply::failure(selected.error());
    }

    m_runtime.emit("workpane.language.changed", {{"language", language}});

    return HostReply::success();
}

nlohmann::json ApplicationHost::selectTheme(const nlohmann::json& argument) {
    std::string theme;
    json::ObjectReader reader(argument, "theme.select");
    reader.readText("theme", theme);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto selected = m_services.themes.selectTheme(theme); !selected.hasValue()) {
        return HostReply::failure(selected.error());
    }

    m_services.applyTheme();
    m_runtime.emit("workpane.theme.changed", {{"theme", theme}});

    return HostReply::success();
}

nlohmann::json ApplicationHost::log(const nlohmann::json& argument) {
    std::string plugin;
    std::string levelName;
    std::string category;
    std::string message;
    const json::Json* details = &json::ObjectReader::emptyObject();
    json::ObjectReader reader(argument, "log");
    reader.readText("plugin", plugin).readText("level", levelName).readText("category", category).readText("message", message).readObject("details", details, json::Presence::Optional);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    const auto level = logging::LogLevels::parse(levelName);

    if (!level.has_value()) {
        return HostReply::failure({"log_level_invalid", "A log level is debug, info, warning or error", levelName});
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    m_services.logs.write(*level, plugin, category, message, *details);

    return HostReply::success();
}

nlohmann::json ApplicationHost::notify(const nlohmann::json& argument) {
    std::string plugin;
    std::string title;
    std::string message;
    std::string severity = "information";
    json::ObjectReader reader(argument, "notify");
    reader.readText("plugin", plugin).readText("title", title).read("message", message, json::Presence::Optional).read("severity", severity, json::Presence::Optional);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    const auto parsed = ApplicationHost::severity(severity);

    if (!parsed.has_value()) {
        return HostReply::failure({"notify_severity_invalid", "A severity is information, success, warning or error", severity});
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    m_services.shell.toasts().show(title, message, *parsed, m_services.render.time());

    return HostReply::success();
}

nlohmann::json ApplicationHost::quit(const nlohmann::json&) {
    m_services.requestQuit();

    return HostReply::success();
}

nlohmann::json ApplicationHost::restart(const nlohmann::json&) {
    m_services.requestRestart();

    return HostReply::success();
}

nlohmann::json ApplicationHost::localTime(const nlohmann::json& argument) const {
    std::string timestamp;
    json::ObjectReader reader(argument, "time.local");
    reader.readText("timestamp", timestamp);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    const auto instant = time::TimestampHelper::parseStoredTimestamp(timestamp);
    const auto presented = instant.has_value() ? time::TimestampHelper::localPresentation(*instant) : std::nullopt;

    if (!presented.has_value()) {
        return HostReply::failure({"time_timestamp_invalid", "A timestamp is not a stored UTC moment the platform can present", timestamp});
    }

    return HostReply::success(*presented);
}

nlohmann::json ApplicationHost::now(const nlohmann::json&) const {
    return HostReply::success(time::TimestampHelper::storedTimestamp(time::TimestampHelper::now()));
}

nlohmann::json ApplicationHost::timeZone(const nlohmann::json&) const {
    const auto zone = m_services.system.timeZone();

    if (!zone.hasValue()) {
        return HostReply::failure(zone.error());
    }

    return HostReply::success(zone.value());
}

// The offset a zone has at an instant is what turns a wall clock written in that zone into the instant it names.
nlohmann::json ApplicationHost::zoneOffset(const nlohmann::json& argument) const {
    std::string zone;
    std::int64_t seconds = 0;
    json::ObjectReader reader(argument, "time.offset");
    reader.readText("zone", zone).readInteger("seconds", seconds, earliestZoneSecond, latestZoneSecond);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    const auto offset = m_services.system.zoneOffset(zone, seconds);

    if (!offset.hasValue()) {
        return HostReply::failure(offset.error());
    }

    return HostReply::success(offset.value());
}

void ApplicationHost::publishContributions() {
    m_services.shell.setNavigation(m_services.plugins.navigation());
    m_services.shell.setBands(m_services.plugins.bands());
    m_services.shell.setSettingsGroups(m_services.plugins.settingsGroups());
}

} // namespace workpane::scripting
