#pragma once

#include "Result.h"
#include "ui/PseudoTerminal.h"
#include "ui/TerminalLaunch.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace workpane::ui {

// Starts the shell of the reader behind a pseudo-terminal, so components never name a process or a platform.
// The host is told once, before any terminal starts, how to wake the product whenever a terminal wrote something or ended.
class PseudoTerminalHost {
  public:
    virtual ~PseudoTerminalHost() = default;

    virtual void listen(std::function<void()> arrived) = 0;
    [[nodiscard]] virtual Result<std::unique_ptr<PseudoTerminal>> start(const TerminalLaunch& launch) = 0;
    [[nodiscard]] virtual std::filesystem::path shellProgram() const = 0;
    [[nodiscard]] virtual std::string quotePaths(const std::vector<std::filesystem::path>& paths, const std::filesystem::path& shell) const = 0;
};

} // namespace workpane::ui
