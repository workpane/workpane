#include "http/StaticFileServer.h"

#include "http/StaticFileResolver.h"
#include "time/Timestamps.h"

#include <httplib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <utility>
#include <vector>

namespace workpane::http {

// A server starts only on a numeric address, a valid port and a readable folder, and binding happens before it answers so an address in use is refused at once.
Result<std::unique_ptr<StaticFileServer>> StaticFileServer::start(const std::string& host, int port, const std::filesystem::path& root, RequestSink sink) {
    if (!numericHost(host)) {
        return Result<std::unique_ptr<StaticFileServer>>::failure({"http_host_invalid", "The bind host must be a numeric IP address", host});
    }

    if (port < 1 || port > 65535) {
        return Result<std::unique_ptr<StaticFileServer>>::failure({"http_port_invalid", "The port must be between 1 and 65535", std::to_string(port)});
    }

    const auto canonical = StaticFileResolver::canonicalRoot(root);

    if (!canonical.hasValue()) {
        return Result<std::unique_ptr<StaticFileServer>>::failure(canonical.error());
    }

    std::unique_ptr<StaticFileServer> server(new StaticFileServer(canonical.value(), std::move(sink)));
    server->route();

    if (!server->m_server->bind_to_port(host, port)) {
        return Result<std::unique_ptr<StaticFileServer>>::failure({"http_bind_failed", "The address is unavailable or the port is already in use", host + ":" + std::to_string(port)});
    }

    // clang-format off
    server->m_thread = std::thread([listening = server->m_server.get()]() { listening->listen_after_bind(); });
    // clang-format on
    return Result<std::unique_ptr<StaticFileServer>>::success(std::move(server));
}

bool StaticFileServer::numericHost(const std::string& host) {
    std::array<unsigned char, 16> address{};
    return inet_pton(AF_INET, host.c_str(), address.data()) == 1 || inet_pton(AF_INET6, host.c_str(), address.data()) == 1;
}

StaticFileServer::StaticFileServer(std::filesystem::path root, RequestSink sink) : m_root(std::move(root)), m_sink(std::move(sink)), m_server(std::make_unique<httplib::Server>()) {}

StaticFileServer::~StaticFileServer() {
    stop();
}

const std::filesystem::path& StaticFileServer::root() const {
    return m_root;
}

// Closing releases the port at once and cuts every transfer at its next chunk, without waiting for the threads that served them.
void StaticFileServer::close() {
    m_closing = true;
    m_server->stop();
}

// Stopping closes the server and returns once its listening thread and every connection it served have ended.
void StaticFileServer::stop() {
    close();
    const std::lock_guard lock(m_joinMutex);

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

std::string StaticFileServer::reason(int status) {
    return status == 404 ? "Not Found" : "Bad Request";
}

// Every answer closes its connection and is logged with the moment it arrived, its duration and the bytes it carried.
void StaticFileServer::route() {
    // clang-format off
    m_server->new_task_queue = []() { return new httplib::ThreadPool(workerThreads, workerThreads, queuedConnections); };
    // clang-format on

    // A port another listener holds is refused, so the socket may reuse a port left in waiting but never share one that is in use.
    // clang-format off
    m_server->set_socket_options([](socket_t socket) {
#if defined(_WIN32)
        httplib::set_socket_opt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1);
#else
        httplib::set_socket_opt(socket, SOL_SOCKET, SO_REUSEADDR, 1);
#endif
    });
    // clang-format on

    m_server->set_keep_alive_max_count(1);
    m_server->set_keep_alive_timeout(headerSeconds);
    m_server->set_read_timeout(headerSeconds, 0);
    m_server->set_payload_max_length(requestBytes);

    // clang-format off
    m_server->set_pre_routing_handler([](const httplib::Request& request, httplib::Response& response) {
        response.user_data.set("started", std::chrono::steady_clock::now());
        response.user_data.set("arrived", time::Timestamps::storedTimestamp(time::Timestamps::now()));

        if (request.method == "GET") {
            return httplib::Server::HandlerResponse::Unhandled;
        }

        response.status = 400;
        response.set_content(reason(400), "text/plain");

        return httplib::Server::HandlerResponse::Handled;
    });

    m_server->Get(R"(.*)", [this](const httplib::Request& request, httplib::Response& response) {
        const auto resolved = StaticFileResolver::resolve(m_root, request.target);

        if (!resolved.hasValue()) {
            response.status = resolved.error().code == "http_file_missing" ? 404 : 400;
            response.set_content(reason(response.status), "text/plain");
            return;
        }

        auto stream = std::make_shared<std::ifstream>(resolved.value().path, std::ios::binary);
        response.set_header("Cache-Control", "no-store");
        response.user_data.set("bytes", static_cast<std::int64_t>(resolved.value().size));
        response.set_content_provider(static_cast<std::size_t>(resolved.value().size), resolved.value().mimeType, [this, stream](std::size_t offset, std::size_t length, httplib::DataSink& sink) {
            if (m_closing) {
                return false;
            }

            std::vector<char> buffer(std::min(length, chunkBytes));
            stream->seekg(static_cast<std::streamoff>(offset));
            stream->read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize read = stream->gcount();

            // A file that shrank while it was sent ends the connection rather than padding what it no longer holds.
            if (read <= 0) {
                return false;
            }

            return sink.write(buffer.data(), static_cast<std::size_t>(read));
        });
    });

    m_server->set_logger([this](const httplib::Request& request, const httplib::Response& response) {
        const auto* started = response.user_data.get<std::chrono::steady_clock::time_point>("started");
        const auto* arrived = response.user_data.get<std::string>("arrived");
        const auto* bytes = response.user_data.get<std::int64_t>("bytes");
        RequestRecord record;
        record.timestamp = arrived == nullptr ? time::Timestamps::storedTimestamp(time::Timestamps::now()) : *arrived;
        record.method = request.method.empty() ? "INVALID" : request.method;
        record.path = request.target.substr(0, request.target.find('?'));
        record.status = response.status;
        record.durationMilliseconds = started == nullptr ? 0 : std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - *started).count();
        record.responseBytes = bytes == nullptr ? static_cast<std::int64_t>(response.body.size()) : *bytes;
        record.remoteAddress = request.remote_addr;
        m_sink(std::move(record));
    });
    // clang-format on
}

} // namespace workpane::http
