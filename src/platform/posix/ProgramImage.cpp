#include "platform/posix/ProgramImage.h"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <unistd.h>

#include <utility>

extern char** environ;

namespace workpane::platform {

ProgramImage::ProgramImage(const std::vector<std::string>& arguments, const std::vector<std::string>& environment, std::string directory) : m_arguments(terminated(arguments)), m_environment(terminated(environment)), m_argv(pointers(m_arguments)), m_envp(pointers(m_environment)), m_directory(std::move(directory)) {}

std::vector<std::string> ProgramImage::inheritedEnvironment() {
    std::vector<std::string> inherited;

    for (char** entry = environ; *entry != nullptr; ++entry) {
        inherited.emplace_back(*entry);
    }

    return inherited;
}

// A stream given as minus one is the stream of the product, and the program starts in a process group or a session of its own.
Result<pid_t> ProgramImage::spawn(const std::array<int, 3>& streams, Grouping grouping) const {
    return start(streams, {}, grouping);
}

Result<pid_t> ProgramImage::spawnOnTerminal(const std::string& terminal) const {
    return start({-1, -1, -1}, terminal, Grouping::Session);
}

// The program starts with every signal unblocked and a broken pipe ending it again, because the product ignores that signal for itself, and with only the descriptors of its streams.
Result<pid_t> ProgramImage::start(const std::array<int, 3>& streams, const std::string& terminal, Grouping grouping) const {
    posix_spawnattr_t attributes;
    posix_spawn_file_actions_t actions;
    posix_spawnattr_init(&attributes);
    posix_spawn_file_actions_init(&actions);
    short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF | (grouping == Grouping::Session ? POSIX_SPAWN_SETSID : POSIX_SPAWN_SETPGROUP);
#if defined(__APPLE__)
    flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif
    posix_spawnattr_setflags(&attributes, flags);
    posix_spawnattr_setpgroup(&attributes, 0);
    sigset_t none;
    sigset_t defaults;
    sigemptyset(&none);
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGPIPE);
    posix_spawnattr_setsigmask(&attributes, &none);
    posix_spawnattr_setsigdefault(&attributes, &defaults);

    if (!m_directory.empty()) {
        posix_spawn_file_actions_addchdir_np(&actions, m_directory.c_str());
    }

    // A terminal opened after the session begins becomes its controlling terminal, and it is the three streams of the shell.
    if (!terminal.empty()) {
        posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, terminal.c_str(), O_RDWR, 0);
        posix_spawn_file_actions_adddup2(&actions, STDIN_FILENO, STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, STDIN_FILENO, STDERR_FILENO);
    }

    for (int stream = 0; terminal.empty() && stream < 3; ++stream) {
        if (streams[static_cast<std::size_t>(stream)] >= 0) {
            posix_spawn_file_actions_adddup2(&actions, streams[static_cast<std::size_t>(stream)], stream);
        }

#if defined(__APPLE__)
        if (streams[static_cast<std::size_t>(stream)] < 0) {
            posix_spawn_file_actions_addinherit_np(&actions, stream);
        }
#endif
    }

#if defined(__linux__)
    posix_spawn_file_actions_addclosefrom_np(&actions, STDERR_FILENO + 1);
#endif

    pid_t child = 0;
    const int failure = posix_spawnp(&child, m_argv.front(), &actions, &attributes, m_argv.data(), m_envp.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);

    if (failure != 0) {
        return Result<pid_t>::failure({"program_spawn_failed", "The system could not start the program", std::to_string(failure)});
    }

    return Result<pid_t>::success(child);
}

// The strings handed to a new program are owned as writable, terminated characters, which is the form it receives them in.
std::vector<std::vector<char>> ProgramImage::terminated(const std::vector<std::string>& texts) {
    std::vector<std::vector<char>> owned;

    for (const auto& text : texts) {
        std::vector<char> characters(text.begin(), text.end());
        characters.push_back('\0');
        owned.push_back(std::move(characters));
    }

    return owned;
}

std::vector<char*> ProgramImage::pointers(std::vector<std::vector<char>>& texts) {
    std::vector<char*> pointed;

    for (auto& text : texts) {
        pointed.push_back(text.data());
    }

    pointed.push_back(nullptr);

    return pointed;
}

} // namespace workpane::platform
