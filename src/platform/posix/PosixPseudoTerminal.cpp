#include "platform/posix/PosixPseudoTerminal.h"

#include "platform/ProcessDirectory.h"
#include "platform/posix/ExitDescriptor.h"
#include "platform/posix/PosixPipe.h"
#include "platform/posix/ProgramImage.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif

#include <array>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace workpane::platform {

// The state one shell shares between its terminal and the thread that serves it, kept until that thread has ended the shell even after the terminal is gone.
struct PosixPseudoTerminal::Session final {
    Session(int descriptor, pid_t process, int endDescriptor, std::array<int, 2> wakeDescriptors, std::function<void()> listener, std::shared_ptr<ShellThreads> running) : master(descriptor), child(process), ended(endDescriptor), wakePipe(wakeDescriptors), arrived(std::move(listener)), threads(std::move(running)) {
        threads->enter();
    }

    // The shell leaves the count of its host only once nothing of it is left open.
    ~Session() {
        if (master >= 0) {
            ::close(master);
        }

        if (ended >= 0) {
            ::close(ended);
        }

        ::close(wakePipe[0]);
        ::close(wakePipe[1]);
        threads->leave();
    }

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    int master;
    pid_t child;
    int ended;
    std::array<int, 2> wakePipe;
    std::function<void()> arrived;
    std::shared_ptr<ShellThreads> threads;
    mutable std::mutex mutex;
    std::string output;
    std::string input;
    std::optional<int> exit;
    bool closing{false};
};

Result<std::unique_ptr<ui::PseudoTerminal>> PosixPseudoTerminal::start(const ShellCommand& shell, const ui::TerminalLaunch& launch, const std::vector<std::string>& environment, std::function<void()> arrived, const std::shared_ptr<ShellThreads>& threads) {
    std::error_code error;

    if (!std::filesystem::is_directory(launch.directory, error)) {
        return Result<std::unique_ptr<ui::PseudoTerminal>>::failure({"terminal_directory_missing", "The directory the terminal opens in is gone", launch.directory.string()});
    }

    if (::access(shell.program.c_str(), X_OK) != 0) {
        return Result<std::unique_ptr<ui::PseudoTerminal>>::failure({"terminal_shell_not_executable", "The shell cannot be run", shell.program});
    }

    std::vector<std::string> arguments{shell.program};
    arguments.insert(arguments.end(), shell.arguments.begin(), shell.arguments.end());
    ProgramImage image(arguments, environment, launch.directory.string());
    std::array<int, 2> wake{-1, -1};

    if (!PosixPipe::open(wake)) {
        return Result<std::unique_ptr<ui::PseudoTerminal>>::failure({"terminal_spawn_failed", "The terminal could not create its wake pipe", std::to_string(errno)});
    }

    winsize size{static_cast<unsigned short>(launch.rows), static_cast<unsigned short>(launch.columns), 0, 0};
    int master = -1;
    int slave = -1;
    std::array<char, 256> terminal{};

    if (::openpty(&master, &slave, nullptr, nullptr, &size) != 0 || ::ttyname_r(slave, terminal.data(), terminal.size()) != 0) {
        const int failure = errno;
        ::close(master);
        ::close(slave);
        ::close(wake[0]);
        ::close(wake[1]);
        return Result<std::unique_ptr<ui::PseudoTerminal>>::failure({"terminal_spawn_failed", "The terminal of the shell could not be opened", std::to_string(failure)});
    }

    // The line discipline edits UTF-8, so erasing a character of a line a program reads erases every byte of it.
    termios attributes{};

    if (::tcgetattr(slave, &attributes) == 0) {
        attributes.c_iflag |= IUTF8;
        ::tcsetattr(slave, TCSANOW, &attributes);
    }

    // The terminal stays out of every program started after it, and none of its descriptors ever blocks the thread that serves it.
    ::fcntl(master, F_SETFD, FD_CLOEXEC);
    ::fcntl(slave, F_SETFD, FD_CLOEXEC);
    const auto spawned = image.spawnOnTerminal(terminal.data());
    ::close(slave);

    if (!spawned.hasValue()) {
        ::close(master);
        ::close(wake[0]);
        ::close(wake[1]);
        return Result<std::unique_ptr<ui::PseudoTerminal>>::failure({"terminal_spawn_failed", "The shell process could not be started", spawned.error().detail});
    }

    const pid_t child = spawned.value();

    for (const int descriptor : {master, wake[0], wake[1]}) {
        ::fcntl(descriptor, F_SETFL, ::fcntl(descriptor, F_GETFL) | O_NONBLOCK);
    }

    return Result<std::unique_ptr<ui::PseudoTerminal>>::success(std::unique_ptr<ui::PseudoTerminal>(new PosixPseudoTerminal(std::make_shared<Session>(master, child, ExitDescriptor::open(child), wake, std::move(arrived), threads))));
}

