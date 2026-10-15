#include "platform/FileReplacement.h"

#include <tuple>

namespace workpane::platform {

std::error_code FileReplacement::replace(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code error;
    const auto destination = std::filesystem::status(to, error);

    if (error && error != std::errc::no_such_file_or_directory) {
        return error;
    }

    error.clear();

    if (std::filesystem::is_regular_file(destination)) {
        std::filesystem::permissions(from, destination.permissions(), std::filesystem::perm_options::replace, error);

        if (error) {
            return error;
        }
    }

    std::filesystem::rename(from, to, error);

    return error;
}

std::error_code FileReplacement::create(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code error;
    std::filesystem::create_hard_link(from, to, error);

    if (error) {
        return error;
    }

    std::ignore = std::filesystem::remove(from, error);

    return {};
}

} // namespace workpane::platform
