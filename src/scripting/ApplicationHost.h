#pragma once

#include "Result.h"
#include "scripting/HostServices.h"
#include "scripting/ScriptRuntime.h"
#include "ui/shell/ToastOverlay.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace workpane::scripting {

// The host functions that register plugins and read or change what belongs to the whole application.
class ApplicationHost final {
  public:
    ApplicationHost(HostServices& services, ScriptRuntime& runtime);

    [[nodiscard]] Result<void> registerFunctions();

  private:
    static constexpr std::int64_t earliestZoneSecond{-62135596800};
    static constexpr std::int64_t latestZoneSecond{253402300799};

    [[nodiscard]] static std::optional<ui::Severity> severity(std::string_view name);

    [[nodiscard]] nlohmann::json info(const nlohmann::json& argument) const;
    [[nodiscard]] nlohmann::json installCatalog(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json registerPlugin(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json removePlugin(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json registerCoreSettings(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json resizeBand(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json ready(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json stopped(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json translate(const nlohmann::json& argument) const;
    [[nodiscard]] nlohmann::json selectLanguage(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json selectTheme(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json log(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json notify(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json quit(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json restart(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json localTime(const nlohmann::json& argument) const;
    [[nodiscard]] nlohmann::json now(const nlohmann::json& argument) const;
    [[nodiscard]] nlohmann::json timeZone(const nlohmann::json& argument) const;
    [[nodiscard]] nlohmann::json zoneOffset(const nlohmann::json& argument) const;
    void publishContributions();

    HostServices& m_services;
    ScriptRuntime& m_runtime;
};

} // namespace workpane::scripting
