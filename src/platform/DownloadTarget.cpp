#include "platform/DownloadTarget.h"

#include <algorithm>
#include <string>
#include <system_error>

namespace workpane::platform {

// Only the last part of the suggestion counts, whichever separator it uses, so a name never climbs out of the folder.
std::filesystem::path DownloadTarget::choose(const std::filesystem::path& folder, std::string_view suggested) {
    std::string plain(suggested);
    std::ranges::replace(plain, '\\', '/');
    std::filesystem::path name = std::filesystem::path(std::u8string(plain.begin(), plain.end())).filename();

    if (name.empty() || name == "." || name == "..") {
        name = std::filesystem::path(unnamed);
    }

    std::error_code error;
    std::filesystem::create_directories(folder, error);
    std::filesystem::path candidate = folder / name;

    for (int copy = 1; std::filesystem::exists(candidate, error); ++copy) {
        const std::string counter = " (" + std::to_string(copy) + ")";
        candidate = folder / (name.stem().u8string() + std::u8string(counter.begin(), counter.end()) + name.extension().u8string());
    }

    return candidate;
}

} // namespace workpane::platform
