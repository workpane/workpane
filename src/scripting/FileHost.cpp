#include "scripting/FileHost.h"

#include "execution/MainThreadQueue.h"
#include "execution/WorkerPool.h"
#include "files/DirectoryEntry.h"
#include "files/DirectoryListing.h"
#include "files/FileDigestHelper.h"
#include "files/TextSearch.h"
#include "files/TreeWalk.h"
#include "json/ObjectReader.h"
#include "platform/FileAccess.h"
#include "platform/FileReplacement.h"
#include "platform/PathTextHelper.h"
#include "scripting/HostOwners.h"
#include "scripting/HostReply.h"
#include "text/Base64Helper.h"
#include "text/Utf8Helper.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

namespace workpane::scripting {

FileHost::FileHost(HostServices& services, ScriptRuntime& runtime, ReplyChannel& replies) : m_services(services), m_runtime(runtime), m_replies(replies), m_staging(services, replies) {}

Result<void> FileHost::registerFunctions() {
    // clang-format off
    const std::vector<std::pair<std::string, ScriptRuntime::HostFunction>> functions{
        {"workpane_files_list", [this](const nlohmann::json& argument) { return list(argument); }},
        {"workpane_files_walk", [this](const nlohmann::json& argument) { return walk(argument); }},
        {"workpane_files_search", [this](const nlohmann::json& argument) { return search(argument); }},
        {"workpane_files_canonical", [this](const nlohmann::json& argument) { return canonical(argument); }},
        {"workpane_files_stat", [this](const nlohmann::json& argument) { return stat(argument); }},
        {"workpane_files_access", [this](const nlohmann::json& argument) { return access(argument); }},
        {"workpane_files_replace", [this](const nlohmann::json& argument) { return replace(argument); }},
        {"workpane_files_digest", [this](const nlohmann::json& argument) { return digest(argument); }},
        {"workpane_files_hash", [this](const nlohmann::json& argument) { return hash(argument); }},
        {"workpane_files_read", [this](const nlohmann::json& argument) { return read(argument); }},
        {"workpane_files_staging_open", [this](const nlohmann::json& argument) { return m_staging.open(argument); }},
        {"workpane_files_staging_ready", [this](const nlohmann::json& argument) { return m_staging.ready(argument); }},
        {"workpane_files_staging_close", [this](const nlohmann::json& argument) { return m_staging.close(argument); }},
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

Result<std::filesystem::path> FileHost::filePath(std::string_view text) {
    if (text.find('\0') != std::string_view::npos || !text::Utf8Helper::valid(text)) {
        return Result<std::filesystem::path>::failure({"files_path_invalid", "A file path must contain valid text without a NUL byte", ""});
    }

    const std::filesystem::path path(std::u8string(text.begin(), text.end()));

    if (!path.is_absolute()) {
        return Result<std::filesystem::path>::failure({"files_path_invalid", "A file path must be absolute", std::string(text)});
    }

    return Result<std::filesystem::path>::success(path);
}

nlohmann::json FileHost::list(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    std::string path;
    std::int64_t maximum = 0;
    json::ObjectReader reader(argument, "files.list");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestRequest).readText("path", path).readInteger("maximum", maximum, 1, largestCount);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    const auto parsed = filePath(path);

    if (!parsed.hasValue()) {
        return HostReply::failure(parsed.error());
    }

    const auto& folder = parsed.value();

    // clang-format off
    answer(request, [folder, maximum, stop = m_stopping]() {
        auto listed = files::DirectoryListing::list(folder, static_cast<std::size_t>(maximum), *stop);

        if (!listed.hasValue()) {
            return Result<nlohmann::json>::failure(listed.error());
        }

        nlohmann::json entries = nlohmann::json::array();
        const std::array<const char*, 4> kinds{"directory", "file", "symlink", "other"};

        for (const auto& entry : listed.value().entries) {
            entries.push_back({{"name", entry.name}, {"kind", kinds[static_cast<std::size_t>(entry.kind)]}, {"size", entry.size}});
        }

        return Result<nlohmann::json>::success({{"entries", std::move(entries)}, {"complete", listed.value().complete}});
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

    const auto parsed = filePath(root);

    if (!parsed.hasValue()) {
        return HostReply::failure(parsed.error());
    }

    const auto& folder = parsed.value();

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

    const auto parsed = filePath(root);

    if (!parsed.hasValue()) {
        return HostReply::failure(parsed.error());
    }

    const auto& folder = parsed.value();

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

    const auto parsed = filePath(path);

    if (!parsed.hasValue()) {
        return HostReply::failure(parsed.error());
    }

    const auto& named = parsed.value();

    // clang-format off
    answer(request, [named, path]() {
        std::error_code error;
        const std::filesystem::path resolved = std::filesystem::canonical(named, error);

        if (error) {
            return Result<nlohmann::json>::failure({"files_path_missing", "The path does not exist", path});
        }

        return Result<nlohmann::json>::success({{"path", platform::PathTextHelper::generic(resolved)}});
    });
    // clang-format on

    return HostReply::success();
}

nlohmann::json FileHost::stat(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    std::string path;
    json::ObjectReader reader(argument, "files.stat");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestRequest).readText("path", path);

    if (const auto checked = reader.finish(); !checked.hasValue()) {
        return HostReply::failure(checked.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    const auto parsed = filePath(path);

    if (!parsed.hasValue()) {
        return HostReply::failure(parsed.error());
    }

    // clang-format off
    answer(request, [named = parsed.value()]() {
        std::error_code error;
        const auto link = std::filesystem::symlink_status(named, error);

        if (error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory || (!error && !std::filesystem::exists(link))) {
            return Result<nlohmann::json>::success(nullptr);
        }

        if (error) {
            return Result<nlohmann::json>::failure({"files_metadata_unavailable", "The path metadata could not be read", ""});
        }

        const auto status = std::filesystem::status(named, error);
        const bool symlink = std::filesystem::is_symlink(link);

        if (symlink && (error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory)) {
            return Result<nlohmann::json>::success({{"isFile", false}, {"isDir", false}, {"isSymlink", true}, {"size", 0}, {"mtime", 0}});
        }

        if (error) {
            return Result<nlohmann::json>::failure({"files_metadata_unavailable", "The path metadata could not be read", ""});
        }

        const bool file = std::filesystem::is_regular_file(status);
        const auto size = file ? std::filesystem::file_size(named, error) : 0;

        if (error || size > static_cast<std::uintmax_t>(std::numeric_limits<std::int64_t>::max())) {
            return Result<nlohmann::json>::failure({"files_metadata_unavailable", "The file size could not be read", ""});
        }

        const auto modified = std::filesystem::last_write_time(named, error);

        if (error) {
            return Result<nlohmann::json>::failure({"files_metadata_unavailable", "The modification time could not be read", ""});
        }

        const auto stamp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(modified - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());

        return Result<nlohmann::json>::success({{"isFile", file}, {"isDir", std::filesystem::is_directory(status)}, {"isSymlink", symlink}, {"size", size}, {"mtime", std::chrono::system_clock::to_time_t(stamp)}});
    });
    // clang-format on

    return HostReply::success();
}

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

    const auto parsed = filePath(path);

    if (!parsed.hasValue()) {
        return HostReply::failure(parsed.error());
    }

    const auto& named = parsed.value();

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

    const auto parsedSource = filePath(source);
    const auto parsedDestination = filePath(destination);

    if (!parsedSource.hasValue()) {
        return HostReply::failure(parsedSource.error());
    }

    if (!parsedDestination.hasValue()) {
        return HostReply::failure(parsedDestination.error());
    }

    const auto& from = parsedSource.value();
    const auto& to = parsedDestination.value();

    // clang-format off
    answer(request, [from, to, destination]() {
        if (const auto error = platform::FileReplacement::replace(from, to); error) {
            return Result<nlohmann::json>::failure({"files_replace_failed", "The file could not be moved over its destination", destination + ": " + error.message()});
        }

        return Result<nlohmann::json>::success(nullptr);
    });
    // clang-format on

    return HostReply::success();
}

nlohmann::json FileHost::digest(const nlohmann::json& argument) {
    std::string plugin;
    std::int64_t request = 0;
    std::int64_t maximum = 0;
    std::string path;
    json::ObjectReader reader(argument, "files.digest");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestRequest).readText("path", path).readInteger("maximum", maximum, 0, largestFileBytes);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return HostReply::failure(finished.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    const auto parsed = filePath(path);

    if (!parsed.hasValue()) {
        return HostReply::failure(parsed.error());
    }

    const auto& file = parsed.value();

    // clang-format off
    answer(request, [file, maximum, stopping = m_stopping]() {
        const auto hashed = files::FileDigestHelper::sha256(file, static_cast<std::size_t>(maximum), *stopping);

        if (!hashed.hasValue()) {
            return Result<nlohmann::json>::failure(hashed.error());
        }

        return Result<nlohmann::json>::success({{"digest", hashed.value()}});
    });
    // clang-format on

    return HostReply::success();
}

nlohmann::json FileHost::hash(const nlohmann::json& argument) {
    std::string plugin;
    std::string content;
    std::int64_t request = 0;
    json::ObjectReader reader(argument, "files.hash");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestRequest).read("content", content);

    if (const auto valid = reader.finish(); !valid.hasValue()) {
        return HostReply::failure(valid.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    if (content.size() > ((largestHashBytes + 2) / 3) * 4) {
        return HostReply::failure({"files_hash_invalid", "The binary hash input exceeds its byte bound", ""});
    }

    // clang-format off
    answer(request, [content = std::move(content), stopping = m_stopping]() {
        if (stopping->load()) {
            return Result<nlohmann::json>::failure({"files_digest_cancelled", "The binary digest was cancelled", ""});
        }

        const auto decoded = text::Base64Helper::decode(content);

        if (!decoded.has_value() || decoded->size() > largestHashBytes) {
            return Result<nlohmann::json>::failure({"files_hash_invalid", "The binary hash input is invalid", ""});
        }

        const std::string bytes(decoded->begin(), decoded->end());

        if (text::Base64Helper::encode(bytes) != content) {
            return Result<nlohmann::json>::failure({"files_hash_invalid", "The binary hash input is invalid", ""});
        }

        const auto hashed = files::FileDigestHelper::sha256(bytes);

        if (!hashed.hasValue()) {
            return Result<nlohmann::json>::failure(hashed.error());
        }

        return Result<nlohmann::json>::success({{"digest", hashed.value()}});
    });
    // clang-format on

    return HostReply::success();
}

nlohmann::json FileHost::read(const nlohmann::json& argument) {
    std::string plugin;
    std::string path;
    std::int64_t request = 0;
    std::int64_t maximum = 0;
    json::ObjectReader reader(argument, "files.read");
    reader.readText("plugin", plugin).readInteger("request", request, 1, largestRequest).readText("path", path).readInteger("maximum", maximum, 0, largestFileBytes);

    if (const auto parsed = reader.finish(); !parsed.hasValue()) {
        return HostReply::failure(parsed.error());
    }

    if (const auto known = HostOwners::check(m_services.plugins, plugin); !known.hasValue()) {
        return HostReply::failure(known.error());
    }

    const auto parsed = filePath(path);

    if (!parsed.hasValue()) {
        return HostReply::failure(parsed.error());
    }

    // clang-format off
    answer(request, [file = parsed.value(), maximum = static_cast<std::size_t>(maximum), stopping = m_stopping]() {
        std::error_code error;

        if (!std::filesystem::is_regular_file(file, error) || error) {
            return Result<nlohmann::json>::failure({"files_read_unavailable", "The file cannot be read", ""});
        }

        std::ifstream input(file, std::ios::binary);

        if (!input) {
            return Result<nlohmann::json>::failure({"files_read_unavailable", "The file cannot be opened", ""});
        }

        std::array<char, 65536> buffer{};
        std::string bytes;

        while (true) {
            if (stopping->load()) {
                return Result<nlohmann::json>::failure({"files_read_cancelled", "The file read was cancelled", ""});
            }

            const auto remaining = maximum - bytes.size();
            input.read(buffer.data(), static_cast<std::streamsize>(std::min(buffer.size(), remaining + 1)));
            const auto count = static_cast<std::size_t>(input.gcount());

            if (input.bad() || (input.fail() && !input.eof())) {
                return Result<nlohmann::json>::failure({"files_read_failed", "The file could not be read whole within its bound", ""});
            }

            bytes.append(buffer.data(), std::min(count, remaining));

            if (count > remaining || input.eof()) {
                return Result<nlohmann::json>::success({{"content", text::Base64Helper::encode(bytes)}, {"complete", input.eof() && count <= remaining}});
            }
        }
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
