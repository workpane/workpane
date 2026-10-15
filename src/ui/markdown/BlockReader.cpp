#include "ui/markdown/BlockReader.h"

extern "C" {
#include <entity.h>
}

#include <charconv>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

namespace workpane::ui::markdown {

BlockReader::BlockReader(std::string_view source, bool breaks) : m_source(source), m_breaks(breaks) {}

std::vector<Block> BlockReader::read() {
    if (m_source.size() > std::numeric_limits<MD_SIZE>::max()) {
        return {};
    }

    MD_PARSER parser{};
    parser.flags = MD_DIALECT_GITHUB | MD_FLAG_NOHTML;
    parser.enter_block = &enterBlock;
    parser.leave_block = &leaveBlock;
    parser.enter_span = &enterSpan;
    parser.leave_span = &leaveSpan;
    parser.text = &text;
    const int parsed = md_parse(m_source.data(), static_cast<MD_SIZE>(m_source.size()), &parser, this);

    if (parsed != 0) {
        return {};
    }

    return std::move(m_blocks);
}

int BlockReader::enterBlock(MD_BLOCKTYPE type, void* detail, void* context) {
    auto& reader = *static_cast<BlockReader*>(context);

    switch (type) {
    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
        reader.m_lists.push_back({type == MD_BLOCK_OL, type == MD_BLOCK_OL ? static_cast<int>(static_cast<MD_BLOCK_OL_DETAIL*>(detail)->start) : 0, false, std::nullopt});
        break;
    case MD_BLOCK_LI: {
        reader.m_current = -1;
        auto& list = reader.m_lists.back();
        const auto& item = *static_cast<MD_BLOCK_LI_DETAIL*>(detail);
        list.pending = true;
        list.checked = item.is_task != 0 ? std::optional<bool>(item.task_mark != ' ') : std::nullopt;
        break;
    }
    case MD_BLOCK_QUOTE:
        ++reader.m_quotes;
        break;
    case MD_BLOCK_H:
        reader.open(BlockKind::Heading);
        reader.m_blocks.back().level = static_cast<int>(static_cast<MD_BLOCK_H_DETAIL*>(detail)->level);
        break;
    case MD_BLOCK_HR:
        reader.open(BlockKind::Rule);
        break;
    case MD_BLOCK_CODE:
        reader.open(BlockKind::Code);
        reader.m_code = true;
        reader.m_blocks.back().language = attribute(static_cast<MD_BLOCK_CODE_DETAIL*>(detail)->lang);
        break;
    case MD_BLOCK_P:
        reader.open(reader.m_quotes > 0 ? BlockKind::Quote : BlockKind::Paragraph);
        break;
    case MD_BLOCK_TABLE:
        reader.open(BlockKind::Table);
        reader.m_table = reader.m_current;
        break;
    case MD_BLOCK_TR:
        reader.m_blocks[static_cast<std::size_t>(reader.m_table)].rows.emplace_back();
        break;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD: {
        auto& cells = reader.m_blocks[static_cast<std::size_t>(reader.m_table)].rows.back();
        const MD_ALIGN align = static_cast<MD_BLOCK_TD_DETAIL*>(detail)->align;
        cells.push_back({{}, align == MD_ALIGN_RIGHT ? TextAlign::End : align == MD_ALIGN_CENTER ? TextAlign::Center : TextAlign::Start});
        reader.m_cell = static_cast<int>(cells.size()) - 1;
        reader.m_styles.back().strong = type == MD_BLOCK_TH;
        break;
    }
    default:
        break;
    }

    return 0;
}

int BlockReader::leaveBlock(MD_BLOCKTYPE type, void*, void* context) {
    auto& reader = *static_cast<BlockReader*>(context);

    switch (type) {
    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
        reader.m_lists.pop_back();
        break;
    case MD_BLOCK_LI:
        ++reader.m_lists.back().ordinal;
        reader.m_lists.back().pending = false;
        reader.m_current = -1;
        break;
    case MD_BLOCK_QUOTE:
        --reader.m_quotes;
        break;
    case MD_BLOCK_CODE:
        if (!reader.m_blocks.back().code.empty() && reader.m_blocks.back().code.back() == '\n') {
            reader.m_blocks.back().code.pop_back();
        }

        reader.m_code = false;
        reader.m_current = -1;
        break;
    case MD_BLOCK_H:
    case MD_BLOCK_HR:
    case MD_BLOCK_P:
        reader.m_current = -1;
        break;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
        reader.m_cell = -1;
        reader.m_styles.back().strong = false;
        break;
    case MD_BLOCK_TABLE:
        reader.m_table = -1;
        reader.m_current = -1;
        break;
    default:
        break;
    }

    return 0;
}

int BlockReader::enterSpan(MD_SPANTYPE type, void* detail, void* context) {
    auto& reader = *static_cast<BlockReader*>(context);
    Span style = reader.m_styles.back();

    switch (type) {
    case MD_SPAN_EM:
        style.emphasis = true;
        break;
    case MD_SPAN_STRONG:
        style.strong = true;
        break;
    case MD_SPAN_CODE:
        style.code = true;
        break;
    case MD_SPAN_DEL:
        style.strike = true;
        break;
    case MD_SPAN_A:
        style.link = attribute(static_cast<MD_SPAN_A_DETAIL*>(detail)->href);
        break;
    case MD_SPAN_IMG:
        style.image = attribute(static_cast<MD_SPAN_IMG_DETAIL*>(detail)->src);

        if (reader.m_imageDepth++ == 0) {
            reader.m_image = style;
        }

        break;
    default:
        break;
    }

    reader.m_styles.push_back(std::move(style));

    return 0;
}

int BlockReader::leaveSpan(MD_SPANTYPE type, void*, void* context) {
    auto& reader = *static_cast<BlockReader*>(context);

    if (type == MD_SPAN_IMG && --reader.m_imageDepth == 0) {
        reader.append(std::move(reader.m_image.text));
        reader.m_image = {};
    }

    reader.m_styles.pop_back();

    return 0;
}

int BlockReader::text(MD_TEXTTYPE type, const MD_CHAR* value, MD_SIZE size, void* context) {
    auto& reader = *static_cast<BlockReader*>(context);
    const std::string_view supplied(value, size);

    if (reader.m_code) {
        reader.m_blocks.back().code.append(supplied);
        return 0;
    }

    if (type == MD_TEXT_BR || type == MD_TEXT_SOFTBR) {
        reader.append(type == MD_TEXT_BR || reader.m_breaks ? "\n" : " ");
    } else if (type == MD_TEXT_NULLCHAR) {
        reader.append(encoded(0xFFFDU));
    } else {
        reader.append(type == MD_TEXT_ENTITY ? entity(supplied) : std::string(supplied));
    }

    return 0;
}

void BlockReader::open(BlockKind kind) {
    Block block;
    block.kind = kind;
    block.quoteDepth = m_quotes;
    block.listDepth = static_cast<int>(m_lists.size());

    if (!m_lists.empty()) {
        auto& list = m_lists.back();
        block.level = static_cast<int>(m_lists.size()) - 1;

        if (list.pending && (kind == BlockKind::Paragraph || kind == BlockKind::Quote)) {
            block.kind = list.ordered ? BlockKind::Numbered : BlockKind::Bullet;
            block.ordinal = list.ordinal;
            block.checked = list.checked;
            list.pending = false;
        }
    }

    m_blocks.push_back(std::move(block));
    m_current = static_cast<int>(m_blocks.size()) - 1;
}

void BlockReader::append(std::string value) {
    if (m_imageDepth > 0) {
        m_image.text += value;
        return;
    }

    if (m_current < 0 && m_cell < 0 && !m_lists.empty()) {
        open(m_quotes > 0 ? BlockKind::Quote : BlockKind::Paragraph);
    }

    if (m_current < 0 && m_cell < 0) {
        return;
    }

    auto& spans = m_cell >= 0 ? m_blocks[static_cast<std::size_t>(m_table)].rows.back()[static_cast<std::size_t>(m_cell)].spans : m_blocks[static_cast<std::size_t>(m_current)].spans;
    Span span = m_styles.back();
    span.text = std::move(value);

    if (!spans.empty() && spans.back().sameStyle(span)) {
        spans.back().text += span.text;
        return;
    }

    spans.push_back(std::move(span));
}

std::string BlockReader::attribute(const MD_ATTRIBUTE& value) {
    std::string decoded;

    for (std::size_t index = 0; value.substr_offsets != nullptr && value.substr_offsets[index] < value.size; ++index) {
        const auto begin = value.substr_offsets[index];
        const std::string_view part(value.text + begin, value.substr_offsets[index + 1] - begin);
        decoded += value.substr_types[index] == MD_TEXT_ENTITY ? entity(part) : std::string(part);
    }

    return decoded;
}

std::string BlockReader::entity(std::string_view value) {
    if (value.size() > 3 && value[1] == '#') {
        const bool hex = value[2] == 'x' || value[2] == 'X';
        unsigned int point = 0;
        const auto parsed = std::from_chars(value.data() + (hex ? 3 : 2), value.data() + value.size() - 1, point, hex ? 16 : 10);

        if (parsed.ec == std::errc() && parsed.ptr == value.data() + value.size() - 1) {
            return encoded(point);
        }
    }

    if (const ENTITY* found = entity_lookup(value.data(), value.size()); found != nullptr) {
        return encoded(found->codepoints[0]) + (found->codepoints[1] != 0 ? encoded(found->codepoints[1]) : "");
    }

    return std::string(value);
}

std::string BlockReader::encoded(unsigned int point) {
    if (point == 0 || point > 0x10FFFFU || (point >= 0xD800U && point <= 0xDFFFU)) {
        point = 0xFFFDU;
    }

    std::string value;

    if (point < 0x80U) {
        value.push_back(static_cast<char>(point));
    } else if (point < 0x800U) {
        value.push_back(static_cast<char>(0xC0U | (point >> 6U)));
        value.push_back(static_cast<char>(0x80U | (point & 0x3FU)));
    } else if (point < 0x10000U) {
        value.push_back(static_cast<char>(0xE0U | (point >> 12U)));
        value.push_back(static_cast<char>(0x80U | ((point >> 6U) & 0x3FU)));
        value.push_back(static_cast<char>(0x80U | (point & 0x3FU)));
    } else {
        value.push_back(static_cast<char>(0xF0U | (point >> 18U)));
        value.push_back(static_cast<char>(0x80U | ((point >> 12U) & 0x3FU)));
        value.push_back(static_cast<char>(0x80U | ((point >> 6U) & 0x3FU)));
        value.push_back(static_cast<char>(0x80U | (point & 0x3FU)));
    }

    return value;
}

} // namespace workpane::ui::markdown
