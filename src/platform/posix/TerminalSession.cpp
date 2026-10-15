#include "platform/posix/TerminalSession.h"

#include <sys/ioctl.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <string>
#include <system_error>
#include <tuple>

namespace workpane::platform {

Result<std::filesystem::path> TerminalSession::program() {
    std::filesystem::path executable;
#if defined(__APPLE__)
    std::uint32_t size = 0;
    std::ignore = _NSGetExecutablePath(nullptr, &size);
    std::string path(size, '\0');

    if (_NSGetExecutablePath(path.data(), &size) != 0) {
        return Result<std::filesystem::path>::failure({"terminal_spawn_failed", "The terminal session launcher could not be located", ""});
    }

    executable = path.c_str();
#else
    std::array<char, 4096> path{};
    const ssize_t size = ::readlink("/proc/self/exe", path.data(), path.size());

    if (size <= 0 || static_cast<std::size_t>(size) == path.size()) {
        return Result<std::filesystem::path>::failure({"terminal_spawn_failed", "The terminal session launcher could not be located", std::to_string(errno)});
    }

    executable = std::string(path.data(), static_cast<std::size_t>(size));
#endif
    std::error_code error;
    const auto launcher = std::filesystem::canonical(executable, error).parent_path() / "WorkpaneTerminal";

    if (error || ::access(launcher.c_str(), X_OK) != 0) {
        return Result<std::filesystem::path>::failure({"terminal_spawn_failed", "The terminal session launcher cannot be run", launcher.string()});
    }

    return Result<std::filesystem::path>::success(launcher);
}

int TerminalSession::run(int argc, char** argv) {
    if (argc < 2 || ::getsid(0) != ::getpid() || ::isatty(STDIN_FILENO) == 0) {
        std::fputs("The terminal session launcher requires a session and a terminal\n", stderr);
        return 126;
    }

    // Darwin applies the session attribute after spawn file actions, so acquiring the controlling terminal must run in the new session.
    if (::ioctl(STDIN_FILENO, TIOCSCTTY, 0) != 0 || ::tcsetpgrp(STDIN_FILENO, ::getpgrp()) != 0) {
        std::fprintf(stderr, "The terminal session could not acquire its controlling terminal (error %d)\n", errno);
        return 126;
    }

    ::execv(argv[1], argv + 1);
    std::fprintf(stderr, "The terminal session could not execute its shell (error %d)\n", errno);

    return 126;
}

} // namespace workpane::platform
