#include "support/FileAddress.h"

#include <array>
#include <cctype>
#include <cstdio>

namespace workpane::tests {

std::string FileAddress::of(const std::filesystem::path& path) {
    const std::string generic = path.generic_string();
    std::string address = generic.starts_with('/') ? "file://" : "file:///";

    for (const char character : generic) {
        const auto byte = static_cast<unsigned char>(character);

        if (std::isalnum(byte) != 0 || character == '-' || character == '.' || character == '_' || character == '~' || character == '/' || character == ':') {
            address += character;
            continue;
        }

        std::array<char, 4> escaped{};
        std::snprintf(escaped.data(), escaped.size(), "%%%02X", byte);
        address += escaped.data();
    }

    return address;
}

} // namespace workpane::tests
