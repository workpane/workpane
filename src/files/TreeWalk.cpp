#include "files/TreeWalk.h"

#include "platform/PathTextHelper.h"

#include <algorithm>
#include <system_error>
#include <utility>

namespace workpane::files {

// A file is named relative to the root with forward slashes, the visitor stops the walk by answering false, and a folder that cannot be opened is left out while the walk goes on with the rest.
Result<bool> TreeWalk::visit(const std::filesystem::path& root, const std::vector<std::string>& skipped, const Visitor& visitor, const std::atomic<bool>& stop, std::size_t maximumInspected) {
    std::error_code error;
    std::vector<std::filesystem::directory_iterator> folders;
    folders.emplace_back(root, error);

    if (error) {
        return Result<bool>::failure({"files_directory_unavailable", "The folder cannot be read", platform::PathTextHelper::generic(root)});
    }

    bool complete = true;
    std::size_t inspected = 0;

    while (!folders.empty() && !stop.load()) {
        std::filesystem::directory_iterator& entries = folders.back();

        if (entries == std::filesystem::directory_iterator()) {
            folders.pop_back();
            continue;
        }

        if (inspected == maximumInspected) {
            complete = false;
            break;
        }

        ++inspected;
        const std::filesystem::directory_entry entry = *entries;
        entries.increment(error);

        if (error) {
            complete = false;
        }

        const auto status = entry.symlink_status(error);

        if (error) {
            complete = false;
            continue;
        }

        const std::string name = platform::PathTextHelper::utf8(entry.path().filename());

        if (std::filesystem::is_directory(status)) {
            if (std::ranges::find(skipped, name) != skipped.end()) {
                continue;
            }

            std::filesystem::directory_iterator inner(entry.path(), error);

            if (error) {
                complete = false;
                continue;
            }

            folders.push_back(std::move(inner));

            continue;
        }

        if (!std::filesystem::is_regular_file(status)) {
            continue;
        }

        if (!visitor(entry.path(), platform::PathTextHelper::generic(entry.path().lexically_relative(root)))) {
            complete = false;
            break;
        }
    }

    return Result<bool>::success(complete && !stop.load());
}

Result<TreeWalk::Walked> TreeWalk::walk(const std::filesystem::path& root, std::size_t maximum, const std::vector<std::string>& skipped, const std::atomic<bool>& stop) {
    Walked walked{{}, true};
    // clang-format off
    const Visitor collect = [&walked, maximum](const std::filesystem::path&, const std::string& relative) {
        if (walked.paths.size() == maximum) {
            walked.complete = false;
            return false;
        }

        walked.paths.push_back(relative);

        return true;
    };
    // clang-format on

    const auto visited = visit(root, skipped, collect, stop, maximumEntries);

    if (!visited.hasValue()) {
        return Result<Walked>::failure(visited.error());
    }

    walked.complete = walked.complete && visited.value();

    return Result<Walked>::success(std::move(walked));
}

} // namespace workpane::files
