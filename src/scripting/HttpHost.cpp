#include "scripting/HttpHost.h"

#include "execution/MainThreadQueue.h"
#include "execution/WorkerPool.h"
#include "json/ObjectReader.h"
#include "platform/PathTextHelper.h"
#include "scripting/HostOwners.h"
#include "scripting/HostReply.h"

#include <filesystem>
#include <mutex>
#include <utility>

namespace workpane::scripting {

// The requests the server threads gather, shared with them so a server still ending never reaches a host that is gone.
struct HttpHost::Mailbox final {
    std::mutex mutex;
    std::map<std::int64_t, std::vector<http::RequestRecord>> pending;
    bool posted{false};
    bool open{true};
};

HttpHost::HttpHost(HostServices& services, ScriptRuntime& runtime, ReplyChannel& replies) : m_services(services), m_runtime(runtime), m_replies(replies), m_mailbox(std::make_shared<Mailbox>()) {}

// The mailbox closes first and every server, including the ones still ending on a worker, stops before the host goes, so no request reaches a host that no longer exists.
HttpHost::~HttpHost() {
    {
        const std::lock_guard lock(m_mailbox->mutex);
        m_mailbox->open = false;
    }

    for (auto& [id, hosted] : m_servers) {
        hosted.server->stop();
    }

    for (const auto& server : m_retired) {
        server->stop();
    }
}

Result<void> HttpHost::registerFunctions() {
    // clang-format off
    const std::vector<std::pair<std::string, ScriptRuntime::HostFunction>> functions{
        {"workpane_http_serve", [this](const nlohmann::json& argument) { return serve(argument); }},
        {"workpane_http_stop", [this](const nlohmann::json& argument) { return stop(argument); }},
        {"workpane_http_forget", [this](const nlohmann::json& argument) { return forget(argument); }},
    };
    // clang-format on

    for (const auto& [name, function] : functions) {
        if (const auto registered = m_runtime.registerFunction(name, ScriptRuntime::Effect::Background, function); !registered.hasValue()) {
            return registered;
        }
    }

    return Result<void>::success();
}

// A server is identified by the request that started it, which the SDK knows before any of its requests can arrive, and binding runs on a worker.
nlohmann::json HttpHost::serve(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    std::string host;
    std::int64_t port = 0;
    std::string root;
    json::ObjectReader reader(argument, "http.serve");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestRequest).readText("host", host).readInteger("port", port, 1, 65535).readText("root", root);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    const std::weak_ptr<bool> alive = m_alive;
    // Requests are gathered on the server threads in the shared mailbox and handed to Lua in one delivery per turn of the frame loop, which runs only while the host lives.
    // clang-format off
    const http::StaticFileServer::RequestSink sink = [this, alive, request, mailbox = m_mailbox, mainThread = &m_services.mainThread](http::RequestRecord record) {
        const std::lock_guard lock(mailbox->mutex);

        if (!mailbox->open) {
            return;
        }

        mailbox->pending[request].push_back(std::move(record));

        if (mailbox->posted) {
            return;
        }

        mailbox->posted = true;
        mainThread->post([this, alive]() {
            if (!alive.expired()) {
                deliver();
            }
        });
    };

    m_services.workers.post([this, alive, request, plugin, host, port, root = std::filesystem::path(std::u8string(root.begin(), root.end())), sink, mainThread = &m_services.mainThread]() {
        auto started = http::StaticFileServer::start(host, static_cast<int>(port), root, sink);
        std::shared_ptr<http::StaticFileServer> server = started.hasValue() ? std::move(started.value()) : nullptr;
        mainThread->post([this, alive, request, plugin, server, failure = started.hasValue() ? Error{} : started.error()]() {
            if (alive.expired()) {
                return;
            }

            if (server == nullptr) {
                m_replies.reply(request, Result<nlohmann::json>::failure(failure));
                return;
            }

            // A server whose bind finished after its plugin was withdrawn stops at once instead of serving for nobody.
            if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
                retire(server);
                m_replies.reply(request, Result<nlohmann::json>::failure(known.error()));
                return;
            }

            m_servers[request] = Hosted{plugin, server};
            m_replies.reply(request, Result<nlohmann::json>::success({{"server", request}, {"root", platform::PathTextHelper::generic(server->root())}}));
        });
    });
    // clang-format on

    return HostReply::success();
}

// A stopped server releases its port at once, and the wait for its connections to end happens on a worker.
nlohmann::json HttpHost::stop(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t server = 0;
    json::ObjectReader reader(argument, "http.stop");
    reader.readText("plugin", plugin).readInteger("server", server, 1, largestRequest);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    const auto found = m_servers.find(server);

    if (found == m_servers.end() || found->second.owner != plugin) {
        return HostReply::failure({"http_server_unknown", "The server is not running or belongs to another plugin", std::to_string(server)});
    }

    retire(std::move(found->second.server));
    m_servers.erase(found);

    return HostReply::success();
}

nlohmann::json HttpHost::forget(const nlohmann::json& argument) {
    std::string plugin;
    json::ObjectReader reader(argument, "http.forget");
    reader.readText("plugin", plugin);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    for (auto entry = m_servers.begin(); entry != m_servers.end();) {
        if (entry->second.owner != plugin) {
            ++entry;
            continue;
        }

        retire(std::move(entry->second.server));
        entry = m_servers.erase(entry);
    }

    return HostReply::success();
}

void HttpHost::retire(std::shared_ptr<http::StaticFileServer> server) {
    server->close();
    m_retired.push_back(server);
    const std::weak_ptr<bool> alive = m_alive;
    // clang-format off
    m_services.workers.post([this, alive, server, mainThread = &m_services.mainThread]() {
        server->stop();
        mainThread->post([this, alive, server]() {
            if (!alive.expired()) {
                std::erase(m_retired, server);
            }
        });
    });
    // clang-format on
}

void HttpHost::deliver() {
    std::map<std::int64_t, std::vector<http::RequestRecord>> pending;

    {
        const std::lock_guard lock(m_mailbox->mutex);
        pending.swap(m_mailbox->pending);
        m_mailbox->posted = false;
    }

    for (const auto& [server, records] : pending) {
        const auto found = m_servers.find(server);

        if (found == m_servers.end()) {
            continue;
        }

        nlohmann::json requests = nlohmann::json::array();

        for (const auto& entry : records) {
            requests.push_back({{"timestamp", entry.timestamp}, {"method", entry.method}, {"path", entry.path}, {"status", entry.status}, {"durationMs", entry.durationMilliseconds}, {"responseBytes", entry.responseBytes}, {"remoteAddress", entry.remoteAddress}});
        }

        m_runtime.emit("workpane.http.requests", {{"plugin", found->second.owner}, {"server", server}, {"requests", requests}});
    }
}

} // namespace workpane::scripting