// The thread holds the session rather than the terminal, so it runs on after the terminal is gone until its shell has ended.
PosixPseudoTerminal::PosixPseudoTerminal(std::shared_ptr<Session> session) : m_session(std::move(session)) {
    // clang-format off
    std::thread([shared = m_session]() { run(*shared); }).detach();
    // clang-format on
}

// Closing never waits for the shell, and the flag is set under the lock every report of the thread checks, so nothing reaches the product once the terminal is gone.
PosixPseudoTerminal::~PosixPseudoTerminal() {
    {
        const std::lock_guard lock(m_session->mutex);
        m_session->closing = true;
    }

    wake(*m_session);
}

// Taking output below the bound the reading waits on wakes the reading thread, which had stopped reading the shell.
std::string PosixPseudoTerminal::takeOutput(std::size_t limit) {
    std::string taken;
    bool resumed = false;

    {
        const std::lock_guard lock(m_session->mutex);
        const bool paused = m_session->output.size() >= maximumPendingOutput;
        taken = m_session->output.substr(0, limit);
        m_session->output.erase(0, taken.size());
        resumed = paused && m_session->output.size() < maximumPendingOutput;
    }

    if (resumed) {
        wake(*m_session);
    }

    return taken;
}

std::optional<int> PosixPseudoTerminal::exitCode() const {
    const std::lock_guard lock(m_session->mutex);
    return m_session->output.empty() ? m_session->exit : std::nullopt;
}

// Input waits in a bounded queue the terminal thread writes whenever the shell reads, so a large paste never blocks the interface.
bool PosixPseudoTerminal::write(std::string_view bytes) {
    {
        const std::lock_guard lock(m_session->mutex);

        if (m_session->exit.has_value() || m_session->input.size() + bytes.size() > maximumPendingInput) {
            return false;
        }

        m_session->input.append(bytes);
    }

    wake(*m_session);

    return true;
}

void PosixPseudoTerminal::resize(int columns, int rows) {
    winsize size{static_cast<unsigned short>(rows), static_cast<unsigned short>(columns), 0, 0};
    ::ioctl(m_session->master, TIOCSWINSZ, &size);
}

// The directory is the one of the program in the foreground, which is the shell itself between commands, and a shell that ended has none, since its number may already name another process.
std::string PosixPseudoTerminal::directory() const {
    {
        const std::lock_guard lock(m_session->mutex);

        if (m_session->exit.has_value()) {
            return {};
        }
    }

    const pid_t foreground = ::tcgetpgrp(m_session->master);
    return ProcessDirectory::of(foreground > 0 ? foreground : m_session->child);
}

