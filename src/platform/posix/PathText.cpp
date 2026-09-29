#include "platform/PathText.h"

namespace workpane::platform {

// A POSIX name is the bytes the system holds, which the host passes on as they are.
std::string PathText::utf8(const std::filesystem::path& path) {
    return path.native();
}

std::string PathText::generic(const std::filesystem::path& path) {
    return path.generic_string();
}

} // namespace workpane::platform
