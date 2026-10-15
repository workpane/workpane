#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "scripting/HostServices.h"
#include "scripting/ReplyChannel.h"
#include "scripting/ScriptRuntime.h"
#include "ui/model/NodeId.h"
#include "ui/shell/DialogButton.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string_view>
#include <vector>

namespace workpane::scripting {

// The host functions through which Lua declares, changes and removes the surfaces the shell draws, and asks for its dialogs.
class InterfaceHost final {
  public:
    InterfaceHost(HostServices& services, ScriptRuntime& runtime, ReplyChannel& replies);

    [[nodiscard]] Result<void> registerFunctions();

  private:
    static constexpr std::int64_t largestNode{9007199254740991};
    static constexpr double smallestDialog{320.0};
    static constexpr double largestDialog{1200.0};
    static constexpr double smallestText{6.0};
    static constexpr double largestText{128.0};

    [[nodiscard]] static bool ownsSurface(std::string_view owner, std::string_view surface);
    [[nodiscard]] Result<void> owns(std::string_view owner, std::string_view surface) const;
    [[nodiscard]] static ui::NodeId node(json::ObjectReader& reader);
    [[nodiscard]] static Result<std::vector<ui::DialogButton>> buttons(const json::Json& entries);

    [[nodiscard]] nlohmann::json mount(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json patch(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json children(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json command(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json unmount(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json failed(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json closeDialog(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json dialogButtons(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json navigate(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json dialog(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json measure(const nlohmann::json& argument) const;
    [[nodiscard]] std::filesystem::path assets(std::string_view owner) const;

    HostServices& m_services;
    ScriptRuntime& m_runtime;
    ReplyChannel& m_replies;
};

} // namespace workpane::scripting
