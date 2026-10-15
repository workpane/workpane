#include "files/DirectoryListing.h"

#include "platform/PathTextHelper.h"

#include <system_error>
#include <utility>

namespace workpane::files {

Result<DirectoryListing::Listed> DirectoryListing::list(const std::filesystem::path& folder, std::size_t maximum, const std::atomic<bool>& stop) {
    Listed listed{{}, true};

    if (stop.load()) {
        listed.complete = false;
        return Result<Listed>::success(std::move(listed));
    }

    std::error_code error;
    std::filesystem::directory_iterator entries(folder, error);

    if (error) {
        return Result<Listed>::failure({"files_directory_unavailable", "The folder cannot be read", platform::PathTextHelper::utf8(folder)});
    }

    std::size_t visited = 0;

    for (; entries != std::filesystem::directory_iterator(); entries.increment(error)) {
        if (stop.load() || visited == maximum || error) {
            listed.complete = false;
            break;
        }

        ++visited;
        std::error_code entryError;
        const auto status = entries->symlink_status(entryError);

        if (entryError) {
            listed.complete = false;
            continue;
        }

        DirectoryEntry::Kind kind = DirectoryEntry::Kind::Other;

        if (std::filesystem::is_symlink(status)) {
            kind = DirectoryEntry::Kind::Symlink;
        } else if (std::filesystem::is_directory(status)) {
            kind = DirectoryEntry::Kind::Directory;
        } else if (std::filesystem::is_regular_file(status)) {
            kind = DirectoryEntry::Kind::File;
        }

        const std::uintmax_t size = kind == DirectoryEntry::Kind::File ? entries->file_size(entryError) : 0;

        if (entryError) {
            listed.complete = false;
            continue;
        }

        listed.entries.push_back({platform::PathTextHelper::utf8(entries->path().filename()), kind, size});
    }

    listed.complete = listed.complete && !error && !stop.load();

    return Result<Listed>::success(std::move(listed));
}

} // namespace workpane::files
