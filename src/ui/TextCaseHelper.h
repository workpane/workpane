#pragma once

#include <string>
#include <string_view>

namespace workpane::ui {

// Upper case applied to UTF-8 text covering the Latin letters every supported language writes, so an accented heading keeps its accents.
class TextCaseHelper final {
  public:
    [[nodiscard]] static std::string upper(std::string_view text);
    [[nodiscard]] static std::string lower(std::string_view text);
    [[nodiscard]] static bool alphabeticalLess(std::string_view left, std::string_view right);

  private:
    struct FoldRange;

    [[nodiscard]] static unsigned int upperCodepoint(unsigned int codepoint);
    [[nodiscard]] static unsigned int lowerCodepoint(unsigned int codepoint);
    static void append(std::string& text, unsigned int codepoint);
    template <typename Mapping> static std::string map(std::string_view text, Mapping mapping);
    [[nodiscard]] static std::string fold(std::string_view text);
};

} // namespace workpane::ui
