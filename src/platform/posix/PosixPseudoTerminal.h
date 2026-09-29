#pragma once

#include "Result.h"
#include "platform/ShellCommand.h"
#include "platform/ShellThreads.h"
#include "ui/PseudoTerminal.h"
#include "ui/TerminalLaunch.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::platform {

// A shell started on a pseudo-terminal the product opens, whose output and input move on one thread that waits on the terminal and on a pipe that wakes it.
// Closing the terminal never waits for its shell, because that thread hangs up the terminal itself, ends the shell after a grace and only then leaves the shells its host waits for.
class PosixPseudoTerminal final : public ui::PseudoTerminal {
  public:
    [[nodiscard]] static Result<std::unique_ptr<ui::PseudoTerminal>> start(const ShellCommand& shell, const ui::TerminalLaunch& launch, const std::vector<std::string>& environment, std::function<void()> arrived, const std::shared_ptr<ShellThreads>& threads);

    ~PosixPseudoTerminal() override;
    PosixPseudoTerminal(const PosixPseudoTerminal&) = delete;
    PosixPseudoTerminal& operator=(const PosixPseudoTerminal&) = delete;

    [[nodiscard]] std::string takeOutput(std::size_t limit) override;
    [[nodiscard]] std::optional<int> exitCode() const override;
    [[nodiscard]] bool write(std::string_view bytes) override;
    void resize(int columns, int rows) override;
    [[nodiscard]] std::string directory() const override;

  private:
    struct Session;

    static constexpr std::size_t readChunk{65536};
    static constexpr std::size_t maximumPendingInput{1U << 20U};
    static constexpr std::size_t maximumPendingOutput{1U << 20U};
    static constexpr int hangupGraceMilliseconds{500};
    static constexpr int hangupStepMilliseconds{10};
    static constexpr int reapStepMilliseconds{100};

    explicit PosixPseudoTerminal(std::shared_ptr<Session> session);
    static void run(Session& session);
    static void deliverInput(Session& session);
    [[nodiscard]] static bool readOutput(Session& session, std::string& buffer);
    [[nodiscard]] static int reap(Session& session);
    static void wake(const Session& session);

    std::shared_ptr<Session> m_session;
};

} // namespace workpane::platform
