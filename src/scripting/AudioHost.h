#pragma once

#include "Error.h"
#include "Result.h"
#include "scripting/HostServices.h"
#include "scripting/ScriptRuntime.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace workpane::scripting {

// The host functions that play the sound files of the assets of a plugin and stop them, forgetting each sound once it ends and telling the log why a sound was never heard.
class AudioHost final {
  public:
    AudioHost(HostServices& services, ScriptRuntime& runtime);

    AudioHost(const AudioHost&) = delete;
    AudioHost& operator=(const AudioHost&) = delete;

    [[nodiscard]] Result<void> registerFunctions();
    void update();

  private:
    static constexpr std::int64_t largestSound{9007199254740991};
    static constexpr std::array<std::string_view, 3> formats{".wav", ".flac", ".mp3"};

    [[nodiscard]] nlohmann::json play(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json stop(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json forget(const nlohmann::json& argument);
    void report(const std::string& plugin, const Error& failure);

    HostServices& m_services;
    ScriptRuntime& m_runtime;
    std::map<std::uint64_t, std::string> m_owners;
    bool m_silenceTold{false};
};

} // namespace workpane::scripting
