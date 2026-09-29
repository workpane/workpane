#pragma once

#include "Result.h"
#include "process/Process.h"
#include "process/ProcessEnd.h"
#include "process/ProcessStream.h"
#include "process/ProcessText.h"
#include "scripting/HostServices.h"
#include "scripting/ReplyChannel.h"
#include "scripting/ScriptRuntime.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace workpane::scripting {

// The host functions through which a plugin runs programs, delivering what each program wrote and the code it ended with to its owner once per turn of the loop.
class ProcessHost final {
  public:
    ProcessHost(HostServices& services, ScriptRuntime& runtime, ReplyChannel& replies);
    ~ProcessHost();

    ProcessHost(const ProcessHost&) = delete;
    ProcessHost& operator=(const ProcessHost&) = delete;

    [[nodiscard]] Result<void> registerFunctions();

  private:
    static constexpr std::int64_t largestRequest{9007199254740991};
    static constexpr std::size_t maximumArguments{256};
    static constexpr std::size_t maximumInput{64U << 20U};

    struct Pending final {
        std::vector<std::pair<process::ProcessStream, std::string>> chunks;
        std::optional<process::ProcessEnd> exit;
    };

    struct Mailbox final {
        std::mutex mutex;
        std::map<std::int64_t, Pending> pending;
        bool posted{false};
    };

    struct Running final {
        std::string owner;
        std::unique_ptr<process::Process> process;
        process::ProcessText output;
        process::ProcessText error;
        bool retiring{false};
    };

    [[nodiscard]] static std::string streamName(process::ProcessStream stream);

    [[nodiscard]] nlohmann::json start(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json write(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json stop(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json forget(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json find(const nlohmann::json& argument);
    [[nodiscard]] Result<Running*> owned(const std::string& plugin, std::int64_t process);
    void deliver();
    void retire(std::unique_ptr<process::Process> process);

    HostServices& m_services;
    ScriptRuntime& m_runtime;
    ReplyChannel& m_replies;
    std::shared_ptr<Mailbox> m_mailbox{std::make_shared<Mailbox>()};
    std::map<std::int64_t, Running> m_running;
    std::int64_t m_next{0};
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};
};

} // namespace workpane::scripting
