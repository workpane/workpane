#pragma once

#include "Result.h"
#include "json/ObjectReader.h"

#include <TextEditor.h>

#include <array>
#include <cstddef>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace workpane::ui {

// The languages a code editor colors: the ones the library carries by name, and the ones a plugin defines with comments, strings, word lists and the constructs it names.
// A construct colors what words cannot: the headings, emphasis, code and links of Markdown, the decorators of annotated languages or the tags and attributes of markup.
class CodeLanguage final {
  public:
    [[nodiscard]] static Result<const TextEditor::Language*> named(std::string_view name);
    [[nodiscard]] static Result<std::unique_ptr<TextEditor::Language>> defined(const json::Json& definition, std::string_view context);

  private:
    enum class Construct { Markdown, Decorators, Tags };

    static constexpr std::size_t largestWordList{20000};
    static constexpr std::array<std::pair<std::string_view, Construct>, 3> constructNames{{{"markdown", Construct::Markdown}, {"decorators", Construct::Decorators}, {"tags", Construct::Tags}}};

    [[nodiscard]] static bool letter(ImWchar character);
    [[nodiscard]] static bool wordCharacter(ImWchar character);
    [[nodiscard]] static TextEditor::Iterator decorator(TextEditor::Iterator start, TextEditor::Iterator end, TextEditor::Color& color);
    [[nodiscard]] static TextEditor::Iterator tag(TextEditor::Iterator start, TextEditor::Iterator end, TextEditor::Color& color);
    [[nodiscard]] static TextEditor::Iterator construct(const std::vector<Construct>& constructs, TextEditor::Iterator start, TextEditor::Iterator end, TextEditor::Color& color);
};

} // namespace workpane::ui
