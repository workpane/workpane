#include "platform/NativeProcesses.h"

#include "platform/posix/PosixProcess.h"

#include <unistd.h>

#include <cstdlib>
#include <string>
#include <system_error>
#include <utility>

namespace workpane::platform {

Result<std::unique_ptr<process::Process>> NativeProcesses::start(const process::ProcessLaunch& launch, process::ProcessEvents events) {
    return PosixProcess::start(launch, std::move(events));
}

// An absolute path names the program itself, and a bare name is looked for on the search path and then in the directories a plugin adds.
std::optional<std::filesystem::path> NativeProcesses::find(std::string_view name, const std::vector<std::filesystem::path>& directories) const {
    const std::filesystem::path named(name);

    if (name.empty() || named.is_absolute()) {
        return name.empty() ? std::nullopt : executable(named);
    }

    if (name.find('/') != std::string_view::npos) {
        return std::nullopt;
    }

    std::vector<std::filesystem::path> searched = searchPath();
    searched.insert(searched.end(), directories.begin(), directories.end());

    for (const auto& directory : searched) {
        if (auto found = executable(directory / named); found.has_value()) {
            return found;
        }
    }

    return std::nullopt;
}

std::vector<std::filesystem::path> NativeProcesses::searchPath() {
    std::vector<std::filesystem::path> directories;
    const char* variable = std::getenv("PATH");
    std::string_view remaining = variable == nullptr ? std::string_view() : std::string_view(variable);

    while (!remaining.empty()) {
        const std::size_t separator = remaining.find(':');
        const std::string_view entry = remaining.substr(0, separator);

        if (!entry.empty()) {
            directories.emplace_back(entry);
        }

        remaining = separator == std::string_view::npos ? std::string_view() : remaining.substr(separator + 1);
    }

    return directories;
}

std::optional<std::filesystem::path> NativeProcesses::executable(const std::filesystem::path& candidate) {
    std::error_code error;

    if (!std::filesystem::is_regular_file(candidate, error) || ::access(candidate.c_str(), X_OK) != 0) {
        return std::nullopt;
    }

    return candidate;
}

} // namespace workpane::platform
