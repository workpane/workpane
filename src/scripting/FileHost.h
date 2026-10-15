#pragma once

#include "Result.h"
#include "scripting/FileStaging.h"
#include "scripting/HostServices.h"
#include "scripting/ReplyChannel.h"
#include "scripting/ScriptRuntime.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::scripting {

// The host functions through which a plugin lists folders, walks trees, searches text, resolves paths and moves a written file over another one, each done on a worker and answered by its request.
// A listing, walk, search, digest or read still running when the host goes away stops between entries or chunks.
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
    static constexpr std::size_t largestHashBytes{4U << 20U};
    static constexpr std::size_t largestSearchText{1000};
    static constexpr std::size_t largestSkipped{64};

    [[nodiscard]] static Result<std::filesystem::path> filePath(std::string_view text);

    [[nodiscard]] nlohmann::json list(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json walk(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json search(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json canonical(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json stat(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json access(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json replace(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json digest(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json hash(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json read(const nlohmann::json& argument);
    void answer(std::int64_t request, std::function<Result<nlohmann::json>()> work);

    HostServices& m_services;
    ScriptRuntime& m_runtime;
    ReplyChannel& m_replies;
    FileStaging m_staging;
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};
    std::shared_ptr<std::atomic<bool>> m_stopping{std::make_shared<std::atomic<bool>>(false)};
};

} // namespace workpane::scripting
