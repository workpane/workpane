#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace workpane::ui::markdown {

// The lines, indentation and character classes of CommonMark, shared by the block and inline readers.
class SourceTextHelper final {
  public:
    [[nodiscard]] static std::vector<std::string_view> lines(std::string_view source);
    [[nodiscard]] static std::size_t indentation(std::string_view line);
    [[nodiscard]] static std::string_view outdent(std::string_view line, std::size_t columns);
    [[nodiscard]] static std::string_view trimStart(std::string_view text);
    [[nodiscard]] static std::string_view trimEnd(std::string_view text);
    [[nodiscard]] static std::string_view trim(std::string_view text);
    [[nodiscard]] static bool blank(std::string_view text);
    [[nodiscard]] static std::size_t run(std::string_view text, std::size_t position);
    [[nodiscard]] static bool space(char character);
    [[nodiscard]] static bool word(char character);
    [[nodiscard]] static bool punctuation(char character);

  private:
    static constexpr std::size_t tabStop{4};
};

} // namespace workpane::ui::markdown
