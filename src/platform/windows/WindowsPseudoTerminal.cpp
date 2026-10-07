#include "platform/windows/WindowsPseudoTerminal.h"

#include "platform/TerminalEnvironment.h"
#include "platform/windows/WindowsEnvironment.h"
#include "platform/windows/WindowsTextHelper.h"

#include <array>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace workpane::platform {

// The state one shell shares between its terminal and the threads that serve it, kept until the watcher has ended the shell even after the terminal is gone.
struct WindowsPseudoTerminal::Session final {
    Session(HPCON pseudoConsole, HANDLE inputEnd, HANDLE outputEnd, HANDLE shell, HANDLE closingEvent, HANDLE drainedEvent, std::function<void()> listener, std::shared_ptr<ShellThreads> running) : console(pseudoConsole), inputPipe(inputEnd), outputPipe(outputEnd), process(shell), closing(closingEvent), drained(drainedEvent), arrived(std::move(listener)), threads(std::move(running)) {
        threads->enter();
    }

    // The shell leaves the count of its host only once nothing of it is left open.
    ~Session() {
        CloseHandle(inputPipe);
        CloseHandle(outputPipe);
        CloseHandle(process);
        CloseHandle(closing);
        CloseHandle(drained);
        threads->leave();
    }

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    HPCON console;
    HANDLE inputPipe;
    HANDLE outputPipe;
    HANDLE process;
    HANDLE closing;
    HANDLE drained;
    std::function<void()> arrived;
    std::shared_ptr<ShellThreads> threads;
    mutable std::mutex mutex;
    std::condition_variable inputReady;
    std::condition_variable outputTaken;
    std::string output;
    std::string pending;
    std::optional<int> exit;
    bool inputClosed{false};
    bool closed{false};
    std::thread reader;
};

Result<std::unique_ptr<ui::PseudoTerminal>> WindowsPseudoTerminal::start(const ShellCommand& shell, const ui::TerminalLaunch& launch, const std::vector<std::string>& additions, std::function<void()> arrived, const std::shared_ptr<ShellThreads>& threads) {
    std::error_code error;

    if (!std::filesystem::is_directory(launch.directory, error)) {
        return Result<std::unique_ptr<ui::PseudoTerminal>>::failure({"terminal_directory_missing", "The directory the terminal opens in is gone", launch.directory.string()});
    }

    if (!std::filesystem::is_regular_file(shell.program, error)) {
        return Result<std::unique_ptr<ui::PseudoTerminal>>::failure({"terminal_shell_not_executable", "The shell cannot be run", shell.program});
    }

    HANDLE inputRead = nullptr;
    HANDLE inputWrite = nullptr;
    HANDLE outputRead = nullptr;
    HANDLE outputWrite = nullptr;

    if (!CreatePipe(&inputRead, &inputWrite, nullptr, 0)) {
        return Result<std::unique_ptr<ui::PseudoTerminal>>::failure({"terminal_spawn_failed", "The terminal could not create its pipes", std::to_string(GetLastError())});
    }

    if (!CreatePipe(&outputRead, &outputWrite, nullptr, 0)) {
        const DWORD failure = GetLastError();
        CloseHandle(inputRead);
        CloseHandle(inputWrite);
        return Result<std::unique_ptr<ui::PseudoTerminal>>::failure({"terminal_spawn_failed", "The terminal could not create its pipes", std::to_string(failure)});
    }

    HPCON console = nullptr;
    const HRESULT created = CreatePseudoConsole(COORD{static_cast<SHORT>(launch.columns), static_cast<SHORT>(launch.rows)}, inputRead, outputWrite, 0, &console);
    CloseHandle(inputRead);
    CloseHandle(outputWrite);

    if (FAILED(created)) {
        CloseHandle(inputWrite);
        CloseHandle(outputRead);
        return Result<std::unique_ptr<ui::PseudoTerminal>>::failure({"terminal_spawn_failed", "The pseudo console could not be created", std::to_string(created)});
    }

    HANDLE closing = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE drained = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    SIZE_T attributesSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributesSize);
    std::vector<std::byte> attributes(attributesSize);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = static_cast<DWORD>(sizeof(STARTUPINFOEXW));
    // Empty standard handles keep the shell on the console even when the product itself was started with its streams redirected.
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    const bool initialized = closing != nullptr && drained != nullptr && InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attributesSize) != 0;
    const bool prepared = initialized && UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, console, sizeof(HPCON), nullptr, nullptr) != 0;
    std::wstring commandLine = WindowsTextHelper::quoted(WindowsTextHelper::wide(shell.program));

    for (const auto& argument : shell.arguments) {
        commandLine += L" " + WindowsTextHelper::quoted(WindowsTextHelper::wide(argument));
    }

    std::wstring environment = WindowsEnvironment::block(TerminalEnvironment::build(WindowsEnvironment::inherited(), additions));
    const std::wstring directory = launch.directory.wstring();
    PROCESS_INFORMATION process{};
    const bool started = prepared && CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, environment.data(), directory.c_str(), &startup.StartupInfo, &process) != 0;
    const DWORD failure = GetLastError();

    if (initialized) {
        DeleteProcThreadAttributeList(startup.lpAttributeList);
    }

    if (!started) {
        ClosePseudoConsole(console);
        CloseHandle(inputWrite);
        CloseHandle(outputRead);

        if (closing != nullptr) {
            CloseHandle(closing);
        }

        if (drained != nullptr) {
            CloseHandle(drained);
        }

        return Result<std::unique_ptr<ui::PseudoTerminal>>::failure({"terminal_spawn_failed", "The shell process could not be started", std::to_string(failure)});
    }

    CloseHandle(process.hThread);

    return Result<std::unique_ptr<ui::PseudoTerminal>>::success(std::unique_ptr<ui::PseudoTerminal>(new WindowsPseudoTerminal(std::make_shared<Session>(console, inputWrite, outputRead, process.hProcess, closing, drained, std::move(arrived), threads))));
}

