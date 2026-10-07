#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace workpane::ui {

// One shell running behind a pseudo-terminal, read and written on a thread of its own and drained by the component that shows it.
// Its exit code appears only once everything it wrote has been taken, so the last output always reaches the reader first.
// Output is taken in slices of a bounded size, and reading from the shell pauses while too much is waiting, so a flood holds back the program rather than the memory of the product.
class PseudoTerminal {
  public:
    virtual ~PseudoTerminal() = default;

    [[nodiscard]] virtual std::string takeOutput(std::size_t limit) = 0;
    [[nodiscard]] virtual std::optional<int> exitCode() const = 0;
    [[nodiscard]] virtual bool write(std::string_view bytes) = 0;
    virtual void resize(int columns, int rows) = 0;
    [[nodiscard]] virtual std::string directory() const = 0;
};

} // namespace workpane::ui
