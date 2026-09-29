#include "files/DirectoryListing.h"

#include "platform/PathText.h"

#include <system_error>
#include <utility>

namespace workpane::files {

Result<std::vector<DirectoryEntry>> DirectoryListing::list(const std::filesystem::path& folder) {
    std::error_code error;
    std::filesystem::directory_iterator entries(folder, error);

    if (error) {
        return Result<std::vector<DirectoryEntry>>::failure({"files_directory_unavailable", "The folder cannot be read", folder.string()});
    }

    std::vector<DirectoryEntry> listed;

    for (; entries != std::filesystem::directory_iterator(); entries.increment(error)) {
        std::error_code entryError;
        const auto status = entries->symlink_status(entryError);
        DirectoryEntry::Kind kind = DirectoryEntry::Kind::Other;

        if (std::filesystem::is_symlink(status)) {
            kind = DirectoryEntry::Kind::Symlink;
        } else if (std::filesystem::is_directory(status)) {
            kind = DirectoryEntry::Kind::Directory;
        } else if (std::filesystem::is_regular_file(status)) {
            kind = DirectoryEntry::Kind::File;
        }

        const std::uintmax_t size = kind == DirectoryEntry::Kind::File ? entries->file_size(entryError) : 0;
        listed.push_back({platform::PathText::utf8(entries->path().filename()), kind, entryError ? 0 : size});
    }

    // A folder that could not be read to its end is refused rather than answered in part.
    if (error) {
        return Result<std::vector<DirectoryEntry>>::failure({"files_directory_unavailable", "The folder could not be read to its end", folder.string() + ": " + error.message()});
    }

    return Result<std::vector<DirectoryEntry>>::success(std::move(listed));
}

} // namespace workpane::files
