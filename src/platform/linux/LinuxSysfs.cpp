#include "platform/linux/LinuxSysfs.h"

#include "text/Integers.h"

#include <cctype>
#include <fstream>
#include <string_view>

namespace workpane::platform {

std::string LinuxSysfs::text(const std::filesystem::path& file) {
    std::ifstream stream(file);
    std::string line;

    if (!stream || !std::getline(stream, line)) {
        return {};
    }

    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())) != 0) {
        line.pop_back();
    }

    return line.find_first_not_of(' ') == std::string::npos ? std::string() : line.substr(line.find_first_not_of(' '));
}

std::int64_t LinuxSysfs::integer(const std::filesystem::path& file) {
    return text::Integers::parse(text(file)).value_or(0);
}

// A cache size is written with a unit suffix such as 32K or 8M, which is turned into bytes.
std::int64_t LinuxSysfs::size(const std::filesystem::path& file) {
    const std::string value = text(file);
    const std::int64_t amount = integer(file);

    if (value.ends_with('K')) {
        return amount * 1024;
    }

    if (value.ends_with('M')) {
        return amount * 1024 * 1024;
    }

    return amount;
}

} // namespace workpane::platform
