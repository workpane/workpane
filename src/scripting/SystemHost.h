#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "platform/DialogService.h"
#include "platform/FileFilter.h"
#include "scripting/HostServices.h"
#include "scripting/ReplyChannel.h"
#include "scripting/ScriptRuntime.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::scripting {

// The host functions that ask the operating system for something outside the product window.
class SystemHost final {
  public:
    SystemHost(HostServices& services, ScriptRuntime& runtime, ReplyChannel& replies);

    SystemHost(const SystemHost&) = delete;
    SystemHost& operator=(const SystemHost&) = delete;

    [[nodiscard]] Result<void> registerFunctions();

  private:
    static constexpr std::int64_t largestSystemRequest{9007199254740991};

    [[nodiscard]] static Result<std::vector<platform::FileFilter>> filters(const json::Json& values);
    [[nodiscard]] static std::optional<platform::MessageKind> kind(std::string_view name);

    [[nodiscard]] nlohmann::json openUrl(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json revealPath(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json openFiles(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json selectFolder(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json saveFile(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json message(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json notify(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json writeClipboard(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json readClipboard(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json information(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json monospaceFonts(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json home(const nlohmann::json& argument) const;
    [[nodiscard]] nlohmann::json shell(const nlohmann::json& argument) const;
    void answerPaths(std::int64_t request, Result<std::vector<std::string>> paths);

    HostServices& m_services;
    ScriptRuntime& m_runtime;
    ReplyChannel& m_replies;
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};
};

} // namespace workpane::scripting
