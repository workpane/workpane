#pragma once

#include "ui/Color.h"

#include <TextEditor.h>

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace workpane::ui {

// The colors of Dark Modern, the default theme of Visual Studio Code, which a code editor wears until its plugin gives a scheme of its own.
// The surfaces it leaves out, such as the background and the pair of brackets at the caret, come from the theme of the product.
class DarkModern final {
  public:
    [[nodiscard]] static std::vector<std::pair<TextEditor::Color, Color>> colors();
    [[nodiscard]] static Color currentLine();
    [[nodiscard]] static Color occurrence();

  private:
    static constexpr std::array<std::pair<TextEditor::Color, std::uint32_t>, 19> roles{{{TextEditor::Color::text, 0xCCCCCC}, {TextEditor::Color::keyword, 0x569CD6}, {TextEditor::Color::declaration, 0x4EC9B0}, {TextEditor::Color::number, 0xB5CEA8}, {TextEditor::Color::string, 0xCE9178}, {TextEditor::Color::punctuation, 0xCCCCCC}, {TextEditor::Color::preprocessor, 0xC586C0}, {TextEditor::Color::identifier, 0x9CDCFE}, {TextEditor::Color::knownIdentifier, 0xDCDCAA}, {TextEditor::Color::comment, 0x6A9955}, {TextEditor::Color::cursor, 0xAEAFAD}, {TextEditor::Color::selection, 0x264F78}, {TextEditor::Color::whitespace, 0x404040}, {TextEditor::Color::matchingBracketActive, 0x707070}, {TextEditor::Color::matchingBracketLevel1, 0xFFD700}, {TextEditor::Color::matchingBracketLevel2, 0xDA70D6}, {TextEditor::Color::matchingBracketLevel3, 0x179FFF}, {TextEditor::Color::lineNumber, 0x6E7681}, {TextEditor::Color::currentLineNumber, 0xCCCCCC}}};
    static constexpr std::uint32_t currentLineColor{0x282828};
    static constexpr std::uint32_t occurrenceColor{0x575757};

    [[nodiscard]] static Color color(std::uint32_t rgb);
};

} // namespace workpane::ui
