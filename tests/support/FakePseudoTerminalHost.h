#pragma once

#include "Result.h"
#include "support/TerminalRecord.h"
#include "ui/PseudoTerminal.h"
#include "ui/PseudoTerminalHost.h"
#include "ui/TerminalLaunch.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace workpane::tests {

// Starts recorded shells instead of real ones, and refuses to start any while the record names a refusal.
class FakePseudoTerminalHost final : public ui::PseudoTerminalHost {
  public:
    explicit FakePseudoTerminalHost(std::shared_ptr<TerminalRecord> record);

    void listen(std::function<void()> arrived) override;
    [[nodiscard]] Result<std::unique_ptr<ui::PseudoTerminal>> start(const ui::TerminalLaunch& launch) override;
    [[nodiscard]] std::filesystem::path shellProgram() const override;
    [[nodiscard]] std::string quotePaths(const std::vector<std::filesystem::path>& paths, const std::filesystem::path& shell) const override;

  private:
    std::shared_ptr<TerminalRecord> m_record;
};

} // namespace workpane::tests
