#include "platform/posix/PosixProcess.h"

#include "platform/posix/PosixPipe.h"
#include "platform/posix/ProgramImage.h"
#include "process/ProcessEnvironment.h"

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace workpane::platform {

Result<std::unique_ptr<process::Process>> PosixProcess::start(const process::ProcessLaunch& launch, process::ProcessEvents events) {
    std::error_code error;

    if (!std::filesystem::is_directory(launch.directory, error)) {
        return Result<std::unique_ptr<process::Process>>::failure({"process_directory_missing", "The directory the program starts in does not exist", launch.directory.string()});
    }

    if (!std::filesystem::is_regular_file(launch.program, error) || ::access(launch.program.c_str(), X_OK) != 0) {
        return Result<std::unique_ptr<process::Process>>::failure({"process_program_missing", "The program is not an executable file", launch.program.string()});
    }

    std::vector<std::string> arguments{launch.program.string()};
    arguments.insert(arguments.end(), launch.arguments.begin(), launch.arguments.end());
    const std::vector<std::string> environment = process::ProcessEnvironment::build(ProgramImage::inheritedEnvironment(), launch, false);
    ProgramImage image(arguments, environment, launch.directory.string());
    std::array<std::array<int, 2>, 4> pipes{{{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}}};

    for (auto& pipe : pipes) {
        if (!PosixPipe::open(pipe)) {
            closeAll(pipes);
            return Result<std::unique_ptr<process::Process>>::failure({"process_start_failed", "The pipes of the program could not be created", std::to_string(errno)});
        }
    }

    // The program joins a group of its own as it starts, so stopping it also stops whatever it started, and keeps no end of its pipes but its three streams, so its input ends when this side closes it.
    const auto spawned = image.spawn({pipes[0][0], pipes[1][1], pipes[2][1]}, ProgramImage::Grouping::Group);

    if (!spawned.hasValue()) {
        closeAll(pipes);
        return Result<std::unique_ptr<process::Process>>::failure({"process_start_failed", "The program could not be started", spawned.error().detail});
    }

    const pid_t child = spawned.value();

    ::close(pipes[0][0]);
    ::close(pipes[1][1]);
    ::close(pipes[2][1]);

    // A program given an empty input reads its end at once, so one that would wait for input finishes instead.
    if (launch.input.has_value() && launch.input->empty()) {
        ::close(pipes[0][1]);
        pipes[0][1] = -1;
    }

    // None of the descriptors of this program ever blocks the thread that serves it.
    for (const int descriptor : {pipes[0][1], pipes[1][0], pipes[2][0], pipes[3][0], pipes[3][1]}) {
        if (descriptor >= 0) {
            ::fcntl(descriptor, F_SETFL, ::fcntl(descriptor, F_GETFL) | O_NONBLOCK);
        }
    }

    return Result<std::unique_ptr<process::Process>>::success(std::unique_ptr<process::Process>(new PosixProcess(child, {pipes[0][1], pipes[1][0], pipes[2][0]}, pipes[3], std::move(events), launch.input)));
}

PosixProcess::PosixProcess(pid_t child, std::array<int, 3> pipes, std::array<int, 2> wake, process::ProcessEvents events, std::optional<std::string> input) : m_child(child), m_input(pipes[0]), m_output(pipes[1]), m_error(pipes[2]), m_wake(wake), m_events(std::move(events)), m_pending(input.value_or(std::string())), m_inputClosed(pipes[0] < 0), m_inputGiven(input.has_value()) {
    // clang-format off
    m_thread = std::thread([this]() { run(); });
    // clang-format on
}

PosixProcess::~PosixProcess() {
    stop();
    m_thread.join();
    ::close(m_wake[0]);
    ::close(m_wake[1]);
}

// Input waits in a bounded queue the serving thread writes whenever the program reads, so a large message never blocks its writer.
bool PosixProcess::write(std::string_view bytes) {
    {
        const std::lock_guard lock(m_mutex);

        if (m_ended || m_stopping || m_inputClosed || m_inputGiven || m_pending.size() + bytes.size() > maximumPendingInput) {
            return false;
        }

        m_pending.append(bytes);
    }

    wake();

    return true;
}

void PosixProcess::stop() {
    m_stopping = true;
    wake();
}

// A write to a program that already ended fails with a broken pipe, which the product ignores from its start instead of ending.
void PosixProcess::run() {
    std::string buffer(readChunk, '\0');
    bool terminated = false;
    auto deadline = std::chrono::steady_clock::now();

    while (m_output >= 0 || m_error >= 0) {
        bool pending = false;

        {
            const std::lock_guard lock(m_mutex);
            pending = !m_pending.empty();
        }

        // Stopping closes the input, asks the group to end and ends it for good once the grace has passed.
        if (m_stopping && !terminated) {
            terminated = true;
            deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(stopGraceMilliseconds);
            closeInput();
            ::kill(-m_child, SIGTERM);
        }

        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();

        // Once the grace has passed the group is killed, and a program outside it that still holds the streams is no longer waited for.
        if (terminated && remaining <= 0) {
            ::kill(-m_child, SIGKILL);
            release(m_output);
            release(m_error);
            break;
        }

        // The input is watched only while something waits to be written, because a pipe whose reader left reports an error on every poll.
        std::array<pollfd, 4> descriptors{{{m_output, POLLIN, 0}, {m_error, POLLIN, 0}, {pending ? m_input : -1, POLLOUT, 0}, {m_wake[0], POLLIN, 0}}};
        const int timeout = terminated ? static_cast<int>(std::max<long long>(remaining, 0)) : -1;

        if (::poll(descriptors.data(), descriptors.size(), timeout) < 0) {
            continue;
        }

        if ((descriptors[3].revents & POLLIN) != 0) {
            std::array<char, 64> drained{};

            while (::read(m_wake[0], drained.data(), drained.size()) > 0) {}
        }

        if ((descriptors[2].revents & (POLLOUT | POLLERR | POLLHUP)) != 0) {
            deliverInput();
        }

        if ((descriptors[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            readStream(m_output, process::ProcessStream::Output, buffer);
        }

        if ((descriptors[1].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            readStream(m_error, process::ProcessStream::Error, buffer);
        }
    }

    closeInput();
    const process::ProcessEnd end = reap();

    {
        const std::lock_guard lock(m_mutex);
        m_ended = true;
    }

    m_events.exited(end);
}

void PosixProcess::deliverInput() {
    const std::lock_guard lock(m_mutex);
    const ssize_t written = ::write(m_input, m_pending.data(), m_pending.size());

    // An input given at the start ends once the program took all of it.
    if (written > 0) {
        m_pending.erase(0, static_cast<std::size_t>(written));

        if (m_inputGiven && m_pending.empty()) {
            m_inputClosed = true;
            release(m_input);
        }

        return;
    }

    if (written == 0 || errno == EAGAIN || errno == EINTR) {
        return;
    }

    // A program that closed its input takes nothing more, so what waits for it is dropped and later writes are refused.
    m_pending.clear();
    m_inputClosed = true;
    release(m_input);
}

void PosixProcess::closeInput() {
    const std::lock_guard lock(m_mutex);
    m_pending.clear();
    m_inputClosed = true;
    release(m_input);
}

void PosixProcess::release(int& descriptor) {
    if (descriptor >= 0) {
        ::close(descriptor);
        descriptor = -1;
    }
}

void PosixProcess::closeAll(const std::array<std::array<int, 2>, 4>& pipes) {
    for (const auto& pipe : pipes) {
        for (const int descriptor : pipe) {
            if (descriptor >= 0) {
                ::close(descriptor);
            }
        }
    }
}

// A stream is closed once the program has closed its end.
void PosixProcess::readStream(int& descriptor, process::ProcessStream stream, std::string& buffer) {
    const ssize_t count = ::read(descriptor, buffer.data(), buffer.size());

    if (count < 0 && (errno == EAGAIN || errno == EINTR)) {
        return;
    }

    if (count <= 0) {
        release(descriptor);
        return;
    }

    m_events.output(stream, std::string(buffer.data(), static_cast<std::size_t>(count)));
}

// A program that closed its streams but keeps running is waited for, and ended too once stopping was asked for.
// A wait a signal interrupted is tried again, and a child the system no longer knows ends with no code it could tell, as a crash.
process::ProcessEnd PosixProcess::reap() {
    int status = 0;
    bool terminated = false;
    auto deadline = std::chrono::steady_clock::now();

    for (pid_t waited = ::waitpid(m_child, &status, WNOHANG); waited != m_child; waited = ::waitpid(m_child, &status, WNOHANG)) {
        if (waited < 0 && errno != EINTR) {
            return process::ProcessEnd{-1, true};
        }

        if (m_stopping && !terminated) {
            terminated = true;
            deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(stopGraceMilliseconds);
            ::kill(-m_child, SIGTERM);
        }

        if (terminated && std::chrono::steady_clock::now() >= deadline) {
            ::kill(-m_child, SIGKILL);
        }

        pollfd wakeDescriptor{m_wake[0], POLLIN, 0};
        std::ignore = ::poll(&wakeDescriptor, 1, reapStepMilliseconds);
        std::array<char, 64> drained{};

        while (::read(m_wake[0], drained.data(), drained.size()) > 0) {}
    }

    return WIFEXITED(status) ? process::ProcessEnd{WEXITSTATUS(status), false} : process::ProcessEnd{128 + WTERMSIG(status), true};
}

void PosixProcess::wake() const {
    const char signal = 1;
    std::ignore = ::write(m_wake[1], &signal, 1);
}

} // namespace workpane::platform
