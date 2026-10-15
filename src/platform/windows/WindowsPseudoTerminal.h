#pragma once

#include "Result.h"
#include "platform/ShellCommand.h"
#include "platform/ShellThreads.h"
#include "ui/PseudoTerminal.h"
#include "ui/TerminalLaunch.h"

#include <windows.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::platform {

// A shell attached to a ConPTY pseudo console, read and written on threads of its own, and closed through the console when the shell ends or the reader closes it.
// Closing the terminal never waits for its shell, because the watcher closes the console, ends the shell after a grace and only then leaves the shells its host waits for.
class WindowsPseudoTerminal final : public ui::PseudoTerminal {
  public:
    [[nodiscard]] static Result<std::unique_ptr<ui::PseudoTerminal>> start(const ShellCommand& shell, const ui::TerminalLaunch& launch, const std::vector<std::string>& additions, std::function<void()> arrived, const std::shared_ptr<ShellThreads>& threads);

    ~WindowsPseudoTerminal() override;
    WindowsPseudoTerminal(const WindowsPseudoTerminal&) = delete;
    WindowsPseudoTerminal& operator=(const WindowsPseudoTerminal&) = delete;

    [[nodiscard]] std::string takeOutput(std::size_t limit) override;
    [[nodiscard]] std::optional<int> exitCode() const override;
    [[nodiscard]] bool write(std::string_view bytes) override;
    void resize(int columns, int rows) override;
    [[nodiscard]] std::string directory() const override;

  private:
    struct Session;

    static constexpr DWORD readChunk{65536};
    static constexpr std::size_t maximumPendingInput{1U << 20U};
    static constexpr std::size_t maximumPendingOutput{1U << 20U};
    static constexpr DWORD closeGraceMilliseconds{500};
    static constexpr DWORD drainMilliseconds{2000};
    static constexpr DWORD cancelRetryMilliseconds{50};

    explicit WindowsPseudoTerminal(std::shared_ptr<Session> session);
    static void read(Session& session);
    static void deliver(Session& session);
    static void watch(Session& session);

    std::shared_ptr<Session> m_session;
};

} // namespace workpane::platform
