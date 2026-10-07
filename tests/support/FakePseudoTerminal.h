#pragma once

#include "support/TerminalProcessRecord.h"
#include "ui/PseudoTerminal.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace workpane::tests {

// A pseudo-terminal whose shell is the test, which writes its output and reads what the component sent it.
class FakePseudoTerminal final : public ui::PseudoTerminal {
  public:
    explicit FakePseudoTerminal(std::shared_ptr<TerminalProcessRecord> process);
    ~FakePseudoTerminal() override;

    FakePseudoTerminal(const FakePseudoTerminal&) = delete;
    FakePseudoTerminal& operator=(const FakePseudoTerminal&) = delete;

    [[nodiscard]] std::string takeOutput(std::size_t limit) override;
    [[nodiscard]] std::optional<int> exitCode() const override;
    [[nodiscard]] bool write(std::string_view bytes) override;
    void resize(int columns, int rows) override;
    [[nodiscard]] std::string directory() const override;

  private:
    std::shared_ptr<TerminalProcessRecord> m_process;
};

} // namespace workpane::tests