// The thread serves the shell until the shell ends or the terminal closes, and reports the end only to a terminal still open.
void PosixPseudoTerminal::run(Session& session) {
    std::string buffer(readChunk, '\0');

    while (true) {
        bool pending = false;
        bool full = false;

        {
            const std::lock_guard lock(session.mutex);

            if (session.closing) {
                break;
            }

            pending = !session.input.empty();
            full = session.output.size() >= maximumPendingOutput;
        }

        std::array<pollfd, 3> descriptors{{{session.master, static_cast<short>((full ? 0 : POLLIN) | (pending ? POLLOUT : 0)), 0}, {session.wakePipe[0], POLLIN, 0}, {session.ended, POLLIN, 0}}};

        if (::poll(descriptors.data(), descriptors.size(), -1) < 0) {
            continue;
        }

        if ((descriptors[1].revents & POLLIN) != 0) {
            std::array<char, 64> drained{};

            while (::read(session.wakePipe[0], drained.data(), drained.size()) > 0) {}
        }

        if ((descriptors[0].revents & POLLOUT) != 0) {
            deliverInput(session);
        }

        if ((descriptors[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0 && !readOutput(session, buffer)) {
            break;
        }

        // A shell that ended while a program it left behind still holds the terminal ends here, after the output it wrote before it ended.
        if ((descriptors[2].revents & POLLIN) != 0) {
            while (readOutput(session, buffer) && ::poll(descriptors.data(), 1, 0) > 0 && (descriptors[0].revents & POLLIN) != 0) {}

            break;
        }
    }

    const int code = reap(session);
    const std::lock_guard lock(session.mutex);
    session.exit = code;

    if (!session.closing) {
        session.arrived();
    }
}

void PosixPseudoTerminal::deliverInput(Session& session) {
    const std::lock_guard lock(session.mutex);
    const ssize_t written = ::write(session.master, session.input.data(), session.input.size());

    if (written > 0) {
        session.input.erase(0, static_cast<std::size_t>(written));
    }
}

// Answers false once the shell side of the terminal is gone, which is how the end of the program shows on the master side.
bool PosixPseudoTerminal::readOutput(Session& session, std::string& buffer) {
    const ssize_t count = ::read(session.master, buffer.data(), buffer.size());

    if (count < 0 && (errno == EAGAIN || errno == EINTR)) {
        return true;
    }

    if (count <= 0) {
        return false;
    }

    const std::lock_guard lock(session.mutex);
    session.output.append(buffer.data(), static_cast<std::size_t>(count));

    if (!session.closing) {
        session.arrived();
    }

    return true;
}

// A terminal closed by the reader hangs up by closing its side of the terminal, as closing a window of a terminal does, and the system sends the hangup to the shell and its foreground program.
// Closing comes before any wait that blocks, because on macOS a shell that leads its session cannot finish ending while output nobody reads still waits in the terminal, and after a short grace whatever still holds on is ended.
// A shell that let go of its terminal may keep running, so waiting for it also watches for the reader closing the terminal.
int PosixPseudoTerminal::reap(Session& session) {
    int status = 0;
    bool closing = false;

    while (!closing) {
        if (::waitpid(session.child, &status, WNOHANG) == session.child) {
            return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        }

        {
            const std::lock_guard lock(session.mutex);
            closing = session.closing;
        }

        pollfd wakeDescriptor{session.wakePipe[0], POLLIN, 0};

        if (!closing && ::poll(&wakeDescriptor, 1, reapStepMilliseconds) > 0) {
            std::array<char, 64> drained{};

            while (::read(session.wakePipe[0], drained.data(), drained.size()) > 0) {}
        }
    }

    ::close(std::exchange(session.master, -1));

    for (int waited = 0; waited < hangupGraceMilliseconds; waited += hangupStepMilliseconds) {
        if (::waitpid(session.child, &status, WNOHANG) == session.child) {
            return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(hangupStepMilliseconds));
    }

    ::kill(session.child, SIGKILL);

    while (::waitpid(session.child, &status, 0) < 0 && errno == EINTR) {}

    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

void PosixPseudoTerminal::wake(const Session& session) {
    const char signal = 1;
    std::ignore = ::write(session.wakePipe[1], &signal, 1);
}

} // namespace workpane::platform
