#include "scripting/FileHost.h"

#include "execution/MainThreadQueue.h"
#include "execution/WorkerPool.h"
#include "files/DirectoryEntry.h"
#include "files/DirectoryListing.h"
#include "files/TextSearch.h"
#include "files/TreeWalk.h"
#include "json/ObjectReader.h"
#include "platform/FileAccess.h"
#include "platform/FileReplacement.h"
#include "platform/PathText.h"
#include "scripting/HostOwners.h"
#include "scripting/HostReply.h"

#include <array>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace workpane::scripting {

FileHost::FileHost(HostServices& services, ScriptRuntime& runtime, ReplyChannel& replies) : m_services(services), m_runtime(runtime), m_replies(replies) {}

Result<void> FileHost::registerFunctions() {
    // clang-format off
    const std::vector<std::pair<std::string, ScriptRuntime::HostFunction>> functions{
        {"workpane_files_list", [this](const nlohmann::json& argument) { return list(argument); }},
        {"workpane_files_walk", [this](const nlohmann::json& argument) { return walk(argument); }},
        {"workpane_files_search", [this](const nlohmann::json& argument) { return search(argument); }},
        {"workpane_files_canonical", [this](const nlohmann::json& argument) { return canonical(argument); }},
        {"workpane_files_access", [this](const nlohmann::json& argument) { return access(argument); }},
        {"workpane_files_replace", [this](const nlohmann::json& argument) { return replace(argument); }},
    };
    // clang-format on

    for (const auto& [name, function] : functions) {
        if (const auto registered = m_runtime.registerFunction(name, ScriptRuntime::Effect::Background, function); !registered.hasValue()) {
            return registered;
        }
    }

    return Result<void>::success();
}

FileHost::~FileHost() {
    m_stopping->store(true);
}

nlohmann::json FileHost::list(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    std::string path;
    json::ObjectReader reader(argument, "files.list");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestRequest).readText("path", path);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    const std::filesystem::path folder(std::u8string(path.begin(), path.end()));

    if (!folder.is_absolute()) {
        return HostReply::failure({"files_path_invalid", "A path must be absolute", path});
    }

    // clang-format off
    answer(request, [folder]() {
        auto listed = files::DirectoryListing::list(folder);

        if (!listed.hasValue()) {
            return Result<nlohmann::json>::failure(listed.error());
        }

        nlohmann::json entries = nlohmann::json::array();
        const std::array<const char*, 4> kinds{"directory", "file", "symlink", "other"};

        for (const auto& entry : listed.value()) {
            entries.push_back({{"name", entry.name}, {"kind", kinds[static_cast<std::size_t>(entry.kind)]}, {"size", entry.size}});
        }

        return Result<nlohmann::json>::success({{"entries", std::move(entries)}});
    });
    // clang-format on

    return HostReply::success();
}

nlohmann::json FileHost::walk(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    std::string root;
    std::int64_t maximum = 0;
    std::vector<std::string> skipped;
    json::ObjectReader reader(argument, "files.walk");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestRequest).readText("root", root).readInteger("maximum", maximum, 1, largestCount).read("skip", skipped);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    const std::filesystem::path folder(std::u8string(root.begin(), root.end()));

    if (!folder.is_absolute()) {
        return HostReply::failure({"files_path_invalid", "A path must be absolute", root});
    }

    if (skipped.size() > largestSkipped) {
        return HostReply::failure({"files_skip_too_long", "A walk or a search leaves out a bounded list of folders", std::to_string(skipped.size())});
    }

    // clang-format off
    answer(request, [folder, maximum, skipped, stop = m_stopping]() {
        auto walked = files::TreeWalk::walk(folder, static_cast<std::size_t>(maximum), skipped, *stop);

        if (!walked.hasValue()) {
            return Result<nlohmann::json>::failure(walked.error());
        }

        return Result<nlohmann::json>::success({{"paths", walked.value().paths}, {"complete", walked.value().complete}});
    });
    // clang-format on

    return HostReply::success();
}

