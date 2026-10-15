#include "platform/FileAccess.h"

#include <io.h>

namespace workpane::platform {

bool FileAccess::readable(const std::filesystem::path& path) {
    return _waccess(path.c_str(), readMode) == 0;
}

// A file marked read-only refuses writing, while Windows lets every folder be written as far as this check tells.
bool FileAccess::writable(const std::filesystem::path& path) {
    return _waccess(path.c_str(), writeMode) == 0;
}

} // namespace workpane::platform
