#include "ui/components/text/Label.h"

#include "ui/FontScope.h"

#include <imgui_stdlib.h>

#include <cmath>

namespace workpane::ui {

Label::Label(NodeId id) : Component(id) {}

std::string_view Label::kind() const {
    return "label";
}

void Label::readProperties(json::ObjectReader& reader) {
    readText(reader, "text", m_text);
    reader.readChoice("style", m_style, {{"body", LabelStyle::Body}, {"strong", LabelStyle::Strong}, {"muted", LabelStyle::Muted}, {"caption", LabelStyle::Caption}, {"title", LabelStyle::Title}, {"heading", LabelStyle::Heading}, {"monospace", LabelStyle::Monospace}}, json::Presence::Optional);
    readColor(reader, "color", m_color);
    reader.readChoice("textAlign", m_textAlign, {{"start", TextAlign::Start}, {"center", TextAlign::Center}, {"end", TextAlign::End}}, json::Presence::Optional);
    reader.read("wrap", m_wrap, json::Presence::Optional).read("selectable", m_selectable, json::Presence::Optional);

    if (reader.contains("size")) {
        double size = 0.0;
        reader.readNumber("size", size, 8.0, 64.0);
        m_size = static_cast<float>(size);
    }
}

Component::Restore Label::keep() {
    return kept(m_text, m_style, m_color, m_textAlign, m_wrap, m_selectable, m_size);
}

ImVec2 Label::measureContent(RenderContext& context, float availableWidth) {
    const FontRole font = role(context);
    const std::string& text = context.text(m_text);

    // A selectable text is a field that scrolls once its content passes its height, so its measure takes whole points.
    if (m_selectable) {
        const ImVec2 size = WidgetHelper::textSize(context, font, text, m_wrap ? availableWidth : -1.0F);
        return {std::ceil(size.x), std::ceil(size.y)};
    }

    if (m_wrap) {
        return WidgetHelper::paragraphSize(context, font, text, availableWidth);
    }

    return {std::ceil(WidgetHelper::textSize(context, font, text).x), WidgetHelper::lineHeight(context, font)};
}

void Label::render(RenderContext& context, const ImRect& bounds) {
    const FontRole font = role(context);
    const std::string& text = context.text(m_text);
    ImDrawList& list = *ImGui::GetWindowDrawList();

    if (!m_selectable) {
        if (m_wrap) {
            WidgetHelper::paragraph(context, list, font, bounds, ink(context), text, m_textAlign);
            return;
        }

        WidgetHelper::alignedText(context, list, font, bounds, ink(context), text, m_textAlign);
        return;
    }

    // Selectable text is a read only field without a frame, which is what lets the reader select a part of it and copy it.
    if (m_selectionBuffer != text) {
        m_selectionBuffer = text;
    }

    const FontScope scope(context.fonts(), font);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0F, 0.0F));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, ink(context).vector());
    ImGui::SetCursorScreenPos(bounds.Min);
    ImGui::InputTextMultiline("##label", &m_selectionBuffer, bounds.GetSize(), ImGuiInputTextFlags_ReadOnly | (m_wrap ? ImGuiInputTextFlags_WordWrap : ImGuiInputTextFlags_None) | ImGuiInputTextFlags_NoHorizontalScroll);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

FontRole Label::role(RenderContext& context) const {
    FontRole font = context.font(ThemeFont::Interface);

    switch (m_style) {
    case LabelStyle::Body:
    case LabelStyle::Muted:
        break;
    case LabelStyle::Strong:
        font.face = FontFace::SemiBold;
        break;
    case LabelStyle::Caption:
        font = context.font(ThemeFont::Caption);
        break;
    case LabelStyle::Title:
        font = context.font(ThemeFont::PageTitle);
        break;
    case LabelStyle::Heading:
        font = context.font(ThemeFont::Heading);
        break;
    case LabelStyle::Monospace:
        font = context.font(ThemeFont::Monospace);
        break;
    }

    if (m_size.has_value()) {
        font.size = *m_size;
    }

    return font;
}

Color Label::ink(RenderContext& context) const {
    if (WidgetHelper::disabled()) {
        return context.color(ThemeColor::TextMuted);
    }

    if (m_color.has_value()) {
        return context.color(*m_color);
    }

    return context.color(m_style == LabelStyle::Muted || m_style == LabelStyle::Caption ? ThemeColor::TextMuted : ThemeColor::Text);
}

} // namespace workpane::ui
