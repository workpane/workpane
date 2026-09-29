#include "platform/ProcessDirectory.h"

#include <filesystem>
#include <string>
#include <system_error>

namespace workpane::platform {

std::string ProcessDirectory::of(long processId) {
    std::error_code error;
    const auto directory = std::filesystem::read_symlink("/proc/" + std::to_string(processId) + "/cwd", error);
    return error ? std::string() : directory.string();
}

} // namespace workpane::platform
