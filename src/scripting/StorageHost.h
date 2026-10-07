#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "persistence/ConfigurationTransfer.h"
#include "persistence/Database.h"
#include "persistence/Execution.h"
#include "scripting/HostServices.h"
#include "scripting/ReplyChannel.h"
#include "scripting/ScriptRuntime.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::scripting {

// The host functions that read and write preference documents, reach the scoped tables of each plugin and erase the data of a plugin, always in the background.
class StorageHost final {
  public:
    StorageHost(HostServices& services, ScriptRuntime& runtime, ReplyChannel& replies);

    StorageHost(const StorageHost&) = delete;
    StorageHost& operator=(const StorageHost&) = delete;

    [[nodiscard]] Result<void> registerFunctions();

  private:
    static constexpr std::int64_t largestRequest{9007199254740991};

    [[nodiscard]] static Result<std::vector<persistence::Value>> bindings(const json::Json& values);
    [[nodiscard]] static nlohmann::json execution(const persistence::Execution& execution);
    [[nodiscard]] static std::filesystem::path path(const std::string& text);
    [[nodiscard]] static Result<std::string> owner(const std::string& plugin, const std::string& document);

    [[nodiscard]] nlohmann::json readPreferences(const nlohmann::json& argument) const;
    [[nodiscard]] nlohmann::json writePreferences(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json pluginData(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json erasePluginData(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json migrate(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json query(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json run(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json transaction(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json exportConfiguration(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json importConfiguration(const nlohmann::json& argument);
    void submit(std::int64_t request, std::function<Result<nlohmann::json>(persistence::Database&)> work);
    [[nodiscard]] nlohmann::json transfer(const std::string& plugin, std::int64_t request, const std::string& path, std::function<Result<void>(persistence::Database&, const std::filesystem::path&)> work);
    [[nodiscard]] static Result<persistence::ConfigurationTransfer::SchemaVersions> schemaVersions(const json::Json& schemas);

    HostServices& m_services;
    ScriptRuntime& m_runtime;
    ReplyChannel& m_replies;
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};
};

} // namespace workpane::scripting