nlohmann::json FileHost::search(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    std::string root;
    std::string text;
    std::int64_t maximumMatches = 0;
    std::int64_t maximumFileBytes = 0;
    std::vector<std::string> skipped;
    json::ObjectReader reader(argument, "files.search");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestRequest).readText("root", root).readText("text", text).readInteger("maximumMatches", maximumMatches, 1, largestCount).readInteger("maximumFileBytes", maximumFileBytes, 1, largestFileBytes).read("skip", skipped);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    const std::filesystem::path folder(std::u8string(root.begin(), root.end()));

    if (!folder.is_absolute()) {
        return HostReply::failure({"files_path_invalid", "A path must be absolute", root});
    }

    if (skipped.size() > largestSkipped) {
        return HostReply::failure({"files_skip_too_long", "A walk or a search leaves out a bounded list of folders", std::to_string(skipped.size())});
    }

    if (text.size() > largestSearchText) {
        return HostReply::failure({"files_search_text_too_long", "A search looks for a bounded text", std::to_string(text.size())});
    }

    // clang-format off
    answer(request, [folder, text, maximumMatches, maximumFileBytes, skipped, stop = m_stopping]() {
        auto found = files::TextSearch::search(folder, text, static_cast<std::size_t>(maximumMatches), static_cast<std::uintmax_t>(maximumFileBytes), skipped, *stop);

        if (!found.hasValue()) {
            return Result<nlohmann::json>::failure(found.error());
        }

        nlohmann::json matches = nlohmann::json::array();

        for (const auto& match : found.value().matches) {
            matches.push_back({{"path", match.path}, {"line", match.line}, {"text", match.text}});
        }

        return Result<nlohmann::json>::success({{"matches", std::move(matches)}, {"complete", found.value().complete}});
    });
    // clang-format on

    return HostReply::success();
}

// A canonical path resolves every link and every dot, which is how a plugin tells whether two paths name the same file or one lies inside another.
nlohmann::json FileHost::canonical(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    std::string path;
    json::ObjectReader reader(argument, "files.canonical");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestRequest).readText("path", path);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    const std::filesystem::path named(std::u8string(path.begin(), path.end()));

    if (!named.is_absolute()) {
        return HostReply::failure({"files_path_invalid", "A path must be absolute", path});
    }

    // clang-format off
    answer(request, [named, path]() {
        std::error_code error;
        const std::filesystem::path resolved = std::filesystem::canonical(named, error);

        if (error) {
            return Result<nlohmann::json>::failure({"files_path_missing", "The path does not exist", path});
        }

        return Result<nlohmann::json>::success({{"path", platform::PathText::generic(resolved)}});
    });
    // clang-format on

    return HostReply::success();
}

// Whether the product may read or write a path is what the system answers for its account, for a folder as much as for a file.
nlohmann::json FileHost::access(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    std::string path;
    json::ObjectReader reader(argument, "files.access");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestRequest).readText("path", path);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    const std::filesystem::path named(std::u8string(path.begin(), path.end()));

    if (!named.is_absolute()) {
        return HostReply::failure({"files_path_invalid", "A path must be absolute", path});
    }

    // clang-format off
    answer(request, [named, path]() {
        std::error_code error;

        if (!std::filesystem::exists(named, error)) {
            return Result<nlohmann::json>::failure({"files_path_missing", "The path does not exist", path});
        }

        return Result<nlohmann::json>::success({{"readable", platform::FileAccess::readable(named)}, {"writable", platform::FileAccess::writable(named)}});
    });
    // clang-format on

    return HostReply::success();
}

// A file moved over another one takes the permissions of the file it replaces, so saving a script keeps it executable, and a destination that is gone is created.
nlohmann::json FileHost::replace(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    std::string source;
    std::string destination;
    json::ObjectReader reader(argument, "files.replace");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestRequest).readText("source", source).readText("destination", destination);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    const std::filesystem::path from(std::u8string(source.begin(), source.end()));
    const std::filesystem::path to(std::u8string(destination.begin(), destination.end()));

    if (!from.is_absolute() || !to.is_absolute()) {
        return HostReply::failure({"files_path_invalid", "A path must be absolute", from.is_absolute() ? destination : source});
    }

    // clang-format off
    answer(request, [from, to, destination]() {
        std::error_code missing;
        std::error_code error;
        const std::filesystem::file_status replaced = std::filesystem::status(to, missing);

        if (std::filesystem::is_regular_file(replaced)) {
            std::filesystem::permissions(from, replaced.permissions(), std::filesystem::perm_options::replace, error);
        }

        if (!error) {
            error = platform::FileReplacement::replace(from, to);
        }

        if (error) {
            return Result<nlohmann::json>::failure({"files_replace_failed", "The file could not be moved over its destination", destination + ": " + error.message()});
        }

        return Result<nlohmann::json>::success(nullptr);
    });
    // clang-format on

    return HostReply::success();
}

void FileHost::answer(std::int64_t request, std::function<Result<nlohmann::json>()> work) {
    const std::weak_ptr<bool> alive = m_alive;
    // clang-format off
    m_services.workers.post([this, alive, request, work = std::move(work), mainThread = &m_services.mainThread]() {
        auto result = work();
        mainThread->post([this, alive, request, result = std::move(result)]() mutable {
            if (!alive.expired()) {
                m_replies.reply(request, std::move(result));
            }
        });
    });
    // clang-format on
}

} // namespace workpane::scripting
