#pragma once

#include "Result.h"
#include "process/Process.h"
#include "process/ProcessEvents.h"
#include "process/ProcessLaunch.h"
#include "process/ProcessStream.h"

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace workpane::platform {

// A program started with pipes inside a job object, read and written on threads of its own, and ended with everything it started once the grace after a stop has passed.
class WindowsProcess final : public process::Process {
  public:
    [[nodiscard]] static Result<std::unique_ptr<process::Process>> start(const process::ProcessLaunch& launch, process::ProcessEvents events);

    ~WindowsProcess() override;
    WindowsProcess(const WindowsProcess&) = delete;
    WindowsProcess& operator=(const WindowsProcess&) = delete;

    [[nodiscard]] bool write(std::string_view bytes) override;
    void stop() override;

  private:
    struct Handles final {
        HANDLE process;
        HANDLE job;
        HANDLE input;
        HANDLE output;
        HANDLE error;
    };

    static constexpr DWORD readChunk{65536};
    static constexpr std::size_t maximumPendingInput{64U << 20U};
    static constexpr DWORD stopGraceMilliseconds{2000};
    static constexpr DWORD crashStatus{0xC0000000};
    static constexpr const char* batchMetacharacters{"\"%!^&|<>()\r\n"};

    WindowsProcess(Handles handles, process::ProcessEvents events, std::optional<std::string> input);
    [[nodiscard]] static std::wstring commandLine(const process::ProcessLaunch& launch);
    [[nodiscard]] static bool batch(const std::filesystem::path& program);
    [[nodiscard]] static std::wstring argument(const std::wstring& text);
    static void closeAll(const std::vector<HANDLE>& handles);
    void read(HANDLE stream, process::ProcessStream kind);
    void deliver();
    void watch();

    Handles m_handles;
    HANDLE m_stopping;
    process::ProcessEvents m_events;
    std::mutex m_mutex;
    std::condition_variable m_changed;
    std::string m_pending;
    int m_readersDone{0};
    bool m_ended{false};
    std::atomic<bool> m_inputClosed{false};
    bool m_inputGiven;
    std::thread m_outputReader;
    std::thread m_errorReader;
    std::thread m_writer;
    std::thread m_watcher;
};

} // namespace workpane::platform
