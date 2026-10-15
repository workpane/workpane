#pragma once

#include "Result.h"
#include "process/Process.h"
#include "process/ProcessEvents.h"
#include "process/ProcessLaunch.h"

#include <memory>
#include <string_view>
#include <thread>

namespace workpane::ui {
class PseudoTerminalHost;
}

namespace workpane::process {

// Runs an explicit program behind a native terminal with owned wake notifications and bounded stream delivery.
class TerminalProcess final : public Process {
  public:
    [[nodiscard]] static Result<std::unique_ptr<Process>> start(ui::PseudoTerminalHost& host, const ProcessLaunch& launch, int columns, int rows, ProcessEvents events);
    ~TerminalProcess() override;
    TerminalProcess(const TerminalProcess&) = delete;
    TerminalProcess& operator=(const TerminalProcess&) = delete;

    [[nodiscard]] bool write(std::string_view bytes) override;
    [[nodiscard]] bool resize(int columns, int rows) override;
    void stop() override;

  private:
    struct Session;

    explicit TerminalProcess(std::shared_ptr<Session> session);
    static void run(Session& session);

    std::shared_ptr<Session> m_session;
    std::thread m_thread;
};

} // namespace workpane::process
