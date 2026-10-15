#pragma once

#include <string>
#include <string_view>

namespace workpane::process {

// Turns the bytes a program writes into UTF-8 text, holding back a character split between two reads and writing U+FFFD for bytes that are not UTF-8.
class ProcessText final {
  public:
    [[nodiscard]] std::string take(std::string_view bytes);
    [[nodiscard]] std::string finish();

  private:
    static constexpr std::string_view replacement{"\xEF\xBF\xBD"};

    std::string m_pending;
};

} // namespace workpane::process
