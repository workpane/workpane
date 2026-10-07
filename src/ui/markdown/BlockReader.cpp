#include "ui/markdown/BlockReader.h"

#include "ui/markdown/Parser.h"
#include "ui/markdown/SourceTextHelper.h"

#include <algorithm>

namespace workpane::ui::markdown {

BlockReader::BlockReader(std::string_view source, bool breaks) : m_lines(SourceTextHelper::lines(source)), m_breaks(breaks) {}

std::vector<Block> BlockReader::read() {
    while (m_index < m_lines.size()) {
        const std::string_view line = m_lines[m_index];
        const std::size_t indent = SourceTextHelper::indentation(line);
        const std::string_view content = SourceTextHelper::trimStart(line);

        if (content.empty()) {
            close();
            ++m_index;
            continue;
        }

        // A line indented as deep as code only continues the block before it, because indented code is not part of the dialect.
        if (indent < codeIndent && (readFence(content, indent) || readHeading(content) || readRule(content) || readItem(content, indent) || readQuote(content))) {
            continue;
        }

        if (m_open) {
            append(content);
        } else {
            open(Block{}, content);
        }

        ++m_index;
    }

    close();

    return std::move(m_blocks);
}

// A fence runs until a closing fence of the same mark at least as long, or to the end of the document when none closes it.
bool BlockReader::readFence(std::string_view content, std::size_t indent) {
    if (content.empty() || (content[0] != '`' && content[0] != '~')) {
        return false;
    }

    const std::size_t length = SourceTextHelper::run(content, 0);
    const std::string_view info = SourceTextHelper::trim(content.substr(length));

    if (length < fenceLength || (content[0] == '`' && info.find('`') != std::string_view::npos)) {
        return false;
    }

    close();
    Block block;
    block.kind = BlockKind::Code;
    block.language = std::string(info.substr(0, info.find_first_of(" \t")));
    bool first = true;
    ++m_index;

    while (m_index < m_lines.size()) {
        const std::string_view line = m_lines[m_index++];
        const std::string_view candidate = SourceTextHelper::trimStart(line);
        const std::size_t closing = SourceTextHelper::run(candidate, 0);

        if (SourceTextHelper::indentation(line) < codeIndent && closing >= length && candidate[0] == content[0] && SourceTextHelper::blank(candidate.substr(closing))) {
            break;
        }

        if (!first) {
            block.code += '\n';
        }

        block.code += SourceTextHelper::outdent(line, indent);
        first = false;
    }

    m_blocks.push_back(std::move(block));

    return true;
}

bool BlockReader::readHeading(std::string_view content) {
    const std::size_t marks = content.find_first_not_of('#');
    const std::size_t level = marks == std::string_view::npos ? content.size() : marks;

    if (level == 0 || level > deepestHeading || (marks != std::string_view::npos && !SourceTextHelper::space(content[marks]))) {
        return false;
    }

    // A closing run of marks is decoration only when a space separates it from the heading text.
    std::string_view text = marks == std::string_view::npos ? std::string_view() : SourceTextHelper::trim(content.substr(marks));
    const std::size_t closing = text.find_last_not_of('#');

    if (closing == std::string_view::npos) {
        text = {};
    } else if (closing + 1 < text.size() && SourceTextHelper::space(text[closing])) {
        text = SourceTextHelper::trimEnd(text.substr(0, closing + 1));
    }

    close();
    Block block;
    block.kind = BlockKind::Heading;
    block.level = static_cast<int>(level);
    block.spans = Parser::inlines(text);
    m_blocks.push_back(std::move(block));
    ++m_index;

    return true;
}

bool BlockReader::readRule(std::string_view content) {
    if (content[0] != '-' && content[0] != '*' && content[0] != '_') {
        return false;
    }

    std::size_t marks = 0;

    for (const char character : content) {
        if (character == content[0]) {
            ++marks;
        } else if (!SourceTextHelper::space(character)) {
            return false;
        }
    }

    if (marks < fenceLength) {
        return false;
    }

    close();
    Block block;
    block.kind = BlockKind::Rule;
    m_blocks.push_back(std::move(block));
    ++m_index;

    return true;
}

// Every two columns of indentation nest an item one level deeper, up to the deepest level a list draws.
bool BlockReader::readItem(std::string_view content, std::size_t indent) {
    Block block;
    block.level = std::min(static_cast<int>(indent / levelIndent), deepestLevel);

    if ((content[0] == '-' || content[0] == '*' || content[0] == '+') && (content.size() == 1 || SourceTextHelper::space(content[1]))) {
        block.kind = BlockKind::Bullet;
        open(std::move(block), content.substr(1));
        ++m_index;
        return true;
    }

    const std::size_t digits = content.find_first_not_of("0123456789");

    if (digits == 0 || digits == std::string_view::npos || digits > longestOrdinal) {
        return false;
    }

    if ((content[digits] != '.' && content[digits] != ')') || (digits + 1 < content.size() && !SourceTextHelper::space(content[digits + 1]))) {
        return false;
    }

    block.kind = BlockKind::Numbered;
    block.ordinal = std::stoi(std::string(content.substr(0, digits)));
    open(std::move(block), content.substr(digits + 1));
    ++m_index;

    return true;
}

// Consecutive quoted lines form one quote, and an empty quoted line breaks it into paragraphs.
bool BlockReader::readQuote(std::string_view content) {
    if (content[0] != '>') {
        return false;
    }

    std::string_view text = content.substr(1);

    if (!text.empty() && SourceTextHelper::space(text[0])) {
        text.remove_prefix(1);
    }

    if (!m_open || m_blocks.back().kind != BlockKind::Quote) {
        Block block;
        block.kind = BlockKind::Quote;
        open(std::move(block), text);
    } else if (SourceTextHelper::blank(text)) {
        m_text += '\n';
        m_break = false;
    } else {
        append(text);
    }

    ++m_index;

    return true;
}

void BlockReader::open(Block block, std::string_view text) {
    close();
    m_blocks.push_back(std::move(block));
    m_open = true;
    append(text);
}

// Two trailing spaces or a trailing backslash end a line where it was written, and any other line break reads as a space unless every break is kept.
void BlockReader::append(std::string_view text) {
    const bool slash = text.ends_with('\\');
    const bool hard = m_breaks || slash || text.ends_with("  ");
    std::string_view content = SourceTextHelper::trim(text);

    if (slash) {
        content.remove_suffix(1);
    }

    if (content.empty()) {
        m_break = hard;
        return;
    }

    if (!m_text.empty() && m_text.back() != '\n') {
        m_text += m_break ? '\n' : ' ';
    }

    m_text += content;
    m_break = hard;
}

void BlockReader::close() {
    if (!m_open) {
        return;
    }

    m_blocks.back().spans = Parser::inlines(m_text);
    m_text.clear();
    m_open = false;
    m_break = false;
}

} // namespace workpane::ui::markdown
