#include "ui/markdown/CodeHighlighter.h"

#include "ui/components/views/CodeLanguage.h"

#include <imgui_internal.h>

#include <array>
#include <string>
#include <utility>

namespace workpane::ui::markdown {

std::vector<std::vector<Run>> CodeHighlighter::lines(std::string_view code, std::string_view language) {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 8> aliases{{{"js", "javascript"}, {"ts", "typescript"}, {"sh", "shell"}, {"bash", "shell"}, {"zsh", "shell"}, {"py", "python"}, {"c++", "cpp"}, {"c#", "cs"}}};

    for (const auto& [alias, name] : aliases) {
        if (language == alias) {
            language = name;
            break;
        }
    }

    CodeHighlighter reader;
    const auto named = CodeLanguage::named(language);
    reader.SetLanguage(named.hasValue() ? named.value() : nullptr);
    reader.SetText(code);
    reader.colorizer.update(reader.config, reader.document);
    std::vector<std::vector<Run>> lines;

    for (const auto& line : reader.document) {
        std::vector<Run> runs;

        for (const auto& glyph : line) {
            const auto color = tone(glyph.color);

            if (runs.empty() || runs.back().color != color) {
                Run run;
                run.color = color;
                runs.push_back(std::move(run));
            }

            std::array<char, 5> text{};
            const int bytes = ImTextCharToUtf8(text.data(), static_cast<unsigned int>(glyph.codepoint));
            runs.back().text.append(text.data(), static_cast<std::size_t>(bytes));
        }

        lines.push_back(std::move(runs));
    }

    return lines;
}

std::optional<ThemeColor> CodeHighlighter::tone(TextEditor::Color color) {
    switch (color) {
    case TextEditor::Color::keyword:
    case TextEditor::Color::preprocessor:
        return ThemeColor::AccentText;
    case TextEditor::Color::declaration:
    case TextEditor::Color::knownIdentifier:
        return ThemeColor::InformationText;
    case TextEditor::Color::number:
        return ThemeColor::WarningText;
    case TextEditor::Color::string:
        return ThemeColor::SuccessText;
    case TextEditor::Color::comment:
        return ThemeColor::TextMuted;
    default:
        return std::nullopt;
    }
}

} // namespace workpane::ui::markdown
