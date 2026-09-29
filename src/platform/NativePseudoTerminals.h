#pragma once

#include "Result.h"
#include "platform/ShellCommand.h"
#include "platform/ShellThreads.h"
#include "ui/PseudoTerminal.h"
#include "ui/PseudoTerminalHost.h"
#include "ui/TerminalLaunch.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::platform {

// Starts the shell of the reader behind the pseudo-terminal of the running platform, with one environment that describes this terminal on every platform.
// A closed terminal ends its shell in the background, and the host waits once for every shell still ending when it is destroyed with the product.
class NativePseudoTerminals final : public ui::PseudoTerminalHost {
  public:
    NativePseudoTerminals() = default;
    ~NativePseudoTerminals() override;
    NativePseudoTerminals(const NativePseudoTerminals&) = delete;
    NativePseudoTerminals& operator=(const NativePseudoTerminals&) = delete;

    void listen(std::function<void()> arrived) override;
    [[nodiscard]] Result<std::unique_ptr<ui::PseudoTerminal>> start(const ui::TerminalLaunch& launch) override;
    [[nodiscard]] std::filesystem::path shellProgram() const override;
    [[nodiscard]] std::string quotePaths(const std::vector<std::filesystem::path>& paths, const std::filesystem::path& shell) const override;

  private:
    [[nodiscard]] static std::filesystem::path program();
    [[nodiscard]] static ShellCommand command(const std::filesystem::path& program);
    [[nodiscard]] static bool posixShell(std::string_view name);

    std::function<void()> m_arrived;
    std::shared_ptr<ShellThreads> m_threads{std::make_shared<ShellThreads>()};
};

} // namespace workpane::platform
