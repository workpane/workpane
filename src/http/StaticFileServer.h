#pragma once

#include "Result.h"
#include "http/RequestRecord.h"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace httplib {
class Server;
}

namespace workpane::http {

// A static web server for one folder, answering GET requests on threads of its own until it is stopped.
class StaticFileServer final {
  public:
    using RequestSink = std::function<void(RequestRecord record)>;

    [[nodiscard]] static Result<std::unique_ptr<StaticFileServer>> start(const std::string& host, int port, const std::filesystem::path& root, RequestSink sink);
    [[nodiscard]] static bool numericHost(const std::string& host);

    ~StaticFileServer();

    StaticFileServer(const StaticFileServer&) = delete;
    StaticFileServer& operator=(const StaticFileServer&) = delete;

    [[nodiscard]] const std::filesystem::path& root() const;
    void close();
    void stop();

  private:
    static constexpr std::size_t workerThreads{8};
    static constexpr std::size_t queuedConnections{128};
    static constexpr int headerSeconds{10};
    static constexpr std::size_t requestBytes{32 * 1024};
    static constexpr std::size_t chunkBytes{64 * 1024};

    [[nodiscard]] static std::string reason(int status);

    StaticFileServer(std::filesystem::path root, RequestSink sink);

    void route();

    std::filesystem::path m_root;
    RequestSink m_sink;
    std::unique_ptr<httplib::Server> m_server;
    std::thread m_thread;
    std::mutex m_joinMutex;
    std::atomic<bool> m_closing{false};
};

} // namespace workpane::http
