#include "ui/markdown/LayoutBuilder.h"

#include "ui/FontScope.h"
#include "ui/WidgetHelper.h"
#include "ui/markdown/CodeHighlighter.h"
#include "ui/markdown/Run.h"
#include "ui/model/RenderContext.h"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <tuple>
#include <utility>

namespace workpane::ui::markdown {

LayoutBuilder::LayoutBuilder(const RenderContext& context, FontRole body, float limit, ImageSizes images) : m_context(context), m_body(body), m_unit(WidgetHelper::fontSize(context, body)), m_images(std::move(images)) {
    m_layout.limit = limit;
}

Layout LayoutBuilder::build(const std::vector<Block>& blocks) {
    float top = 0.0F;
    const Block* previous = nullptr;

    for (const Block& block : blocks) {
        if (previous != nullptr) {
            top += gap(*previous, block);
        }

        const bool listed = block.kind == BlockKind::Bullet || block.kind == BlockKind::Numbered;
        const float listIndent = static_cast<float>(std::max(0, block.listDepth - (listed ? 1 : 0))) * std::round(m_unit * levelIndent);
        const float start = top;
        m_indent = listIndent + static_cast<float>(block.quoteDepth) * std::round(m_unit * quoteIndent);

        switch (block.kind) {
        case BlockKind::Heading:
            top = flow(block.spans, m_indent, top, heading(block.level), Ink::Text);
            break;
        case BlockKind::Paragraph:
            top = flow(block.spans, m_indent, top, m_body, Ink::Text);
            break;
        case BlockKind::Bullet:
        case BlockKind::Numbered:
            top = item(block, top);
            break;
        case BlockKind::Quote:
            top = flow(block.spans, m_indent, top, m_body, Ink::Muted);
            break;
        case BlockKind::Code:
            top = code(block, top);
            break;
        case BlockKind::Rule:
            top = rule(top);
            break;
        case BlockKind::Table:
            top = table(block, top);
            break;
        }

        for (int depth = 0; depth < block.quoteDepth; ++depth) {
            const float x = listIndent + static_cast<float>(depth) * std::round(m_unit * quoteIndent);
            m_layout.shapes.push_back({ShapeKind::Bar, ImRect(x, start, x + std::round(quoteBar * m_context.scale()), top), false});
        }

        previous = &block;
    }

    m_layout.size = ImVec2(std::min(std::ceil(m_widest), m_layout.limit), std::ceil(top));
    indexText();

    return std::move(m_layout);
}

// Items of one list sit closer together than paragraphs, and a heading keeps more room above it than below.
float LayoutBuilder::gap(const Block& previous, const Block& block) const {
    const bool listed = (previous.kind == BlockKind::Bullet || previous.kind == BlockKind::Numbered) && (block.kind == BlockKind::Bullet || block.kind == BlockKind::Numbered);

    if (block.kind == BlockKind::Heading) {
        return std::round(m_unit * headingGap);
    }

    return std::round(m_unit * (listed ? itemGap : blockGap));
}

FontRole LayoutBuilder::heading(int level) const {
    const auto deepest = static_cast<int>(headingScales.size());
    const float factor = level >= 1 && level <= deepest ? headingScales[static_cast<std::size_t>(level - 1)] : 1.0F;
    return {FontFace::SemiBold, std::round(m_body.size * factor)};
}

FontRole LayoutBuilder::face(const Span& span, FontRole role) {
    if (span.code) {
        return {FontFace::Monospace, role.size};
    }

    const bool strong = span.strong || role.face == FontFace::SemiBold;

    if (strong) {
        return {span.emphasis ? FontFace::SemiBoldItalic : FontFace::SemiBold, role.size};
    }

    return {span.emphasis ? FontFace::Italic : FontFace::Regular, role.size};
}

// Words of one span on one line join a single run, so a link underlines and a code span paints its surface without gaps.
float LayoutBuilder::flow(const std::vector<Span>& spans, float left, float top, FontRole role, Ink ink) {
    m_left = left;
    m_x = left;
    m_top = top;
    m_line = WidgetHelper::lineHeight(m_context, role);
    m_textLine = m_line;
    m_space = false;
    m_lineStart = m_layout.runs.size();

    for (const Span& span : spans) {
        const FontRole font = face(span, role);
        const std::string_view text = span.text;
        int link = -1;

        if (!span.link.empty()) {
            link = static_cast<int>(m_layout.links.size());
            m_layout.links.push_back(span.link);
        }

        m_current = -1;

        if (const auto found = m_images.find(span.image); !span.image.empty() && found != m_images.end()) {
            image(span, found->second, font, link);
            continue;
        }

        if (span.code) {
            const std::size_t first = m_layout.runs.size();
            place(text, font, link >= 0 ? Ink::Link : ink, true, link);

            for (std::size_t index = first; index < m_layout.runs.size(); ++index) {
                m_layout.runs[index].strike = span.strike;
            }

            continue;
        }

        std::size_t position = 0;

        while (position < text.size()) {
            const char character = text[position];

            if (character == '\n') {
                breakLine();
                ++position;
                continue;
            }

            if (character == ' ' || character == '\t') {
                m_space = m_x > m_left;
                ++position;
                continue;
            }

            const std::size_t end = std::min(text.find_first_of(" \t\n", position), text.size());
            const std::size_t first = m_layout.runs.size();
            place(text.substr(position, end - position), font, link >= 0 ? Ink::Link : ink, span.code, link);

            for (std::size_t index = first; index < m_layout.runs.size(); ++index) {
                m_layout.runs[index].strike = span.strike;
            }

            position = end;
        }
    }

    m_current = -1;
    finishLine();

    return m_top + m_line;
}

// A marker hangs in its own column before the text of the item, so wrapped lines align with the first one and not with the marker.
float LayoutBuilder::item(const Block& block, float top) {
    const float left = m_indent;
    const float column = std::round(m_unit * markerColumn);
    const std::string marker = block.checked.has_value() ? (*block.checked ? "\xE2\x98\x91" : "\xE2\x98\x90") : block.kind == BlockKind::Bullet ? "\xE2\x80\xA2" : std::to_string(block.ordinal) + ".";
    const float width = WidgetHelper::textSize(m_context, m_body, marker).x;
    const float x = block.kind == BlockKind::Bullet ? left + (column - width) / 2.0F : left + column - width - std::round(m_unit * markerSpacing);

    Run run;
    run.offset = ImVec2(std::max(left, x), top);
    run.width = width;
    run.height = WidgetHelper::lineHeight(m_context, m_body);
    run.font = m_body;
    run.text = marker;
    m_layout.runs.push_back(std::move(run));

    return flow(block.spans, left + column, top, m_body, Ink::Text);
}

// Code keeps the lines it was written with, so a line wider than the document is clipped by its surface instead of being wrapped.
float LayoutBuilder::code(const Block& block, float top) {
    const FontRole font{FontFace::Monospace, m_body.size};
    const float line = WidgetHelper::lineHeight(m_context, font);
    const float padding = std::round(m_unit * codePadding);
    const int clip = static_cast<int>(m_layout.shapes.size());
    m_layout.shapes.push_back({ShapeKind::Surface, ImRect(m_indent, top, m_layout.limit, top), true});
    float y = top + padding;

    for (auto& tokens : CodeHighlighter::lines(block.code, block.language)) {
        float x = m_indent + padding;

        for (auto& run : tokens) {
            std::string expanded;

            for (const char character : run.text) {
                expanded.append(character == '\t' ? static_cast<std::size_t>(tabWidth) : 1U, character == '\t' ? ' ' : character);
            }

            run.text = std::move(expanded);
            run.width = WidgetHelper::textSize(m_context, font, run.text).x;
            run.offset = ImVec2(x, y);
            run.height = line;
            run.font = font;
            run.clip = clip;
            x += run.width;
            m_widest = std::max(m_widest, padding + x);
            m_layout.runs.push_back(std::move(run));
        }

        y += line;
    }

    const float bottom = y + padding;
    m_layout.shapes[static_cast<std::size_t>(clip)].rect.Max.y = bottom;

    return bottom;
}

float LayoutBuilder::rule(float top) {
    const float middle = std::round(top + m_unit / 2.0F);
    m_layout.shapes.push_back({ShapeKind::Rule, ImRect(m_indent, middle, m_layout.limit, middle + ruleHeight), true});

    return top + m_unit;
}

float LayoutBuilder::table(const Block& block, float top) {
    if (block.rows.empty() || block.rows.front().empty()) {
        return top;
    }

    const float padding = std::round(m_unit * codePadding);
    const std::size_t columns = block.rows.front().size();
    const float available = std::max(0.0F, m_layout.limit - m_indent);
    std::vector<float> widths(columns, m_unit * markerColumn);

    for (const auto& row : block.rows) {
        for (std::size_t column = 0; column < std::min(columns, row.size()); ++column) {
            Block content;
            content.spans = row[column].spans;
            const auto natural = LayoutBuilder(m_context, m_body, available, m_images).build({content});
            widths[column] = std::max(widths[column], natural.size.x + padding * 2.0F);
        }
    }

    float total = 0.0F;

    for (float width : widths) {
        total += width;
    }

    const float factor = total > available ? available / total : 1.0F;

    for (float& width : widths) {
        width *= factor;
    }

    total *= factor;
    float y = top;

    for (const auto& row : block.rows) {
        float x = m_indent;
        float bottom = y;
        std::vector<std::size_t> clips;

        for (std::size_t column = 0; column < std::min(columns, row.size()); ++column) {
            const float inset = std::min(padding, widths[column] / 4.0F);
            const float inner = std::max(1.0F, widths[column] - inset * 2.0F);
            Block content;
            content.spans = row[column].spans;
            Layout cell = LayoutBuilder(m_context, m_body, inner, m_images).build({content});
            const float shift = row[column].align == TextAlign::End ? inner - cell.size.x : row[column].align == TextAlign::Center ? (inner - cell.size.x) / 2.0F : 0.0F;
            const ImVec2 origin(x + inset + std::max(0.0F, shift), y + padding);
            const int links = static_cast<int>(m_layout.links.size());
            m_layout.links.insert(m_layout.links.end(), cell.links.begin(), cell.links.end());
            const int clip = static_cast<int>(m_layout.shapes.size());
            clips.push_back(m_layout.shapes.size());
            m_layout.shapes.push_back({ShapeKind::Clip, ImRect(x, y, x + widths[column], y), false});

            for (auto& run : cell.runs) {
                run.offset += origin;
                run.lineTop += origin.y;
                run.link = run.link >= 0 ? run.link + links : -1;
                run.clip = clip;
                m_layout.runs.push_back(std::move(run));
            }

            bottom = std::max(bottom, y + padding * 2.0F + cell.size.y);
            x += widths[column];
        }

        for (const std::size_t clip : clips) {
            m_layout.shapes[clip].rect.Max.y = bottom;
        }

        m_layout.shapes.push_back({ShapeKind::Rule, ImRect(m_indent, bottom, m_indent + total, bottom + ruleHeight), false});
        y = bottom;
    }

    float x = m_indent;

    for (float width : widths) {
        m_layout.shapes.push_back({ShapeKind::Bar, ImRect(x, top, x + ruleHeight, y), false});
        x += width;
    }

    m_layout.shapes.push_back({ShapeKind::Bar, ImRect(m_indent + total, top, m_indent + total + ruleHeight, y), false});
    m_layout.shapes.push_back({ShapeKind::Rule, ImRect(m_indent, top, m_indent + total, top + ruleHeight), false});
    m_widest = std::max(m_widest, m_indent + total);

    return y;
}

void LayoutBuilder::indexText() {
    const Run* previous = nullptr;

    for (Run& run : m_layout.runs) {
        if (previous != nullptr) {
            const float top = run.image.empty() ? run.offset.y : run.lineTop;
            const float previousTop = previous->image.empty() ? previous->offset.y : previous->lineTop;

            if (top > previousTop + previous->height / 2.0F) {
                m_layout.text += '\n';
            } else if (top < previousTop || run.offset.x < previous->offset.x) {
                m_layout.text += '\t';
            } else if (run.offset.x > previous->offset.x + previous->width + m_context.scale()) {
                m_layout.text += ' ';
            }
        }

        run.textOffset = m_layout.text.size();
        m_layout.text += run.text;
        previous = &run;
    }
}

// A word wider than a whole line is broken between characters, because no break between words can make it fit.
void LayoutBuilder::place(std::string_view text, FontRole font, Ink ink, bool code, int link) {
    float space = m_space ? WidgetHelper::textSize(m_context, font, " ").x : 0.0F;
    const float width = WidgetHelper::textSize(m_context, font, text).x;
    const float available = m_layout.limit - m_left;

    if (m_x > m_left && m_x + space + width > m_layout.limit) {
        breakLine();
        space = 0.0F;
    }

    if (m_x > m_left || width <= available) {
        append(text, space, width, font, ink, code, link);
        return;
    }

    if (code) {
        const char* cursor = text.data();
        const char* end = cursor + text.size();
        const FontScope scope(m_context.fonts(), font);
        ImFont* face = m_context.fonts().face(font.face);

        while (cursor < end) {
            const char* next = cursor;
            std::ignore = face->CalcTextSizeA(WidgetHelper::fontSize(m_context, font), available, 0.0F, cursor, end, &next);

            if (next == cursor) {
                unsigned int point = 0;
                next += ImTextCharFromUtf8(&point, cursor, end);
            }

            const std::string_view piece(cursor, static_cast<std::size_t>(next - cursor));
            append(piece, 0.0F, WidgetHelper::textSize(m_context, font, piece).x, font, ink, true, link);
            cursor = next;

            if (cursor < end) {
                breakLine();
            }
        }

        return;
    }

    const auto pieces = WidgetHelper::wrapLines(m_context, font, text, available);

    for (std::size_t index = 0; index < pieces.size(); ++index) {
        if (index > 0) {
            breakLine();
        }

        append(pieces[index], 0.0F, WidgetHelper::textSize(m_context, font, pieces[index]).x, font, ink, code, link);
    }
}

void LayoutBuilder::append(std::string_view text, float space, float width, FontRole font, Ink ink, bool code, int link) {
    if (m_current >= 0) {
        Run& run = m_layout.runs[static_cast<std::size_t>(m_current)];
        run.text.append(space > 0.0F ? " " : "").append(text);
        run.width = m_x + space + width - run.offset.x;
    } else {
        Run run;
        run.offset = ImVec2(m_x + space, m_top);
        run.width = width;
        run.height = m_line;
        run.font = font;
        run.ink = ink;
        run.code = code;
        run.link = link;
        run.text = std::string(text);
        m_layout.runs.push_back(std::move(run));
        m_current = static_cast<int>(m_layout.runs.size()) - 1;
    }

    m_x += space + width;
    m_space = false;
    m_widest = std::max(m_widest, m_x);
}

void LayoutBuilder::breakLine() {
    finishLine();
    m_x = m_left;
    m_top += m_line;
    m_line = m_textLine;
    m_space = false;
    m_current = -1;
    m_lineStart = m_layout.runs.size();
}

void LayoutBuilder::finishLine() {
    for (std::size_t index = m_lineStart; index < m_layout.runs.size(); ++index) {
        Run& run = m_layout.runs[index];

        if (run.image.empty()) {
            run.height = m_line;
        } else {
            run.offset.y += (m_line - run.height) / 2.0F;
        }
    }
}

void LayoutBuilder::image(const Span& span, ImVec2 size, FontRole font, int link) {
    const float available = std::max(1.0F, m_layout.limit - m_left);
    const float factor = std::min(1.0F, available / size.x);
    size *= factor;
    float space = m_space ? WidgetHelper::textSize(m_context, font, " ").x : 0.0F;

    if (m_x > m_left && m_x + space + size.x > m_layout.limit) {
        breakLine();
        space = 0.0F;
    }

    Run run;
    run.offset = ImVec2(m_x + space, m_top);
    run.width = size.x;
    run.height = size.y;
    run.font = font;
    run.link = link;
    run.text = span.text;
    run.image = span.image;
    run.lineTop = m_top;
    m_layout.runs.push_back(std::move(run));
    m_line = std::max(m_line, size.y);
    m_x += space + size.x;
    m_space = false;
    m_widest = std::max(m_widest, m_x);
}

} // namespace workpane::ui::markdown
