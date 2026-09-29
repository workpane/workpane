#pragma once

#include "Result.h"
#include "process/Process.h"
#include "process/ProcessEnd.h"
#include "process/ProcessEvents.h"
#include "process/ProcessLaunch.h"
#include "process/ProcessStream.h"

#include <sys/types.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace workpane::platform {

// A program started with fork and exec in a process group of its own, whose pipes are served by one thread that also ends the whole group when asked.
class PosixProcess final : public process::Process {
  public:
    [[nodiscard]] static Result<std::unique_ptr<process::Process>> start(const process::ProcessLaunch& launch, process::ProcessEvents events);

    ~PosixProcess() override;
    PosixProcess(const PosixProcess&) = delete;
    PosixProcess& operator=(const PosixProcess&) = delete;

    [[nodiscard]] bool write(std::string_view bytes) override;
    void stop() override;

  private:
    static constexpr std::size_t readChunk{65536};
    static constexpr std::size_t maximumPendingInput{64U << 20U};
    static constexpr int stopGraceMilliseconds{2000};
    static constexpr int reapStepMilliseconds{50};

    PosixProcess(pid_t child, std::array<int, 3> pipes, std::array<int, 2> wake, process::ProcessEvents events, std::optional<std::string> input);
    static void closeAll(const std::array<std::array<int, 2>, 4>& pipes);
    static void release(int& descriptor);
    void run();
    void deliverInput();
    void closeInput();
    void readStream(int& descriptor, process::ProcessStream stream, std::string& buffer);
    [[nodiscard]] process::ProcessEnd reap();
    void wake() const;

    pid_t m_child;
    int m_input;
    int m_output;
    int m_error;
    std::array<int, 2> m_wake;
    process::ProcessEvents m_events;
    std::mutex m_mutex;
    std::string m_pending;
    bool m_inputClosed;
    bool m_inputGiven;
    bool m_ended{false};
    std::atomic<bool> m_stopping{false};
    std::thread m_thread;
};

} // namespace workpane::platform
