#pragma once

#include "Result.h"
#include "http/RequestRecord.h"
#include "http/StaticFileServer.h"
#include "scripting/HostServices.h"
#include "scripting/ReplyChannel.h"
#include "scripting/ScriptRuntime.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace workpane::scripting {

// The host functions through which a plugin serves a folder over HTTP, delivering the requests every server answered to its owner once per frame.
class HttpHost final {
  public:
    HttpHost(HostServices& services, ScriptRuntime& runtime, ReplyChannel& replies);
    ~HttpHost();

    HttpHost(const HttpHost&) = delete;
    HttpHost& operator=(const HttpHost&) = delete;

    [[nodiscard]] Result<void> registerFunctions();

  private:
    static constexpr std::int64_t largestRequest{9007199254740991};

    struct Hosted final {
        std::string owner;
        std::shared_ptr<http::StaticFileServer> server;
    };

    struct Mailbox;

    [[nodiscard]] nlohmann::json serve(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json stop(const nlohmann::json& argument);
    [[nodiscard]] nlohmann::json forget(const nlohmann::json& argument);
    void retire(std::shared_ptr<http::StaticFileServer> server);
    void deliver();

    HostServices& m_services;
    ScriptRuntime& m_runtime;
    ReplyChannel& m_replies;
    std::map<std::int64_t, Hosted> m_servers;
    std::vector<std::shared_ptr<http::StaticFileServer>> m_retired;
    std::shared_ptr<Mailbox> m_mailbox;
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};
};

} // namespace workpane::scripting
