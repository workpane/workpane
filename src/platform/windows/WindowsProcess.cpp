#include "platform/windows/WindowsProcess.h"

#include "platform/windows/WindowsEnvironment.h"
#include "platform/windows/WindowsTextHelper.h"
#include "process/ProcessEnvironment.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace workpane::platform {

// The child inherits exactly its three pipe ends, starts suspended inside a job that ends with it, and runs without a console window of its own.
Result<std::unique_ptr<process::Process>> WindowsProcess::start(const process::ProcessLaunch& launch, process::ProcessEvents events) {
    std::error_code error;

    if (!std::filesystem::is_directory(launch.directory, error)) {
        return Result<std::unique_ptr<process::Process>>::failure({"process_directory_missing", "The directory the program starts in does not exist", launch.directory.string()});
    }

    if (!std::filesystem::is_regular_file(launch.program, error)) {
        return Result<std::unique_ptr<process::Process>>::failure({"process_program_missing", "The program is not an executable file", launch.program.string()});
    }

    // clang-format off
    const auto unsafe = [](const std::string& argument) { return argument.find_first_of(batchMetacharacters) != std::string::npos; };
    // clang-format on

    if (batch(launch.program) && std::ranges::any_of(launch.arguments, unsafe)) {
        return Result<std::unique_ptr<process::Process>>::failure({"process_argument_unsafe", "The command prompt that runs a batch file would read a character of an argument as a command", launch.program.string()});
    }

    SECURITY_ATTRIBUTES inherited{static_cast<DWORD>(sizeof(SECURITY_ATTRIBUTES)), nullptr, TRUE};
    std::array<HANDLE, 6> pipes{};

    if (!CreatePipe(&pipes[0], &pipes[1], &inherited, 0) || !CreatePipe(&pipes[2], &pipes[3], &inherited, 0) || !CreatePipe(&pipes[4], &pipes[5], &inherited, 0)) {
        closeAll({pipes.begin(), pipes.end()});
        return Result<std::unique_ptr<process::Process>>::failure({"process_start_failed", "The pipes of the program could not be created", std::to_string(GetLastError())});
    }

    SetHandleInformation(pipes[1], HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(pipes[2], HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(pipes[4], HANDLE_FLAG_INHERIT, 0);

    // The child inherits exactly its three ends, named in an attribute list whose size the system answers first.
    std::array<HANDLE, 3> childEnds{pipes[0], pipes[3], pipes[5]};
    SIZE_T attributesSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributesSize);
    std::vector<std::byte> attributes(attributesSize);

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = static_cast<DWORD>(sizeof(STARTUPINFOEXW));
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = pipes[0];
    startup.StartupInfo.hStdOutput = pipes[3];
    startup.StartupInfo.hStdError = pipes[5];
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());

    const bool initialized = InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attributesSize) != 0;
    const bool prepared = initialized && UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, childEnds.data(), sizeof(childEnds), nullptr, nullptr) != 0;

    std::wstring command = commandLine(launch);
    std::wstring environment = WindowsEnvironment::block(process::ProcessEnvironment::build(WindowsEnvironment::inherited(), launch, true));
    const std::wstring directory = launch.directory.wstring();
    PROCESS_INFORMATION started{};
    const bool created = prepared && CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED | CREATE_NO_WINDOW, environment.data(), directory.c_str(), &startup.StartupInfo, &started) != 0;
    const DWORD failure = GetLastError();

    if (initialized) {
        DeleteProcThreadAttributeList(startup.lpAttributeList);
    }

    closeAll({pipes[0], pipes[3], pipes[5]});

    if (!created) {
        closeAll({pipes[1], pipes[2], pipes[4]});
        return Result<std::unique_ptr<process::Process>>::failure({"process_start_failed", "The program could not be started", std::to_string(failure)});
    }

    // The program stays suspended until it is inside a job that ends with the product, so nothing it starts can outlive a stop.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

    if (job == nullptr || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) || !AssignProcessToJobObject(job, started.hProcess)) {
        const DWORD refused = GetLastError();
        TerminateProcess(started.hProcess, 1);
        closeAll({started.hThread, started.hProcess, job, pipes[1], pipes[2], pipes[4]});
        return Result<std::unique_ptr<process::Process>>::failure({"process_start_failed", "The program could not be placed in a job that ends with it", std::to_string(refused)});
    }

    ResumeThread(started.hThread);
    CloseHandle(started.hThread);

    return Result<std::unique_ptr<process::Process>>::success(std::unique_ptr<process::Process>(new WindowsProcess(Handles{started.hProcess, job, pipes[1], pipes[2], pipes[4]}, std::move(events), launch.input)));
}

// A batch file only runs inside the command prompt, which receives the whole quoted command line to strip its outer quotes from.
std::wstring WindowsProcess::commandLine(const process::ProcessLaunch& launch) {
    std::wstring arguments = WindowsTextHelper::quoted(launch.program.wstring());

    for (const auto& argument : launch.arguments) {
        arguments += L" " + WindowsProcess::argument(WindowsTextHelper::wide(argument));
    }

    if (!batch(launch.program)) {
        return arguments;
    }

    std::array<wchar_t, MAX_PATH> system{};
    const UINT length = GetSystemDirectoryW(system.data(), static_cast<UINT>(system.size()));
    const std::wstring prompt = std::wstring(system.data(), length) + L"\\cmd.exe";
    return WindowsTextHelper::quoted(prompt) + L" /d /s /c \"" + arguments + L"\"";
}

