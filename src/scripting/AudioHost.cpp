#include "scripting/AudioHost.h"

#include "audio/AudioOutput.h"
#include "json/ObjectReader.h"
#include "localization/Localization.h"
#include "logging/LogService.h"
#include "scripting/HostReply.h"
#include "scripting/PluginRegistry.h"
#include "ui/AssetPath.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>
#include <system_error>
#include <utility>
#include <vector>

namespace workpane::scripting {

AudioHost::AudioHost(HostServices& services, ScriptRuntime& runtime) : m_services(services), m_runtime(runtime) {}

Result<void> AudioHost::registerFunctions() {
    // clang-format off
    const std::vector<std::pair<std::string, ScriptRuntime::HostFunction>> functions{
        {"workpane_audio_play", [this](const nlohmann::json& argument) { return play(argument); }},
        {"workpane_audio_stop", [this](const nlohmann::json& argument) { return stop(argument); }},
        {"workpane_audio_forget", [this](const nlohmann::json& argument) { return forget(argument); }},
    };
    // clang-format on

    for (const auto& [name, function] : functions) {
        if (const auto registered = m_runtime.registerFunction(name, ScriptRuntime::Effect::Background, function); !registered.hasValue()) {
            return registered;
        }
    }

    return Result<void>::success();
}

// The sounds that ended are forgotten, so an identity never names a sound that no longer plays, and a sound its plugin stopped is forgotten with the reason it was not heard.
void AudioHost::update() {
    for (const auto& ended : m_services.audio.update()) {
        const auto owner = m_owners.find(ended.sound);

        if (owner == m_owners.end()) {
            continue;
        }

        if (ended.failure.has_value()) {
            report(owner->second, *ended.failure);
        }

        m_owners.erase(owner);
    }
}

// A machine without an audio device is told once for the whole product, and any other sound that could not play is told in the log of its plugin.
void AudioHost::report(const std::string& plugin, const Error& failure) {
    const bool silent = failure.code == "audio_unavailable";

    if (silent && m_silenceTold) {
        return;
    }

    if (silent) {
        m_silenceTold = true;
        m_services.logs.write(logging::LogLevel::Warning, std::string(localization::Localization::coreOwner), "audio", failure.message, {{"code", failure.code}});

        return;
    }

    m_services.logs.write(logging::LogLevel::Warning, plugin, "audio", failure.message, {{"code", failure.code}, {"detail", failure.detail}});
}

// A sound is a WAV, FLAC or MP3 file inside the assets of its plugin, played at a volume from zero to one, once or in a loop.
nlohmann::json AudioHost::play(const nlohmann::json& argument) {
    std::string plugin;
    std::string path;
    double volume = 1.0;
    bool loop = false;
    json::ObjectReader reader(argument, "audio.play");
    reader.readText("plugin", plugin).readText("path", path).readNumber("volume", volume, -std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), json::Presence::Optional).read("loop", loop, json::Presence::Optional);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    const PluginManifest* manifest = m_services.plugins.find(plugin);

    if (manifest == nullptr) {
        return HostReply::failure({"plugin_unknown", "Only a registered plugin plays the sounds of its assets", plugin});
    }

    if (!ui::AssetPath::safe(path)) {
        return HostReply::failure({"audio_path_invalid", "A sound is a plain path inside the assets of its plugin", path});
    }

    if (volume < 0.0 || volume > 1.0) {
        return HostReply::failure({"audio_volume_invalid", "A sound plays at a volume from zero to one", std::to_string(volume)});
    }

    const std::filesystem::path file = manifest->directory / "assets" / std::filesystem::path(std::u8string(path.begin(), path.end()));
    std::error_code error;

    if (!std::filesystem::is_regular_file(file, error)) {
        return HostReply::failure({"audio_file_missing", "The assets of the plugin hold no such sound", path});
    }

    std::string extension = file.extension().string();
    // clang-format off
    std::ranges::transform(extension, extension.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    // clang-format on

    if (std::ranges::find(formats, extension) == formats.end()) {
        return HostReply::failure({"audio_format_unsupported", "A sound is a WAV, FLAC or MP3 file", path});
    }

    const std::uint64_t sound = m_services.audio.play(file, static_cast<float>(volume), loop);
    m_owners.emplace(sound, plugin);

    return HostReply::success({{"sound", sound}});
}

nlohmann::json AudioHost::stop(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t sound = 0;
    json::ObjectReader reader(argument, "audio.stop");
    reader.readText("plugin", plugin).readInteger("sound", sound, 1, largestSound);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    const auto found = m_owners.find(static_cast<std::uint64_t>(sound));

    if (found == m_owners.end() || found->second != plugin) {
        return HostReply::failure({"audio_sound_unknown", "The plugin plays no sound under that identity", std::to_string(sound)});
    }

    m_services.audio.stop(found->first);
    m_owners.erase(found);

    return HostReply::success();
}

// A plugin that stops, or that silences everything it plays, takes every one of its sounds with it.
nlohmann::json AudioHost::forget(const nlohmann::json& argument) {
    std::string plugin;
    json::ObjectReader reader(argument, "audio.forget");
    reader.readText("plugin", plugin);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    for (auto sound = m_owners.begin(); sound != m_owners.end();) {
        if (sound->second != plugin) {
            ++sound;
            continue;
        }

        m_services.audio.stop(sound->first);
        sound = m_owners.erase(sound);
    }

    return HostReply::success();
}

} // namespace workpane::scripting
