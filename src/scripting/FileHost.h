#pragma once

#include "Result.h"
#include "scripting/HostServices.h"
#include "scripting/ReplyChannel.h"
#include "scripting/ScriptRuntime.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace workpane::scripting {

// The host functions through which a plugin lists folders, walks trees, searches text, resolves paths and moves a written file over another one, each done on a worker and answered by its request.
// A walk or a search still running when the host goes away stops at its next file, so quitting never waits for it.
class FileHost final {
  public:
    FileHost(HostServices& services, ScriptRuntime& runtime, ReplyChannel& replies);
    ~FileHost();

    FileHost(const FileHost&) = delete;
    FileHost& operator=(const FileHost&) = delete;

    [[nodiscard]] Result<void> registerFunctions();

  private:
    static constexpr std::int64_t largestRequest{9007199254740991};
    static constexpr std::int64_t largestCount{1000000};
    static constexpr std::int64_t largestFileBytes{64LL << 20};
    static constexpr std::size_t largestSearchText{1000};
    static constexpr std::size_t largestSkipped{64};

    [[nodiscard]] nlohmann::json list(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json walk(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json search(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json canonical(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json access(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json replace(const nlohmann::json& argument);
    void answer(std::int64_t request, std::function<Result<nlohmann::json>()> work);

    HostServices& m_services;
    ScriptRuntime& m_runtime;
    ReplyChannel& m_replies;
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};
    std::shared_ptr<std::atomic<bool>> m_stopping{std::make_shared<std::atomic<bool>>(false)};
};

} // namespace workpane::scripting