bool WindowsProcess::batch(const std::filesystem::path& program) {
    std::wstring extension = program.extension().wstring();
    // clang-format off
    std::ranges::transform(extension, extension.begin(), [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
    // clang-format on
    return extension == L".cmd" || extension == L".bat";
}

// An argument is quoted only when it needs to be, because a program such as the command prompt reads its switches unquoted.
std::wstring WindowsProcess::argument(const std::wstring& text) {
    return text.empty() || text.find_first_of(L" \t\"") != std::wstring::npos ? WindowsTextHelper::quoted(text) : text;
}

void WindowsProcess::closeAll(const std::vector<HANDLE>& handles) {
    for (HANDLE handle : handles) {
        if (handle != nullptr) {
            CloseHandle(handle);
        }
    }
}

// A program given an input reads it and then its end, which an empty one reaches at once because the writer closes it before anything is written.
WindowsProcess::WindowsProcess(Handles handles, process::ProcessEvents events, std::optional<std::string> input) : m_handles(handles), m_stopping(CreateEventW(nullptr, TRUE, FALSE, nullptr)), m_events(std::move(events)), m_pending(input.value_or(std::string())), m_inputClosed(input.has_value() && input->empty()), m_inputGiven(input.has_value()) {
    // clang-format off
    m_outputReader = std::thread([this]() { read(m_handles.output, process::ProcessStream::Output); });
    m_errorReader = std::thread([this]() { read(m_handles.error, process::ProcessStream::Error); });
    m_writer = std::thread([this]() { deliver(); });
    m_watcher = std::thread([this]() { watch(); });
    // clang-format on
}

// The job ends before the writer is joined, since a descendant holding the input without reading would keep a write waiting, and closing the job ends every descendant anyway.
WindowsProcess::~WindowsProcess() {
    stop();
    m_watcher.join();
    TerminateJobObject(m_handles.job, 1);
    m_writer.join();
    closeAll({m_handles.process, m_handles.job, m_handles.output, m_handles.error, m_stopping});
}

// Input waits in a bounded queue its own thread writes, so a program that is not reading never stops the writer.
bool WindowsProcess::write(std::string_view bytes) {
    {
        const std::lock_guard lock(m_mutex);

        if (m_ended || m_inputClosed || m_inputGiven || m_pending.size() + bytes.size() > maximumPendingInput) {
            return false;
        }

        m_pending.append(bytes);
    }

    m_changed.notify_all();

    return true;
}

void WindowsProcess::stop() {
    SetEvent(m_stopping);
}

void WindowsProcess::read(HANDLE stream, process::ProcessStream kind) {
    std::string buffer(readChunk, '\0');
    DWORD count = 0;

    while (ReadFile(stream, buffer.data(), readChunk, &count, nullptr) && count > 0) {
        m_events.output(kind, std::string(buffer.data(), count));
    }

    {
        const std::lock_guard lock(m_mutex);
        ++m_readersDone;
    }

    m_changed.notify_all();
}

// The writer ends when the input closes, which also drops what the program never read, or once it wrote all of an input given at the start.
void WindowsProcess::deliver() {
    while (true) {
        std::string chunk;

        {
            std::unique_lock lock(m_mutex);
            // clang-format off
            m_changed.wait(lock, [this]() { return m_inputClosed || !m_pending.empty() || m_inputGiven; });
            // clang-format on

            if (m_inputClosed || (m_inputGiven && m_pending.empty())) {
                break;
            }

            chunk = std::exchange(m_pending, {});
        }

        DWORD written = 0;

        if (!WriteFile(m_handles.input, chunk.data(), static_cast<DWORD>(chunk.size()), &written, nullptr)) {
            break;
        }
    }

    // The input closes under the lock a stop cancels it with, so a cancel never reaches a handle that was closed and reused.
    const std::lock_guard lock(m_mutex);
    m_inputClosed = true;
    CloseHandle(std::exchange(m_handles.input, nullptr));
}

// However the program ends its input closes and a pending write is cancelled, a stop ends the whole job after the grace, and the end is told only once both streams have drained.
// A descendant still holding the streams after the grace is ended with the job, which closes every end the readers wait on, since closing the job ends it anyway.
void WindowsProcess::watch() {
    const std::array<HANDLE, 2> waited{m_handles.process, m_stopping};
    const DWORD signalled = WaitForMultipleObjects(static_cast<DWORD>(waited.size()), waited.data(), FALSE, INFINITE);

    {
        const std::lock_guard lock(m_mutex);
        m_inputClosed = true;

        if (m_handles.input != nullptr) {
            CancelIoEx(m_handles.input, nullptr);
        }
    }

    m_changed.notify_all();

    if (signalled != WAIT_OBJECT_0 && WaitForSingleObject(m_handles.process, stopGraceMilliseconds) != WAIT_OBJECT_0) {
        TerminateJobObject(m_handles.job, 1);
        WaitForSingleObject(m_handles.process, INFINITE);
    }

    {
        std::unique_lock lock(m_mutex);
        // clang-format off
        const bool drained = m_changed.wait_for(lock, std::chrono::milliseconds(stopGraceMilliseconds), [this]() { return m_readersDone == 2; });
        // clang-format on

        if (!drained) {
            TerminateJobObject(m_handles.job, 1);
        }
    }

    m_outputReader.join();
    m_errorReader.join();
    DWORD code = 0;
    GetExitCodeProcess(m_handles.process, &code);

    {
        const std::lock_guard lock(m_mutex);
        m_ended = true;
    }

    // A status in the failure range of Windows, such as an access violation, means the program crashed rather than ended.
    m_events.exited({static_cast<int>(code), code >= crashStatus});
}

} // namespace workpane::platform
