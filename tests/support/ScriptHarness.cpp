#include "support/ScriptHarness.h"

#include "scripting/HostReply.h"
#include "support/Resources.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>
#include <utility>

namespace workpane::tests {

ScriptHarness::ScriptHarness() {
    auto created = scripting::ScriptRuntime::create();
    EXPECT_TRUE(created.hasValue());
    m_runtime = std::move(created.value());
    m_replies = std::make_unique<scripting::ReplyChannel>(*m_runtime);
    // clang-format off
    function("test_result", [this](const nlohmann::json& argument) { results.push_back(argument); return scripting::HostReply::success(); });
    function("workpane_log", [this](const nlohmann::json& argument) { logged.push_back(argument); return scripting::HostReply::success(); });
    // clang-format on
}

void ScriptHarness::function(const std::string& name, scripting::ScriptRuntime::HostFunction host) {
    EXPECT_TRUE(m_runtime->registerFunction(name, scripting::ScriptRuntime::Effect::Screen, std::move(host)).hasValue());
}

// The chunk keeps its own reference to the host table, because the SDK takes the global one away the moment it loads.
Result<void> ScriptHarness::run(std::string_view source) {
    const std::string lua = (Resources::staged() / "lua").generic_string();
    const std::string prelude = "package.path = [==[" + lua + "/?.lua]==] .. ';' .. package.path\nlocal host = host\n";
    return m_runtime->loadString(prelude + std::string(source), "=test");
}

bool ScriptHarness::pollUntil(const std::function<bool()>& condition) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);

    while (std::chrono::steady_clock::now() < deadline) {
        m_runtime->poll();

        if (condition()) {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return false;
}

scripting::ScriptRuntime& ScriptHarness::runtime() {
    return *m_runtime;
}

scripting::ReplyChannel& ScriptHarness::replies() {
    return *m_replies;
}

} // namespace workpane::tests
