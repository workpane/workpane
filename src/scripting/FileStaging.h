#pragma once

#include "scripting/HostServices.h"
#include "scripting/ReplyChannel.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace workpane::scripting {

class FileStaging final {
  public:
    FileStaging(HostServices& services, ReplyChannel& replies);
    ~FileStaging();

    FileStaging(const FileStaging&) = delete;
    FileStaging& operator=(const FileStaging&) = delete;

    [[nodiscard]] nlohmann::json open(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json ready(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json close(const nlohmann::json& argument);

  private:
    struct Entry;

    void release(const std::shared_ptr<Entry>& entry, std::int64_t request);
    void reply(std::int64_t request, const Result<nlohmann::json>& result);

    HostServices& m_services;
    ReplyChannel& m_replies;
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};
    std::unordered_map<std::int64_t, std::shared_ptr<Entry>> m_entries;
    std::int64_t m_next{0};
};

} // namespace workpane::scripting
