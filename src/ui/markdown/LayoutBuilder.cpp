#include "ui/markdown/LayoutBuilder.h"

#include "ui/WidgetHelper.h"
#include "ui/markdown/Run.h"
#include "ui/model/RenderContext.h"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

namespace workpane::ui::markdown {

LayoutBuilder::LayoutBuilder(const RenderContext& context, FontRole body, float limit) : m_context(context), m_body(body), m_unit(WidgetHelper::fontSize(context, body)) {
    m_layout.limit = limit;
}

Layout LayoutBuilder::build(const std::vector<Block>& blocks) {
    float top = 0.0F;
    const Block* previous = nullptr;

    for (const Block& block : blocks) {
        if (previous != nullptr) {
            top += gap(*previous, block);
        }

        switch (block.kind) {
        case BlockKind::Heading:
            top = flow(block.spans, 0.0F, top, heading(block.level), Ink::Text);
            break;
        case BlockKind::Paragraph:
            top = flow(block.spans, 0.0F, top, m_body, Ink::Text);
            break;
        case BlockKind::Bullet:
        case BlockKind::Numbered:
            top = item(block, top);
            break;
        case BlockKind::Quote:
            top = quote(block, top);
            break;
        case BlockKind::Code:
            top = code(block, top);
            break;
        case BlockKind::Rule:
            top = rule(top);
            break;
        }

        previous = &block;
    }

    m_layout.size = ImVec2(std::min(std::ceil(m_widest), m_layout.limit), std::ceil(top));

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
    m_space = false;

    for (const Span& span : spans) {
        const FontRole font = face(span, role);
        const std::string_view text = span.text;
        int link = -1;

        if (!span.link.empty()) {
            link = static_cast<int>(m_layout.links.size());
            m_layout.links.push_back(span.link);
        }

        m_current = -1;
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
            place(text.substr(position, end - position), font, link >= 0 ? Ink::Link : ink, span.code, link);
            position = end;
        }
    }

    m_current = -1;

    return m_top + m_line;
}

// A marker hangs in its own column before the text of the item, so wrapped lines align with the first one and not with the marker.
float LayoutBuilder::item(const Block& block, float top) {
    const float left = static_cast<float>(block.level) * std::round(m_unit * levelIndent);
    const float column = std::round(m_unit * markerColumn);
    const std::string marker = block.kind == BlockKind::Bullet ? "\xE2\x80\xA2" : std::to_string(block.ordinal) + ".";
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

float LayoutBuilder::quote(const Block& block, float top) {
    const float bottom = flow(block.spans, std::round(m_unit * quoteIndent), top, m_body, Ink::Muted);
    m_layout.shapes.push_back({ShapeKind::Bar, ImRect(0.0F, top, std::round(quoteBar * m_context.scale()), bottom), false});

    return bottom;
}

// Code keeps the lines it was written with, so a line wider than the document is clipped by its surface instead of being wrapped.
float LayoutBuilder::code(const Block& block, float top) {
    const FontRole font{FontFace::Monospace, m_body.size};
    const float line = WidgetHelper::lineHeight(m_context, font);
    const float padding = std::round(m_unit * codePadding);
    const int clip = static_cast<int>(m_layout.shapes.size());
    m_layout.shapes.push_back({ShapeKind::Surface, ImRect(0.0F, top, 0.0F, top), true});
    float y = top + padding;
    std::size_t start = 0;

    while (start <= block.code.size()) {
        const std::size_t newline = std::min(block.code.find('\n', start), block.code.size());
        std::string text;

        for (const char character : std::string_view(block.code).substr(start, newline - start)) {
            text.append(character == '\t' ? static_cast<std::size_t>(tabWidth) : 1U, character == '\t' ? ' ' : character);
        }

        if (!text.empty()) {
            Run run;
            run.width = WidgetHelper::textSize(m_context, font, text).x;
            run.offset = ImVec2(padding, y);
            run.height = line;
            run.font = font;
            run.clip = clip;
            run.text = std::move(text);
            m_widest = std::max(m_widest, padding * 2.0F + run.width);
            m_layout.runs.push_back(std::move(run));
        }

        y += line;
        start = newline + 1;
    }

    const float bottom = y + padding;
    m_layout.shapes[static_cast<std::size_t>(clip)].rect.Max.y = bottom;

    return bottom;
}

float LayoutBuilder::rule(float top) {
    const float middle = std::round(top + m_unit / 2.0F);
    m_layout.shapes.push_back({ShapeKind::Rule, ImRect(0.0F, middle, 0.0F, middle + ruleHeight), true});

    return top + m_unit;
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
    m_x = m_left;
    m_top += m_line;
    m_space = false;
    m_current = -1;
}

} // namespace workpane::ui::markdown
