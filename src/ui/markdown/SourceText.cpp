#include "ui/markdown/SourceText.h"

#include <cctype>

namespace workpane::ui::markdown {

std::vector<std::string_view> SourceText::lines(std::string_view source) {
    std::vector<std::string_view> result;
    std::size_t start = 0;

    while (start <= source.size()) {
        const std::size_t newline = source.find('\n', start);
        std::string_view line = source.substr(start, newline == std::string_view::npos ? std::string_view::npos : newline - start);

        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }

        result.push_back(line);

        if (newline == std::string_view::npos) {
            break;
        }

        start = newline + 1;
    }

    return result;
}

// A tab advances to the next multiple of four columns, which is how an editor shows the indentation a writer typed.
std::size_t SourceText::indentation(std::string_view line) {
    std::size_t columns = 0;

    for (const char character : line) {
        if (character == ' ') {
            ++columns;
        } else if (character == '\t') {
            columns += tabStop - columns % tabStop;
        } else {
            break;
        }
    }

    return columns;
}

std::string_view SourceText::outdent(std::string_view line, std::size_t columns) {
    std::size_t removed = 0;

    while (removed < columns && removed < line.size() && line[removed] == ' ') {
        ++removed;
    }

    return line.substr(removed);
}

std::string_view SourceText::trimStart(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t");
    return first == std::string_view::npos ? std::string_view() : text.substr(first);
}

std::string_view SourceText::trimEnd(std::string_view text) {
    const std::size_t last = text.find_last_not_of(" \t");
    return last == std::string_view::npos ? std::string_view() : text.substr(0, last + 1);
}

std::string_view SourceText::trim(std::string_view text) {
    return trimEnd(trimStart(text));
}

bool SourceText::blank(std::string_view text) {
    return trimStart(text).empty();
}

std::size_t SourceText::run(std::string_view text, std::size_t position) {
    std::size_t end = position;

    while (end < text.size() && text[end] == text[position]) {
        ++end;
    }

    return end - position;
}

bool SourceText::space(char character) {
    return character == ' ' || character == '\t' || character == '\n';
}

// Every byte of a multibyte character counts as part of a word, so an underscore between two accented letters stays a literal.
bool SourceText::word(char character) {
    const auto byte = static_cast<unsigned char>(character);
    return byte >= 0x80 || std::isalnum(byte) != 0;
}

bool SourceText::punctuation(char character) {
    return std::ispunct(static_cast<unsigned char>(character)) != 0;
}

} // namespace workpane::ui::markdown
