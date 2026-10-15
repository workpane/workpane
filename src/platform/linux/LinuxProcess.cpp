#include "platform/linux/LinuxProcess.h"

#include "platform/posix/ProgramImage.h"

#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <thread>

namespace workpane::platform {

// A reaped program answers its status when it ends within a short wait, which is how an opener reports that nothing handles an address.
// An opener that runs the program it opened in the foreground, as xdg-open does outside a desktop it knows, is left to a reaper of its own, so no worker waits for the browser to close.
Result<void> LinuxProcess::spawn(const std::vector<std::string>& command, bool reap, std::string_view failureCode) {
    // A system program starts in a session of its own with the streams of the product, so it outlives the product and nothing the product keeps reaches it.
    const auto spawned = ProgramImage(command, ProgramImage::inheritedEnvironment(), {}).spawn({-1, -1, -1}, ProgramImage::Grouping::Session);

    if (!spawned.hasValue()) {
        return Result<void>::failure({std::string(failureCode), "A system program could not be started", command.front()});
    }

    const pid_t process = spawned.value();

    if (!reap) {
        return Result<void>::success();
    }

    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(statusWaitMilliseconds);
    pid_t waited = waitpid(process, &status, WNOHANG);

    while (waited == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(statusPollMilliseconds));
        waited = waitpid(process, &status, WNOHANG);
    }

    if (waited == 0) {
        // clang-format off
        std::thread([process]() { int ended = 0; waitpid(process, &ended, 0); }).detach();
        // clang-format on
        return Result<void>::success();
    }

    if (waited != process || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return Result<void>::failure({std::string(failureCode), "A system program reported a failure", command.front()});
    }

    return Result<void>::success();
}

} // namespace workpane::platform
