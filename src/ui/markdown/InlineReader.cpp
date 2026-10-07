#include "ui/markdown/InlineReader.h"

#include "ui/markdown/SourceTextHelper.h"

#include <algorithm>
#include <set>
#include <utility>

namespace workpane::ui::markdown {

std::vector<Span> InlineReader::read(std::string_view text) {
    parse(text, Style{}, 0);

    return std::move(m_spans);
}

// Markup nested past a bound is kept as text, and a mark whose closing was already looked for in vain is not looked for again, so no paragraph costs more than a linear pass per mark.
void InlineReader::parse(std::string_view text, const Style& style, std::size_t depth) {
    if (depth > maximumNesting) {
        push(text, style, false);
        return;
    }

    std::string literal;
    std::size_t position = 0;
    std::set<std::size_t> unclosedTicks;
    std::set<std::pair<char, std::size_t>> unclosedEmphasis;

    while (position < text.size()) {
        const char character = text[position];

        if (character == '\\' && position + 1 < text.size() && SourceTextHelper::punctuation(text[position + 1])) {
            literal += text[position + 1];
            position += 2;
            continue;
        }

        // A code span ends at the first run of exactly as many backticks as opened it, and nothing inside it is read as markup.
        if (character == '`') {
            const std::size_t length = SourceTextHelper::run(text, position);
            const std::size_t closing = unclosedTicks.contains(length) ? std::string_view::npos : closingTicks(text, position + length, length);

            if (closing == std::string_view::npos) {
                unclosedTicks.insert(length);
                literal.append(text.substr(position, length));
                position += length;
                continue;
            }

            push(literal, style, false);
            literal.clear();
            push(codeContent(text.substr(position + length, closing - position - length)), style, true);
            position = closing + length;
            continue;
        }

        // An image shows its description where it stands, and a link is never opened inside the label of another one.
        const bool image = character == '!' && position + 1 < text.size() && text[position + 1] == '[';

        if (image || (character == '[' && style.link.empty()) || (character == '<' && style.link.empty())) {
            const auto parts = character == '<' ? autolink(text, position) : link(text, image ? position + 1 : position);

            if (parts.has_value()) {
                push(literal, style, false);
                literal.clear();
                Style linked = style;

                if (!image) {
                    linked.link = std::string(parts->destination);
                }

                if (character == '<') {
                    push(parts->label, linked, false);
                } else {
                    parse(parts->label, linked, depth + 1);
                }

                position = parts->end;
                continue;
            }
        }

        // One mark is emphasis, two are strong and three are both, closed by a run of the same length.
        if (character == '*' || character == '_') {
            const std::size_t length = SourceTextHelper::run(text, position);
            const bool opens = length <= strongestRun && !unclosedEmphasis.contains({character, length}) && opensEmphasis(text, position, length);
            const std::size_t closing = opens ? closingEmphasis(text, position + length, character, length) : std::string_view::npos;

            if (closing == std::string_view::npos) {
                unclosedEmphasis.insert(opens ? std::pair{character, length} : std::pair{'\0', std::size_t{0}});
                literal.append(text.substr(position, length));
                position += length;
                continue;
            }

            push(literal, style, false);
            literal.clear();
            Style nested = style;
            nested.emphasis = nested.emphasis || length != 2;
            nested.strong = nested.strong || length >= 2;
            parse(text.substr(position + length, closing - position - length), nested, depth + 1);
            position = closing + length;
            continue;
        }

        literal += character;
        ++position;
    }

    push(literal, style, false);
}

void InlineReader::push(std::string_view text, const Style& style, bool code) {
    if (text.empty()) {
        return;
    }

    Span span{std::string(text), style.strong, style.emphasis, code, style.link};

    if (!m_spans.empty() && m_spans.back().sameStyle(span)) {
        m_spans.back().text += span.text;
        return;
    }

    m_spans.push_back(std::move(span));
}

// A line break inside a code span is a space, and one space on both sides is padding the writer added around backticks.
std::string InlineReader::codeContent(std::string_view text) {
    std::string content(text);
    std::replace(content.begin(), content.end(), '\n', ' ');

    if (content.size() >= 2 && content.front() == ' ' && content.back() == ' ' && content.find_first_not_of(' ') != std::string::npos) {
        return content.substr(1, content.size() - 2);
    }

    return content;
}

std::size_t InlineReader::skipCode(std::string_view text, std::size_t position) {
    const std::size_t length = SourceTextHelper::run(text, position);
    const std::size_t closing = closingTicks(text, position + length, length);
    return closing == std::string_view::npos ? position + length : closing + length;
}

std::size_t InlineReader::closingTicks(std::string_view text, std::size_t from, std::size_t length) {
    std::size_t position = text.find('`', from);

    while (position != std::string_view::npos) {
        const std::size_t found = SourceTextHelper::run(text, position);

        if (found == length) {
            return position;
        }

        position = text.find('`', position + found);
    }

    return std::string_view::npos;
}

bool InlineReader::opensEmphasis(std::string_view text, std::size_t position, std::size_t length) {
    const std::size_t after = position + length;

    if (after >= text.size() || SourceTextHelper::space(text[after])) {
        return false;
    }

    return text[position] != '_' || position == 0 || !SourceTextHelper::word(text[position - 1]);
}

std::size_t InlineReader::closingEmphasis(std::string_view text, std::size_t from, char mark, std::size_t length) {
    std::size_t position = from;

    while (position < text.size()) {
        const char character = text[position];

        if (character == '\\') {
            position += 2;
            continue;
        }

        if (character == '`') {
            position = skipCode(text, position);
            continue;
        }

        if (character != mark) {
            ++position;
            continue;
        }

        const std::size_t found = SourceTextHelper::run(text, position);
        const std::size_t after = position + found;
        const bool closes = found == length && position > from && !SourceTextHelper::space(text[position - 1]) && (mark != '_' || after >= text.size() || !SourceTextHelper::word(text[after]));

        if (closes) {
            return position;
        }

        position = after;
    }

    return std::string_view::npos;
}

// A link is a bracketed label directly followed by a parenthesized destination, whose optional title is dropped.
std::optional<InlineReader::LinkParts> InlineReader::link(std::string_view text, std::size_t open) {
    // The label closes at the bracket that balances the opening one, skipping escapes and code spans.
    std::size_t position = open;
    std::size_t labelEnd = std::string_view::npos;
    int depth = 0;

    while (position < std::min(text.size(), open + longestLabel) && labelEnd == std::string_view::npos) {
        const char character = text[position];

        if (character == '\\') {
            position += 2;
            continue;
        }

        if (character == '`') {
            position = skipCode(text, position);
            continue;
        }

        if (character == '[') {
            ++depth;
        } else if (character == ']' && --depth == 0) {
            labelEnd = position;
        }

        ++position;
    }

    if (labelEnd == std::string_view::npos || labelEnd + 1 >= text.size() || text[labelEnd + 1] != '(') {
        return std::nullopt;
    }

    // The destination follows at once in parentheses, which may nest inside it.
    std::size_t destinationEnd = std::string_view::npos;
    position = labelEnd + 1;
    depth = 0;

    while (position < std::min(text.size(), labelEnd + longestDestination) && destinationEnd == std::string_view::npos) {
        const char character = text[position];

        if (character == '\\') {
            position += 2;
            continue;
        }

        if (character == '(') {
            ++depth;
        } else if (character == ')' && --depth == 0) {
            destinationEnd = position;
        }

        ++position;
    }

    if (destinationEnd == std::string_view::npos) {
        return std::nullopt;
    }

    // A destination in angle brackets may hold spaces, and any other one ends at its first space before an optional title.
    std::string_view destination = SourceTextHelper::trim(text.substr(labelEnd + 2, destinationEnd - labelEnd - 2));

    if (destination.starts_with('<') && destination.find('>') != std::string_view::npos) {
        destination = destination.substr(1, destination.find('>') - 1);
    } else {
        destination = destination.substr(0, destination.find_first_of(" \t\n"));
    }

    return LinkParts{text.substr(open + 1, labelEnd - open - 1), destination, destinationEnd + 1};
}

// An autolink ends at its first closing angle bracket, and the search for it stops at the first space or opening bracket, which no address holds.
std::optional<InlineReader::LinkParts> InlineReader::autolink(std::string_view text, std::size_t open) {
    const std::size_t close = text.find_first_of("> \t\n<", open + 1);

    if (close == std::string_view::npos || text[close] != '>') {
        return std::nullopt;
    }

    const std::string_view inside = text.substr(open + 1, close - open - 1);
    const bool addressed = inside.starts_with("http://") || inside.starts_with("https://") || inside.starts_with("mailto:");

    if (!addressed || inside.find_first_of(" \t\n<") != std::string_view::npos) {
        return std::nullopt;
    }

    return LinkParts{inside, inside, close + 1};
}

} // namespace workpane::ui::markdown
