#include "platform/FileReplacement.h"

#include <windows.h>

#include <thread>

namespace workpane::platform {

// A program that holds a file for a moment, such as a scanner reading what was just written, refuses the move until it lets go, so a refused move is tried again within a bound.
std::error_code FileReplacement::replace(const std::filesystem::path& from, const std::filesystem::path& to) {
    const auto deadline = std::chrono::steady_clock::now() + busyBound;

    while (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) == 0) {
        const DWORD failure = GetLastError();
        const bool held = failure == ERROR_SHARING_VIOLATION || failure == ERROR_LOCK_VIOLATION || failure == ERROR_ACCESS_DENIED;

        if (!held || std::chrono::steady_clock::now() >= deadline) {
            return {static_cast<int>(failure), std::system_category()};
        }

        std::this_thread::sleep_for(retryPause);
    }

    return {};
}

} // namespace workpane::platform
