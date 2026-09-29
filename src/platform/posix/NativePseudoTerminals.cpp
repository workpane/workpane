#include "platform/NativePseudoTerminals.h"

#include "platform/TerminalEnvironment.h"
#include "platform/posix/PosixPseudoTerminal.h"
#include "platform/posix/ProgramImage.h"
#include "platform/posix/ShellHistory.h"

#include <pwd.h>
#include <unistd.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>

namespace workpane::platform {

// The shells of closed terminals are hung up already, so the wait lasts at most the grace of the last one.
NativePseudoTerminals::~NativePseudoTerminals() {
    m_threads->wait();
}

void NativePseudoTerminals::listen(std::function<void()> arrived) {
    m_arrived = std::move(arrived);
}

// A terminal names its own shell or starts the one of the reader, and a history file of its own is prepared before the shell starts.
Result<std::unique_ptr<ui::PseudoTerminal>> NativePseudoTerminals::start(const ui::TerminalLaunch& launch) {
    const ShellCommand shell = command(launch.shell.empty() ? program() : launch.shell);
    const std::vector<std::string> inherited = ProgramImage::inheritedEnvironment();
    const auto history = ShellHistory::environment(shell, launch.historyFile, inherited);

    if (!history.hasValue()) {
        return Result<std::unique_ptr<ui::PseudoTerminal>>::failure(history.error());
    }

    return PosixPseudoTerminal::start(shell, launch, TerminalEnvironment::build(inherited, history.value()), m_arrived, m_threads);
}

std::filesystem::path NativePseudoTerminals::shellProgram() const {
    return program();
}

// A POSIX shell reads a single quoted path literally, so only a quote inside it needs to be closed, escaped and opened again.
std::string NativePseudoTerminals::quotePaths(const std::vector<std::filesystem::path>& paths, const std::filesystem::path&) const {
    std::string quoted;

    for (const auto& path : paths) {
        std::string text = path.string();
        std::string escaped = "'";

        for (const char character : text) {
            escaped += character == '\'' ? std::string("'\\''") : std::string(1, character);
        }

        quoted += escaped + "' ";
    }

    return quoted;
}

// The shell of the account is the one the reader chose, found from the environment first and the account database next.
std::filesystem::path NativePseudoTerminals::program() {
    if (const char* variable = std::getenv("SHELL"); variable != nullptr && ::access(variable, X_OK) == 0) {
        return variable;
    }

    passwd account{};
    passwd* found = nullptr;
    std::array<char, 4096> buffer{};

    if (::getpwuid_r(::getuid(), &account, buffer.data(), buffer.size(), &found) == 0 && found != nullptr && found->pw_shell != nullptr && ::access(found->pw_shell, X_OK) == 0) {
        return found->pw_shell;
    }

    return "/bin/sh";
}

// A window opened by the system on macOS inherits no profile, so a shell starts as a login shell there and as an interactive one elsewhere.
ShellCommand NativePseudoTerminals::command(const std::filesystem::path& program) {
#if defined(__APPLE__)
    return {program.string(), {"-l"}, program.filename().string()};
#else
    return {program.string(), {"-i"}, program.filename().string()};
#endif
}

} // namespace workpane::platform
