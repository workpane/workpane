#pragma once

#include "Result.h"
#include "ui/PseudoTerminal.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace workpane::ui {

class TerminalStream final : public PseudoTerminal {
  public:
    [[nodiscard]] Result<void> append(std::string_view bytes, std::string directory);
    [[nodiscard]] Result<void> finish(int code);
    [[nodiscard]] Result<void> acknowledge(std::size_t bytes);
    [[nodiscard]] std::string takeOutput(std::size_t limit) override;
    [[nodiscard]] std::optional<int> exitCode() const override;
    [[nodiscard]] bool write(std::string_view bytes) override;
    void resize(int columns, int rows) override;
    [[nodiscard]] std::string directory() const override;

  private:
    static constexpr std::size_t maximumPending{256U * 1024U};

    std::string m_output;
    std::string m_directory;
    std::size_t m_input{0};
    std::optional<int> m_code;
};

} // namespace workpane::ui
