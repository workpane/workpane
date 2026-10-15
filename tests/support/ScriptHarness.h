#pragma once

#include "Result.h"
#include "scripting/ReplyChannel.h"
#include "scripting/ScriptRuntime.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::tests {

// A runtime with the SDK on its path, a result sink Lua writes to and the log function the SDK reports its own failures through.
class ScriptHarness final {
  public:
    ScriptHarness();

    void function(const std::string& name, scripting::ScriptRuntime::HostFunction host);
    Result<void> run(std::string_view source);
    bool pollUntil(const std::function<bool()>& condition);
    scripting::ScriptRuntime& runtime();
    scripting::ReplyChannel& replies();

    std::vector<nlohmann::json> results;
    std::vector<nlohmann::json> logged;

  private:
    std::unique_ptr<scripting::ScriptRuntime> m_runtime;
    std::unique_ptr<scripting::ReplyChannel> m_replies;
};

} // namespace workpane::tests
