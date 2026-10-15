#include "platform/PrivateDirectoryHelper.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>

namespace workpane::platform {

std::error_code PrivateDirectoryHelper::create(const std::filesystem::path& path) {
    if (::mkdir(path.c_str(), 0700) != 0) {
        return {errno, std::generic_category()};
    }

    return {};
}

bool PrivateDirectoryHelper::restricted(const std::filesystem::path& path) {
    struct stat status{};

    return ::lstat(path.c_str(), &status) == 0 && S_ISDIR(status.st_mode) && status.st_uid == ::geteuid() && (status.st_mode & 07777U) == 0700U;
}

} // namespace workpane::platform
