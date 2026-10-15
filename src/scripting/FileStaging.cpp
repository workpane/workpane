#include "scripting/FileStaging.h"

#include "execution/MainThreadQueue.h"
#include "execution/WorkerPool.h"
#include "json/ObjectReader.h"
#include "logging/LogLevels.h"
#include "logging/LogService.h"
#include "platform/PrivateDirectoryHelper.h"
#include "scripting/HostOwners.h"
#include "scripting/HostReply.h"
#include "text/Utf8Helper.h"

#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>

namespace workpane::scripting {

struct FileStaging::Entry final {
    std::string owner;
    std::filesystem::path path;
    std::mutex mutex;
    std::condition_variable finished;
    bool started{false};
    bool ready{false};
    bool created{false};
    bool closing{false};
};

FileStaging::FileStaging(HostServices& services, ReplyChannel& replies) : m_services(services), m_replies(replies) {}

FileStaging::~FileStaging() {
    m_alive.reset();

    for (const auto& [id, entry] : m_entries) {
        static_cast<void>(id);

        if (!entry->closing) {
            release(entry, 0);
        }
    }
}

nlohmann::json FileStaging::open(const nlohmann::json& argument) {
    std::string plugin;
    std::string path;
    json::ObjectReader reader(argument, "files.staging");
    reader.readText("plugin", plugin).readText("path", path);

    if (const auto parsed = reader.finish(); !parsed.hasValue()) {
        return HostReply::failure(parsed.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    if (path.find('\0') != std::string::npos || !text::Utf8Helper::valid(path)) {
        return HostReply::failure({"files_path_invalid", "A file path must contain valid text without a NUL byte", ""});
    }

    const std::filesystem::path folder(std::u8string(path.begin(), path.end()));

    if (!folder.is_absolute()) {
        return HostReply::failure({"files_path_invalid", "A file path must be absolute", ""});
    }

    if (m_entries.size() >= 32 || m_next == 9007199254740991) {
        return HostReply::failure({"files_staging_full", "The product has reached its private staging limit", ""});
    }

    const auto id = ++m_next;
    auto entry = std::make_shared<Entry>();
    entry->owner = plugin;
    entry->path = folder;
    m_entries.emplace(id, std::move(entry));

    return HostReply::success({{"id", id}, {"path", path}});
}

nlohmann::json FileStaging::ready(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t id = 0;
    std::int64_t request = 0;
    json::ObjectReader reader(argument, "files.staging.ready");
    reader.readText("plugin", plugin).readInteger("id", id, 1, 9007199254740991).readInteger("request", request, 1, 9007199254740991);

    if (const auto parsed = reader.finish(); !parsed.hasValue()) {
        return HostReply::failure(parsed.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    const auto found = m_entries.find(id);

    if (found == m_entries.end() || found->second->owner != plugin || found->second->started || found->second->closing) {
        return HostReply::failure({"files_staging_invalid", "The private staging operation is unavailable", ""});
    }

    const auto entry = found->second;
    entry->started = true;
    const std::weak_ptr<bool> alive = m_alive;
    // clang-format off
    m_services.workers.post([this, alive, entry, request, mainThread = &m_services.mainThread]() {
        const auto error = platform::PrivateDirectoryHelper::create(entry->path);

        {
            const std::scoped_lock lock(entry->mutex);
            entry->created = !error;
            entry->ready = true;
        }

        entry->finished.notify_all();
        mainThread->post([this, alive, request, error]() {
            if (!alive.expired()) {
                reply(request, error ? Result<nlohmann::json>::failure({"files_private_directory_failed", "The private directory could not be created", ""}) : Result<nlohmann::json>::success(nullptr));
            }
        });
    });
    // clang-format on

    return HostReply::success();
}

nlohmann::json FileStaging::close(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t id = 0;
    std::int64_t request = 0;
    json::ObjectReader reader(argument, "files.staging.close");
    reader.readText("plugin", plugin).readInteger("id", id, 1, 9007199254740991).readInteger("request", request, 1, 9007199254740991);

    if (const auto parsed = reader.finish(); !parsed.hasValue()) {
        return HostReply::failure(parsed.error());
    }

    const auto found = m_entries.find(id);

    if (found == m_entries.end() || found->second->owner != plugin || found->second->closing) {
        return HostReply::failure({"files_staging_invalid", "The private staging operation is unavailable", ""});
    }

    found->second->closing = true;
    release(found->second, request);

    return HostReply::success();
}

void FileStaging::release(const std::shared_ptr<Entry>& entry, std::int64_t request) {
    const std::weak_ptr<bool> alive = m_alive;
    // clang-format off
    m_services.workers.post([this, alive, entry, request, mainThread = &m_services.mainThread, logs = &m_services.logs]() {
        {
            std::unique_lock lock(entry->mutex);
            entry->finished.wait(lock, [&entry]() { return !entry->started || entry->ready; });
        }

        std::error_code error;

        if (entry->created) {
            std::filesystem::remove_all(entry->path, error);
        }

        if (error) {
            logs->write(logging::LogLevel::Error, entry->owner, "files", "The private staging directory could not be removed", {{"code", "files_staging_cleanup_failed"}});
        }

        if (request != 0) {
            mainThread->post([this, alive, entry, request, error]() {
                if (!alive.expired()) {
                    if (error) {
                        entry->closing = false;
                    } else {
                        std::erase_if(m_entries, [&entry](const auto& item) { return item.second == entry; });
                    }

                    reply(request, error ? Result<nlohmann::json>::failure({"files_staging_cleanup_failed", "The private staging directory could not be removed", ""}) : Result<nlohmann::json>::success(nullptr));
                }
            });
        }
    });
    // clang-format on
}

void FileStaging::reply(std::int64_t request, const Result<nlohmann::json>& result) {
    m_replies.reply(request, result);
}

} // namespace workpane::scripting
