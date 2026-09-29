#include "platform/FileReplacement.h"

namespace workpane::platform {

// A rename replaces its destination at once on macOS and Linux, where a file another program holds open never keeps its name.
std::error_code FileReplacement::replace(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code error;
    std::filesystem::rename(from, to, error);

    return error;
}

} // namespace workpane::platform
