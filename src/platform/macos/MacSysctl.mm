#include "platform/macos/MacSysctl.h"

#include <sys/sysctl.h>

#include <cstddef>
#include <cstring>
#include <vector>

namespace workpane::platform {

std::string MacSysctl::text(const char* name) {
    std::size_t size = 0;

    if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0) {
        return {};
    }

    std::vector<char> buffer(size);

    if (sysctlbyname(name, buffer.data(), &size, nullptr, 0) != 0) {
        return {};
    }

    return std::string(buffer.data(), strnlen(buffer.data(), size));
}

// A value is published as a 32 or a 64 bit integer depending on the name, and its size tells which one arrived.
std::int64_t MacSysctl::integer(const char* name) {
    std::int64_t wide = 0;
    std::size_t size = sizeof(wide);

    if (sysctlbyname(name, &wide, &size, nullptr, 0) != 0) {
        return 0;
    }

    if (size == sizeof(std::int32_t)) {
        std::int32_t narrow = 0;
        std::memcpy(&narrow, &wide, sizeof(narrow));
        return narrow;
    }

    return wide;
}

} // namespace workpane::platform
