#include "ui/components/text/Markdown.h"

#include "json/ObjectReader.h"
#include "ui/ButtonState.h"
#include "ui/Color.h"
#include "ui/Painter.h"
#include "ui/Widgets.h"
#include "ui/model/ContentFontSize.h"
#include "ui/model/EventSink.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string_view>

namespace workpane::ui {

Markdown::Markdown(NodeId id) : Component(id) {}

std::string_view Markdown::kind() const {
    return "markdown";
}

void Markdown::readProperties(json::ObjectReader& reader) {
    readText(reader, "text", m_text);
    readColor(reader, "color", m_color);
    reader.read("breaks", m_breaks, json::Presence::Optional);

    if (reader.contains("fontSize")) {
        double size = 0.0;
        reader.readNumber("fontSize", size, ContentFontSize::minimum, ContentFontSize::maximum);
        m_fontSize = static_cast<float>(size);
    }

    m_layoutValid = false;
}

Component::Restore Markdown::keep() {
    return kept(m_text, m_color, m_breaks, m_fontSize, m_layoutValid);
}

ImVec2 Markdown::measureContent(RenderContext& context, float availableWidth) {
    return layout(context, availableWidth).size;
}

void Markdown::render(RenderContext& context, const ImRect& bounds) {
    const markdown::Layout& laid = layout(context, bounds.GetWidth());
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const float scale = context.scale();

    for (const auto& shape : laid.shapes) {
        const ImVec2 minimum = bounds.Min + shape.rect.Min;
        const ImVec2 maximum = bounds.Min + ImVec2(shape.reachesEdge ? bounds.GetWidth() : shape.rect.Max.x, shape.rect.Max.y);

        switch (shape.kind) {
        case markdown::ShapeKind::Surface:
            list.AddRectFilled(minimum, maximum, Widgets::ink(context.color(ThemeColor::Terminal)), context.metric(ThemeMetric::ControlRadius));
            break;
        case markdown::ShapeKind::Bar:
            list.AddRectFilled(minimum, maximum, Widgets::ink(context.color(ThemeColor::BorderStrong)));
            break;
        case markdown::ShapeKind::Rule:
            Painter::horizontalDivider(list, minimum, maximum.x - minimum.x, context.metric(ThemeMetric::LineWidth), context.color(ThemeColor::Border));
            break;
        }
    }

    // Links are registered as items before anything is painted, so every run of a link wrapped over lines shows the hover together.
    int hovered = -1;

    for (std::size_t index = 0; index < laid.runs.size(); ++index) {
        const markdown::Run& run = laid.runs[index];

        if (run.link < 0) {
            continue;
        }

        const ImVec2 position = bounds.Min + run.offset;
        const ButtonState state = Widgets::interact(ImGui::GetID(static_cast<int>(index)), ImRect(position, position + ImVec2(run.width, run.height)));

        if (state.hovered) {
            hovered = run.link;
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }

        if (state.pressed) {
            context.emit(id(), "link", {{"url", laid.links[static_cast<std::size_t>(run.link)]}});
        }
    }

    for (const auto& run : laid.runs) {
        const ImVec2 position = bounds.Min + run.offset;
        const float glyphs = Widgets::fontSize(context, run.font);
        const float baseline = position.y + (run.height - glyphs) / 2.0F;
        const bool linkHovered = run.link >= 0 && run.link == hovered;
        const Color color = ink(context, run.ink, linkHovered);

        if (run.clip >= 0) {
            const auto& surface = laid.shapes[static_cast<std::size_t>(run.clip)];
            list.PushClipRect(bounds.Min + surface.rect.Min, bounds.Min + ImVec2(bounds.GetWidth(), surface.rect.Max.y), true);
        }

        if (run.code) {
            const float padding = codeSpanPadding * scale;
            list.AddRectFilled(ImVec2(position.x - padding, baseline - scale), ImVec2(position.x + run.width + padding, baseline + glyphs + 2.0F * scale), Widgets::ink(context.color(ThemeColor::Terminal)), codeSpanRadius * scale);
        }

        Widgets::text(context, list, run.font, ImVec2(position.x, baseline), color, run.text);

        if (linkHovered) {
            const float underline = std::round(baseline + glyphs + scale);
            list.AddLine(ImVec2(position.x, underline), ImVec2(position.x + run.width, underline), Widgets::ink(color), std::max(1.0F, std::round(scale)));
        }

        if (run.clip >= 0) {
            list.PopClipRect();
        }
    }
}

// A layout that already fits a narrower width than it was built for is kept, because breaking the same words again places them where they are.
const markdown::Layout& Markdown::layout(RenderContext& context, float limit) {
    const std::string& source = context.text(m_text);

    if (source != m_source || m_breaks != m_parsedBreaks) {
        m_source = source;
        m_parsedBreaks = m_breaks;
        m_blocks = markdown::Parser::parse(m_source, m_breaks);
        m_layoutValid = false;
    }

    const bool reusable = m_layoutValid && m_layoutScale == context.scale() && m_layout.size.x <= limit && limit <= m_layout.limit;

    if (!reusable) {
        FontRole body = context.font(ThemeFont::Interface);

        if (m_fontSize.has_value()) {
            body.size = *m_fontSize;
        }

        m_layout = markdown::LayoutBuilder(context, body, limit).build(m_blocks);
        m_layoutScale = context.scale();
        m_layoutValid = true;
    }

    return m_layout;
}

Color Markdown::ink(RenderContext& context, markdown::Ink role, bool hovered) const {
    if (Widgets::disabled()) {
        return context.color(ThemeColor::TextMuted);
    }

    switch (role) {
    case markdown::Ink::Muted:
        return context.color(ThemeColor::TextMuted);
    case markdown::Ink::Link:
        return context.color(hovered ? ThemeColor::AccentText : ThemeColor::Accent);
    case markdown::Ink::Text:
        break;
    }

    return context.color(m_color.value_or(ThemeColor::Text));
}

} // namespace workpane::ui
