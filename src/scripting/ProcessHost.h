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

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
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
    static constexpr std::size_t maximumFindDirectories{256};
    static constexpr std::size_t maximumPath{4096};
    static constexpr std::int64_t largestRequest{9007199254740991};
    static constexpr std::size_t maximumPendingBytes{4U << 20U};
    static constexpr std::size_t maximumPendingChunks{4096};
    static constexpr std::size_t maximumPullBytes{1U << 20U};
    static constexpr std::size_t maximumPullChunks{1024};
    static constexpr std::size_t maximumReadBytes{65536};

    struct Pending final {
        std::deque<std::pair<process::ProcessStream, std::string>> chunks;
        std::size_t bytes{0};
        std::size_t offset{0};
        std::optional<process::ProcessEnd> exit;
    };

    struct Waiting final {
        std::int64_t request;
        std::size_t maximum;
    };

    struct Mailbox final {
        std::mutex mutex;
        std::condition_variable ready;
        std::map<std::int64_t, Pending> pending;
        std::size_t bytes{0};
        std::size_t chunks{0};
        bool posted{false};
        bool closed{false};
    };

    struct Running final {
        std::string owner;
        std::unique_ptr<process::Process> process;
        process::ProcessText output;
        process::ProcessText error;
        bool binaryOutput{false};
        bool pullOutput{false};
        std::optional<Waiting> reader;
        bool retiring{false};
        bool stopping{false};
    };

    [[nodiscard]] static std::string streamName(process::ProcessStream stream);
    [[nodiscard]] static Result<std::string> decodeInput(std::string_view content);

    [[nodiscard]] nlohmann::json start(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json write(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json endInput(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json read(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json resize(const nlohmann::json& argument);
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
