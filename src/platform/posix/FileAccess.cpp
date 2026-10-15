#include "platform/FileAccess.h"

#include <unistd.h>

namespace workpane::platform {

bool FileAccess::readable(const std::filesystem::path& path) {
    static_assert(readMode == R_OK && writeMode == W_OK, "The modes of access are the ones POSIX names");

    return ::access(path.c_str(), readMode) == 0;
}

bool FileAccess::writable(const std::filesystem::path& path) {
    return ::access(path.c_str(), writeMode) == 0;
}

} // namespace workpane::platform
