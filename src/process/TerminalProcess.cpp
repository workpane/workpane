#include "process/TerminalProcess.h"

#include "ui/PseudoTerminal.h"
#include "ui/PseudoTerminalHost.h"
#include "ui/TerminalLaunch.h"

#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace workpane::process {

struct TerminalProcess::Session final {
    std::mutex mutex;
    std::mutex wakeMutex;
    std::condition_variable ready;
    std::unique_ptr<ui::PseudoTerminal> terminal;
    ProcessEvents events;
    bool changed{true};
    bool closed{false};
};

Result<std::unique_ptr<Process>> TerminalProcess::start(ui::PseudoTerminalHost& host, const ProcessLaunch& launch, int columns, int rows, ProcessEvents events) {
    if (const auto valid = launch.validate(); !valid.hasValue()) {
        return Result<std::unique_ptr<Process>>::failure(valid.error());
    }

    if (launch.input.has_value()) {
        return Result<std::unique_ptr<Process>>::failure({"terminal_launch_invalid", "A terminal program keeps its input open for interactive writes", ""});
    }

    if (columns < 1 || columns > 10000 || rows < 1 || rows > 10000) {
        return Result<std::unique_ptr<Process>>::failure({"terminal_size_invalid", "The terminal dimensions are outside their bounds", ""});
    }

    auto session = std::make_shared<Session>();
    session->events = std::move(events);
    ui::TerminalLaunch requested{launch.directory, columns, rows, {}, {}, launch, {}};
    const std::weak_ptr<Session> owned = session;
    // clang-format off
    requested.arrived = [owned]() {
        if (const auto running = owned.lock()) {
            const std::lock_guard lock(running->wakeMutex);
            running->changed = true;
            running->ready.notify_one();
        }
    };
    // clang-format on

    auto started = host.start(requested);

    if (!started.hasValue()) {
        return Result<std::unique_ptr<Process>>::failure(started.error());
    }

    session->terminal = std::move(started.value());

    return Result<std::unique_ptr<Process>>::success(std::unique_ptr<Process>(new TerminalProcess(std::move(session))));
}

TerminalProcess::TerminalProcess(std::shared_ptr<Session> session) : m_session(std::move(session)) {
    // clang-format off
    m_thread = std::thread([running = m_session]() { run(*running); });
    // clang-format on
}

TerminalProcess::~TerminalProcess() {
    stop();
    m_thread.join();
}

bool TerminalProcess::write(std::string_view bytes) {
    const std::lock_guard lock(m_session->mutex);

    return !m_session->closed && m_session->terminal != nullptr && m_session->terminal->write(bytes);
}

bool TerminalProcess::resize(int columns, int rows) {
    if (columns < 1 || columns > 10000 || rows < 1 || rows > 10000) {
        return false;
    }

    const std::lock_guard lock(m_session->mutex);

    if (m_session->closed || m_session->terminal == nullptr) {
        return false;
    }

    m_session->terminal->resize(columns, rows);

    return true;
}

void TerminalProcess::stop() {
    {
        const std::lock_guard lock(m_session->mutex);
        m_session->closed = true;
        m_session->terminal.reset();
    }

    const std::lock_guard lock(m_session->wakeMutex);
    m_session->changed = true;
    m_session->ready.notify_one();
}

void TerminalProcess::run(Session& session) {
    while (true) {
        std::string output;
        std::optional<int> code;
        bool closed = false;

        {
            std::unique_lock lock(session.wakeMutex);
            // clang-format off
            session.ready.wait(lock, [&session]() { return session.changed; });
            // clang-format on
            session.changed = false;
        }

        {
            const std::lock_guard lock(session.mutex);
            closed = session.closed;

            if (!closed) {
                output = session.terminal->takeOutput(65536);
                code = session.terminal->exitCode();
            }

            if (closed || code.has_value()) {
                session.closed = true;
                session.terminal.reset();
            }
        }

        if (!output.empty()) {
            {
                const std::lock_guard lock(session.wakeMutex);
                session.changed = true;
            }

            session.events.output(ProcessStream::Output, std::move(output));
        }

        if (closed || code.has_value()) {
            session.events.exited({code.value_or(-1), closed});
            return;
        }
    }
}

} // namespace workpane::process