// The threads hold the session rather than the terminal, so they run on after the terminal is gone until its shell has ended, and the watcher joins the reader.
WindowsPseudoTerminal::WindowsPseudoTerminal(std::shared_ptr<Session> session) : m_session(std::move(session)) {
    // clang-format off
    m_session->reader = std::thread([shared = m_session]() { read(*shared); });
    std::thread([shared = m_session]() { deliver(*shared); }).detach();
    std::thread([shared = m_session]() { watch(*shared); }).detach();
    // clang-format on
}

// Closing never waits for the shell, and the flag is set under the lock every waiting thread and every report checks, so none misses the wake and nothing reaches the product once the terminal is gone.
WindowsPseudoTerminal::~WindowsPseudoTerminal() {
    {
        const std::lock_guard lock(m_session->mutex);
        m_session->closed = true;
    }

    SetEvent(m_session->closing);
    m_session->inputReady.notify_all();
    m_session->outputTaken.notify_all();
}

std::string WindowsPseudoTerminal::takeOutput(std::size_t limit) {
    std::string taken;

    {
        const std::lock_guard lock(m_session->mutex);
        taken = m_session->output.substr(0, limit);
        m_session->output.erase(0, taken.size());
    }

    m_session->outputTaken.notify_one();

    return taken;
}

std::optional<int> WindowsPseudoTerminal::exitCode() const {
    const std::lock_guard lock(m_session->mutex);
    return m_session->output.empty() ? m_session->exit : std::nullopt;
}

bool WindowsPseudoTerminal::write(std::string_view bytes) {
    {
        const std::lock_guard lock(m_session->mutex);

        if (m_session->exit.has_value() || m_session->inputClosed || m_session->pending.size() + bytes.size() > maximumPendingInput) {
            return false;
        }

        m_session->pending.append(bytes);
    }

    m_session->inputReady.notify_one();

    return true;
}

// The console is resized under the lock the watcher closes it with, so a resize never reaches a console that was already closed.
void WindowsPseudoTerminal::resize(int columns, int rows) {
    const std::lock_guard lock(m_session->mutex);

    if (m_session->console == nullptr) {
        return;
    }

    ResizePseudoConsole(m_session->console, COORD{static_cast<SHORT>(columns), static_cast<SHORT>(rows)});
}

// Windows tells nobody where another process stands, so the directory reaches the terminal through the marker its PowerShell prompt writes.
std::string WindowsPseudoTerminal::directory() const {
    return {};
}

// Reading waits while the bound of pending output is reached, so the console holds the program back instead of the product growing without end, and a closed terminal drains the console without reporting.
void WindowsPseudoTerminal::read(Session& session) {
    std::string buffer(readChunk, '\0');
    DWORD count = 0;

    while (ReadFile(session.outputPipe, buffer.data(), readChunk, &count, nullptr) && count > 0) {
        std::unique_lock lock(session.mutex);
        session.output.append(buffer.data(), count);
        // clang-format off
        session.outputTaken.wait(lock, [&session]() { return session.closed || session.output.size() < maximumPendingOutput; });
        // clang-format on

        if (!session.closed) {
            session.arrived();
        }
    }

    SetEvent(session.drained);
}

// Input is written on a thread of its own, because a console that is not reading would otherwise stop the interface.
void WindowsPseudoTerminal::deliver(Session& session) {
    while (true) {
        std::string chunk;

        {
            std::unique_lock lock(session.mutex);
            // clang-format off
            session.inputReady.wait(lock, [&session]() { return session.closed || !session.pending.empty(); });
            // clang-format on

            if (session.closed) {
                return;
            }

            chunk = std::exchange(session.pending, {});
        }

        DWORD written = 0;

        if (!WriteFile(session.inputPipe, chunk.data(), static_cast<DWORD>(chunk.size()), &written, nullptr)) {
            const std::lock_guard lock(session.mutex);
            session.inputClosed = true;
            return;
        }
    }
}

// Closing the console asks every program attached to it to leave, a shell that ignores it ends after its grace, and the last output drains within a bound, since a program still attached could otherwise keep the console open forever.
void WindowsPseudoTerminal::watch(Session& session) {
    const std::array<HANDLE, 2> waited{session.process, session.closing};
    const DWORD signalled = WaitForMultipleObjects(static_cast<DWORD>(waited.size()), waited.data(), FALSE, INFINITE);
    HPCON console = nullptr;

    {
        const std::lock_guard lock(session.mutex);
        console = std::exchange(session.console, nullptr);
    }

    ClosePseudoConsole(console);

    if (signalled != WAIT_OBJECT_0 && WaitForSingleObject(session.process, closeGraceMilliseconds) != WAIT_OBJECT_0) {
        TerminateProcess(session.process, 1);
    }

    // A read still waiting once the bound passed is cancelled until the reader leaves, which a read begun just after a cancel needs again.
    for (DWORD bound = drainMilliseconds; WaitForSingleObject(session.drained, bound) == WAIT_TIMEOUT; bound = cancelRetryMilliseconds) {
        CancelIoEx(session.outputPipe, nullptr);
    }

    session.reader.join();
    WaitForSingleObject(session.process, INFINITE);

    DWORD code = 0;
    GetExitCodeProcess(session.process, &code);
    const std::lock_guard lock(session.mutex);
    session.exit = static_cast<int>(code);

    if (!session.closed) {
        session.arrived();
    }
}

} // namespace workpane::platform
