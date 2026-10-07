#include "ui/WidgetHelper.h"

#include "ui/FontScope.h"
#include "ui/Painter.h"
#include "ui/TextCaseHelper.h"
#include "ui/model/RenderContext.h"
#include "ui/theme/Theme.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <tuple>

namespace workpane::ui {

bool WidgetHelper::disabled() {
    return (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
}

// Drawing through the style alpha keeps a control inside a disabled scope faded exactly like every native ImGui widget.
ImU32 WidgetHelper::ink(Color color) {
    return ImGui::GetColorU32(color.vector());
}

float WidgetHelper::controlHeight(const RenderContext& context) {
    return context.metric(ThemeMetric::ControlHeight);
}

ImFont* WidgetHelper::font(const RenderContext& context, FontRole role) {
    return context.fonts().face(role.face);
}

float WidgetHelper::fontSize(const RenderContext& context, FontRole role) {
    return context.fonts().size(role.face, role.size) * context.scale();
}

// Text is measured and drawn with its face pushed, because ImGui rasterizes a face at the density of the framebuffer only while it is the current one.
ImVec2 WidgetHelper::textSize(const RenderContext& context, FontRole role, std::string_view text, float wrapWidth) {
    if (text.empty()) {
        return {0.0F, fontSize(context, role)};
    }

    const FontScope scope(context.fonts(), role);
    return font(context, role)->CalcTextSizeA(fontSize(context, role), FLT_MAX, std::max(0.0F, wrapWidth), text.data(), text.data() + text.size());
}

void WidgetHelper::text(const RenderContext& context, ImDrawList& list, FontRole role, ImVec2 position, Color color, std::string_view text, float wrapWidth) {
    if (text.empty()) {
        return;
    }

    const FontScope scope(context.fonts(), role);
    list.AddText(font(context, role), fontSize(context, role), ImFloor(position), ink(color), text.data(), text.data() + text.size(), std::max(0.0F, wrapWidth));
}

void WidgetHelper::alignedText(const RenderContext& context, ImDrawList& list, FontRole role, const ImRect& bounds, Color color, std::string_view text, TextAlign align) {
    const std::string fitted = elided(context, role, text, bounds.GetWidth());
    const ImVec2 size = textSize(context, role, fitted);
    float x = bounds.Min.x;

    if (align == TextAlign::Center) {
        x = bounds.Min.x + (bounds.GetWidth() - size.x) / 2.0F;
    } else if (align == TextAlign::End) {
        x = bounds.Max.x - size.x;
    }

    WidgetHelper::text(context, list, role, ImVec2(x, bounds.Min.y + (bounds.GetHeight() - size.y) / 2.0F), color, fitted);
}

// A single line shows its text up to the first line break and keeps as many whole characters as fit, and an ellipsis marks whatever it leaves out.
// A rectangle built from the measure of its own text can come out a rounding error narrower than that measure, so a text fits within a hundredth of a pixel.
std::string WidgetHelper::elided(const RenderContext& context, FontRole role, std::string_view text, float width) {
    const std::string_view line = text.substr(0, text.find_first_of("\r\n"));

    if (line.size() == text.size() && textSize(context, role, line).x <= width + fitTolerance) {
        return std::string(line);
    }

    // One measurement finds the cut, where the next character would pass the room the ellipsis leaves.
    const float available = width - textSize(context, role, ellipsis).x;
    const FontScope scope(context.fonts(), role);
    const char* cut = line.data();
    std::ignore = font(context, role)->CalcTextSizeA(fontSize(context, role), std::max(0.0F, available), 0.0F, line.data(), line.data() + line.size(), &cut);

    return std::string(line.data(), cut) + ellipsis;
}

float WidgetHelper::lineHeight(const RenderContext& context, FontRole role) {
    return std::round(fontSize(context, role) * 1.3F);
}

// A paragraph breaks between words and at every newline, and a single word wider than the line is the only thing broken inside itself.
std::vector<std::string_view> WidgetHelper::wrapLines(const RenderContext& context, FontRole role, std::string_view text, float width) {
    std::vector<std::string_view> lines;

    // A paragraph broken earlier in the frame, as a measure breaks it before a draw, is read back from its offsets.
    if (const auto* known = context.paragraphs().find(text, role, width); known != nullptr) {
        lines.reserve(known->size());

        for (const ParagraphCache::Line& line : *known) {
            lines.push_back(text.substr(line.offset, line.length));
        }

        return lines;
    }

    ImFont* face = font(context, role);
    const float size = fontSize(context, role);
    std::size_t start = 0;

    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        const std::string_view paragraph = text.substr(start, newline == std::string_view::npos ? std::string_view::npos : newline - start);
        const char* cursor = paragraph.data();
        const char* end = paragraph.data() + paragraph.size();

        if (cursor == end) {
            lines.emplace_back();
        }

        while (cursor < end) {
            const char* wrap = width > 0.0F ? face->CalcWordWrapPosition(size, cursor, end, width) : end;

            // A word wider than the whole line still advances, because a position that never moves would never end.
            if (wrap == cursor) {
                unsigned int codepoint = 0;
                wrap = cursor + ImTextCharFromUtf8(&codepoint, cursor, end);
            }

            std::string_view line(cursor, static_cast<std::size_t>(wrap - cursor));

            while (!line.empty() && line.back() == ' ') {
                line.remove_suffix(1);
            }

            lines.push_back(line);
            cursor = wrap;

            while (cursor < end && *cursor == ' ') {
                ++cursor;
            }
        }

        if (newline == std::string_view::npos) {
            break;
        }

        start = newline + 1;
    }

    std::vector<ParagraphCache::Line> offsets;
    offsets.reserve(lines.size());

    for (const std::string_view line : lines) {
        offsets.push_back({line.empty() ? 0 : static_cast<std::size_t>(line.data() - text.data()), line.size()});
    }

    context.paragraphs().keep(text, role, width, std::move(offsets));

    return lines;
}

ImVec2 WidgetHelper::paragraphSize(const RenderContext& context, FontRole role, std::string_view text, float width) {
    const auto lines = wrapLines(context, role, text, width);
    float widest = 0.0F;

    for (const auto line : lines) {
        widest = std::max(widest, textSize(context, role, line).x);
    }

    return {std::ceil(widest), lineHeight(context, role) * static_cast<float>(std::max<std::size_t>(1, lines.size()))};
}

void WidgetHelper::paragraph(const RenderContext& context, ImDrawList& list, FontRole role, const ImRect& bounds, Color color, std::string_view text, TextAlign align) {
    const float height = lineHeight(context, role);
    const float glyphs = fontSize(context, role);
    float y = bounds.Min.y;

    for (const auto line : wrapLines(context, role, text, bounds.GetWidth())) {
        const float width = textSize(context, role, line).x;
        float x = bounds.Min.x;

        if (align == TextAlign::Center) {
            x += (bounds.GetWidth() - width) / 2.0F;
        } else if (align == TextAlign::End) {
            x += bounds.GetWidth() - width;
        }

        WidgetHelper::text(context, list, role, ImVec2(x, y + (height - glyphs) / 2.0F), color, line);
        y += height;
    }
}

// A control registers its rectangle as an ImGui item, so hovering, activation and keyboard navigation behave like every native one.
// The navigation ring of ImGui, in the focus color of the theme, shows where the keyboard rests, since these controls draw no focus border of their own.
ButtonState WidgetHelper::interact(ImGuiID id, const ImRect& bounds, ImGuiButtonFlags flags) {
    ImGui::SetCursorScreenPos(bounds.Min);
    ImGui::ItemSize(bounds.GetSize());

    if (!ImGui::ItemAdd(bounds, id)) {
        return {};
    }

    ButtonState state;
    state.pressed = ImGui::ButtonBehavior(bounds, id, &state.hovered, &state.held, flags);
    ImGui::RenderNavCursor(bounds, id);

    return state;
}

ImVec2 WidgetHelper::buttonSize(const RenderContext& context, std::string_view label, std::optional<Icon> icon, ButtonVariant variant) {
    const FontRole role = context.font(ThemeFont::Interface);

    if (variant == ButtonVariant::Icon) {
        const float side = context.metric(ThemeMetric::CompactButtonSize);
        return {side, side};
    }

    if (variant == ButtonVariant::Link) {
        return {textSize(context, role, label).x, controlHeight(context)};
    }

    const float padding = variant == ButtonVariant::Toolbar ? toolbarHorizontalPadding * context.scale() : context.metric(ThemeMetric::ControlHorizontalPadding);
    const float iconWidth = icon.has_value() ? context.metric(ThemeMetric::SmallIconSize) : 0.0F;
    const float spacing = icon.has_value() && !label.empty() ? buttonContentSpacing * context.scale() : 0.0F;
    return {std::ceil(padding * 2.0F + iconWidth + spacing + textSize(context, role, label).x), controlHeight(context)};
}

bool WidgetHelper::button(const RenderContext& context, std::string_view id, const ImRect& bounds, std::string_view label, std::optional<Icon> icon, ButtonVariant variant, bool checked) {
    const ImGuiID identity = ImGui::GetID(id.data(), id.data() + id.size());
    const ButtonState state = interact(identity, bounds);
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const bool inactive = disabled();
    const float radius = context.metric(ThemeMetric::ControlRadius);

    Color fill = context.color(ThemeColor::Raised).withAlpha(0.0F);
    Color ink = context.color(state.hovered ? ThemeColor::Text : ThemeColor::TextMuted);
    Color labelInk = context.color(ThemeColor::Text);

    switch (variant) {
    case ButtonVariant::Default:
        fill = context.color(state.held ? ThemeColor::BorderStrong : state.hovered ? ThemeColor::Hover : ThemeColor::Raised);
        break;
    case ButtonVariant::Primary:
        fill = context.color(state.held ? ThemeColor::AccentStrong : state.hovered ? ThemeColor::AccentHover : ThemeColor::Accent);
        ink = context.color(ThemeColor::OnAccent);
        labelInk = ink;
        break;
    case ButtonVariant::Destructive:
        fill = context.color(state.held ? ThemeColor::DangerStrong : state.hovered ? ThemeColor::DangerHover : ThemeColor::Danger);
        ink = context.color(ThemeColor::OnDanger);
        labelInk = ink;
        break;
    case ButtonVariant::Toolbar:
    case ButtonVariant::Icon:
        if (state.held) {
            fill = context.color(ThemeColor::BorderStrong);
        } else if (state.hovered) {
            fill = context.color(ThemeColor::Hover);
        }
        break;
    case ButtonVariant::Link:
        labelInk = context.color(state.hovered ? ThemeColor::AccentText : ThemeColor::Accent);
        break;
    }

    if (checked) {
        fill = context.color(state.hovered ? ThemeColor::AccentHover : ThemeColor::Accent);
        ink = context.color(ThemeColor::OnAccent);
        labelInk = ink;
    }

    // A disabled button over a toned fill keeps the ink of that fill and fades with the disabled scope, and any other mutes its ink.
    const bool toned = variant == ButtonVariant::Primary || variant == ButtonVariant::Destructive || checked;

    if (inactive && !toned) {
        ink = context.color(ThemeColor::TextMuted);
        labelInk = ink;
    }

    if (fill.alpha > 0) {
        list.AddRectFilled(bounds.Min, bounds.Max, WidgetHelper::ink(fill), radius);
    }

    const FontRole role = context.font(ThemeFont::Interface);
    const float iconSize = context.metric(ThemeMetric::SmallIconSize);

    if (variant == ButtonVariant::Icon && icon.has_value()) {
        IconCatalog::draw(context.fonts(), list, *icon, ImFloor(ImVec2(bounds.Min.x + (bounds.GetWidth() - iconSize) / 2.0F, bounds.Min.y + (bounds.GetHeight() - iconSize) / 2.0F)), iconSize, ink);
        return state.pressed;
    }

    const ImVec2 labelSize = textSize(context, role, label);
    const float spacing = icon.has_value() && !label.empty() ? buttonContentSpacing * context.scale() : 0.0F;
    const float contentWidth = (icon.has_value() ? iconSize : 0.0F) + spacing + labelSize.x;
    float x = bounds.Min.x + (bounds.GetWidth() - contentWidth) / 2.0F;

    if (icon.has_value()) {
        IconCatalog::draw(context.fonts(), list, *icon, ImFloor(ImVec2(x, bounds.Min.y + (bounds.GetHeight() - iconSize) / 2.0F)), iconSize, ink);
        x += iconSize + spacing;
    }

    const ImVec2 labelPosition(x, bounds.Min.y + (bounds.GetHeight() - labelSize.y) / 2.0F);
    text(context, list, role, labelPosition, labelInk, label);

    if (variant == ButtonVariant::Link && state.hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        list.AddLine(ImVec2(labelPosition.x, labelPosition.y + labelSize.y), ImVec2(labelPosition.x + labelSize.x, labelPosition.y + labelSize.y), WidgetHelper::ink(labelInk), 1.0F);
    }

    return state.pressed;
}

bool WidgetHelper::closeCircle(const RenderContext& context, std::string_view id, ImVec2 center, float diameter) {
    const ImGuiID identity = ImGui::GetID(id.data(), id.data() + id.size());
    const float radius = diameter / 2.0F;
    const ButtonState state = interact(identity, ImRect(center.x - radius, center.y - radius, center.x + radius, center.y + radius));
    Painter::closeCircle(*ImGui::GetWindowDrawList(), center, diameter, context.color(ThemeColor::Danger), context.color(ThemeColor::OnDanger), state.hovered || state.held);

    return state.pressed;
}

// A field left less than a pixel submits nothing, because ImGui reads a width of zero or below as the rest of the window, and its clear button shows only while the text keeps a pixel beside it.
TextFieldResult WidgetHelper::textField(const RenderContext& context, std::string_view id, const ImRect& bounds, std::string& value, const TextFieldOptions& options) {
    TextFieldResult result;

    if (bounds.GetWidth() < 1.0F || bounds.GetHeight() < 1.0F) {
        return result;
    }

    ImDrawList& list = *ImGui::GetWindowDrawList();
    const float clearRoom = (clearButtonSize + clearButtonMargin) * context.scale();
    const bool clearable = options.clearButton && !options.readOnly && !value.empty() && !disabled() && bounds.GetWidth() - clearRoom >= 1.0F;
    const float clearWidth = clearable ? clearRoom : 0.0F;

    frame(context, bounds, context.color(ThemeColor::Raised));

    ImGui::PushID(id.data(), id.data() + id.size());
    const FontRole role = context.font(options.monospace ? ThemeFont::Monospace : ThemeFont::Interface);
    const FontScope font(context.fonts(), role);
    const float padding = context.metric(ThemeMetric::ControlHorizontalPadding);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padding, std::max(0.0F, (bounds.GetHeight() - ImGui::GetFontSize()) / 2.0F)));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, context.color(ThemeColor::TextMuted).vector());
    ImGui::PushStyleColor(ImGuiCol_NavCursor, IM_COL32(0, 0, 0, 0));

    ImGuiInputTextFlags flags = ImGuiInputTextFlags_None;
    flags |= options.password ? ImGuiInputTextFlags_Password : ImGuiInputTextFlags_None;
    flags |= options.readOnly ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None;
    flags |= options.numeric ? ImGuiInputTextFlags_CharsScientific : ImGuiInputTextFlags_None;

    ImGuiInputTextState* editing = ImGui::GetInputTextState(ImGui::GetID("##field"));
    ImGuiStorage& storage = *ImGui::GetStateStorage();
    const ImGuiID selecting = ImGui::GetID("##selecting");
    storage.SetBool(selecting, storage.GetBool(selecting) || (options.focus && options.selectAll));
    const bool reloading = editing != nullptr && options.reload && !options.readOnly;

    // A value replaced from outside while the field is being edited is reloaded, because the field otherwise keeps writing its own copy back, while a read-only field reads its value afresh every frame.
    // A field asked for the keyboard with its text selected keeps the request until the keyboard reaches it a frame later, selects the new text of a reload whole, and one that already has the keyboard selects its text at once, so what is typed next replaces it.
    if (reloading && storage.GetBool(selecting)) {
        editing->ReloadUserBufAndSelectAll();
    } else if (reloading) {
        editing->ReloadUserBufAndKeepSelection();
    } else if (editing != nullptr && storage.GetBool(selecting) && ImGui::GetActiveID() == editing->ID) {
        editing->SelectAll();
    }

    flags |= storage.GetBool(selecting) ? ImGuiInputTextFlags_AutoSelectAll : ImGuiInputTextFlags_None;

    if (options.focus) {
        takeKeyboard();
    }

    const std::string placeholder(options.placeholder);
    ImGui::SetCursorScreenPos(bounds.Min);
    ImGui::SetNextItemWidth(bounds.GetWidth() - clearWidth);
    result.changed = ImGui::InputTextWithHint("##field", placeholder.c_str(), &value, flags);
    result.active = ImGui::IsItemActive();
    result.focused = focused();
    result.deactivated = ImGui::IsItemDeactivated();
    result.submitted = result.deactivated && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));

    if (result.active) {
        storage.SetBool(selecting, false);
    }

    // A field that reports its arrows keeps Up and Down from the navigation of ImGui while it has the keyboard, so they move a list beside it instead.
    if (options.arrows && result.active) {
        GImGui->ActiveIdUsingNavDirMask |= (1U << static_cast<unsigned>(ImGuiDir_Up)) | (1U << static_cast<unsigned>(ImGuiDir_Down));
        result.arrow = ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? ImGuiDir_Up : ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? ImGuiDir_Down : ImGuiDir_None;
    }

    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar();

    if (result.focused) {
        focusBorder(context, bounds);
    }

    if (clearable) {
        const float side = clearButtonSize * context.scale();
        const ImRect area(bounds.Max.x - clearWidth, bounds.Min.y + (bounds.GetHeight() - side) / 2.0F, bounds.Max.x - clearButtonMargin * context.scale(), bounds.Min.y + (bounds.GetHeight() + side) / 2.0F);
        const ButtonState state = interact(ImGui::GetID("##clear"), area);
        IconCatalog::draw(context.fonts(), list, Icon::Close, area.Min, side, context.color(state.hovered ? ThemeColor::Text : ThemeColor::TextMuted));

        if (state.pressed) {
            value.clear();
            result.changed = true;
        }
    }

    ImGui::PopID();

    return result;
}

// A field that sends on Enter keeps the new line Shift and Enter type and the new lines of pasted text, and takes Enter alone as the request to send without losing the keyboard.
int WidgetHelper::submitOnEnter(ImGuiInputTextCallbackData* data) {
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);

    if (data->EventChar != '\n' || ImGui::GetIO().KeyShift || !enter) {
        return 0;
    }

    *static_cast<bool*>(data->UserData) = true;

    return 1;
}

TextFieldResult WidgetHelper::textArea(const RenderContext& context, std::string_view id, const ImRect& bounds, std::string& value, const TextAreaOptions& options) {
    if (bounds.GetWidth() < 1.0F || bounds.GetHeight() < 1.0F) {
        return {};
    }

    ImDrawList& list = *ImGui::GetWindowDrawList();
    const float radius = context.metric(ThemeMetric::ControlRadius);
    list.AddRectFilled(bounds.Min, bounds.Max, ink(context.color(ThemeColor::Raised)), radius);

    ImGui::PushID(id.data(), id.data() + id.size());
    const FontScope font(context.fonts(), context.font(ThemeFont::Interface));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(textAreaHorizontalPadding * context.scale(), textAreaVerticalPadding * context.scale()));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_NavCursor, IM_COL32(0, 0, 0, 0));

    ImGuiInputTextFlags flags = options.readOnly ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None;
    flags |= options.wrap ? ImGuiInputTextFlags_WordWrap : ImGuiInputTextFlags_None;
    flags |= options.submitOnEnter ? ImGuiInputTextFlags_CallbackCharFilter : ImGuiInputTextFlags_None;

    // A value replaced from outside while the area is being edited is reloaded, such as a composer emptied once its message is sent, while a read-only area reads its value afresh every frame.
    if (ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetID("##area")); state != nullptr && options.reload && !options.readOnly) {
        state->ReloadUserBufAndKeepSelection();
    }

    ImGui::SetCursorScreenPos(bounds.Min);
    TextFieldResult result;
    result.changed = ImGui::InputTextMultiline("##area", &value, bounds.GetSize(), flags, options.submitOnEnter ? &WidgetHelper::submitOnEnter : nullptr, &result.submitted);
    result.active = ImGui::IsItemActive();
    result.focused = focused();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();

    // A multiline field has no hint of its own, so the placeholder is drawn while it is empty and nobody is typing into it.
    if (value.empty() && !result.active && !options.placeholder.empty()) {
        text(context, list, context.font(ThemeFont::Interface), ImVec2(bounds.Min.x + textAreaHorizontalPadding * context.scale(), bounds.Min.y + textAreaVerticalPadding * context.scale()), context.color(ThemeColor::TextMuted), options.placeholder);
    }

    Painter::rectBorder(list, bounds.Min, bounds.Max, radius, context.metric(ThemeMetric::LineWidth), context.color(result.focused ? ThemeColor::Focus : ThemeColor::BorderStrong));
    ImGui::PopID();

    return result;
}

bool WidgetHelper::combo(const RenderContext& context, std::string_view id, const ImRect& bounds, const std::vector<std::string>& items, int& selected, std::string_view placeholder) {
    ImGui::PushID(id.data(), id.data() + id.size());
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const ButtonState state = interact(ImGui::GetID("##combo"), bounds);
    const bool open = ImGui::IsPopupOpen("##popup");
    const FontRole role = context.font(ThemeFont::Interface);
    const float padding = context.metric(ThemeMetric::ControlHorizontalPadding);
    const float indicator = context.metric(ThemeMetric::ComboIndicatorWidth);
    const bool hasSelection = selected >= 0 && static_cast<std::size_t>(selected) < items.size();

    frame(context, bounds, context.color(ThemeColor::Raised));
    const ImRect textBounds(bounds.Min.x + padding, bounds.Min.y, bounds.Max.x - indicator, bounds.Max.y);
    alignedText(context, list, role, textBounds, context.color(hasSelection && !disabled() ? ThemeColor::Text : ThemeColor::TextMuted), hasSelection ? std::string_view(items[static_cast<std::size_t>(selected)]) : placeholder, TextAlign::Start);
    Painter::chevron(list, ImVec2(bounds.Max.x - indicator / 2.0F, bounds.GetCenter().y), comboChevronWidth * context.scale(), ChevronDirection::Down, context.color(disabled() ? ThemeColor::TextMuted : ThemeColor::Text));

    if (open) {
        focusBorder(context, bounds);
    }

    if (state.pressed) {
        ImGui::OpenPopup("##popup");
    }

    // The list opens under the field, at least as wide as it, and scrolls once it holds more entries than it shows.
    bool changed = false;
    const float itemHeight = controlHeight(context) - 2.0F * context.scale();
    const float inset = 4.0F * context.scale();
    const float visible = static_cast<float>(std::min<std::size_t>(items.size(), comboVisibleItems));

    ImGui::SetNextWindowSizeConstraints(ImVec2(bounds.GetWidth(), 0.0F), ImVec2(std::max(bounds.GetWidth(), 480.0F * context.scale()), visible * itemHeight + inset * 2.0F));
    placePopup(context, "##popup", bounds);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(inset, inset));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0F, 0.0F));

    if (ImGui::BeginPopup("##popup", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize)) {
        const float width = std::max(bounds.GetWidth() - inset * 2.0F, ImGui::GetContentRegionAvail().x);

        for (std::size_t index = 0; index < items.size(); ++index) {
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const ImRect row(origin, ImVec2(origin.x + width, origin.y + itemHeight));
            const bool current = static_cast<int>(index) == selected;
            const ButtonState item = interact(ImGui::GetID(static_cast<int>(index)), row);
            ImDrawList& popupList = *ImGui::GetWindowDrawList();

            if (current || item.hovered) {
                popupList.AddRectFilled(row.Min, row.Max, ink(context.color(current ? ThemeColor::Accent : ThemeColor::Hover)), context.metric(ThemeMetric::ControlRadius));
            }

            alignedText(context, popupList, role, ImRect(row.Min.x + padding, row.Min.y, row.Max.x - padding, row.Max.y), context.color(current ? ThemeColor::OnAccent : ThemeColor::Text), items[index], TextAlign::Start);

            if (current && ImGui::IsWindowAppearing()) {
                ImGui::SetScrollHereY();
            }

            if (item.pressed) {
                selected = static_cast<int>(index);
                changed = true;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);
    ImGui::PopID();

    return changed;
}

bool WidgetHelper::checkbox(const RenderContext& context, std::string_view id, const ImRect& bounds, bool& value, std::string_view label) {
    const ImGuiID identity = ImGui::GetID(id.data(), id.data() + id.size());
    const ButtonState state = interact(identity, bounds);
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const float side = choiceBoxSize * context.scale();
    const ImRect box(bounds.Min.x, bounds.GetCenter().y - side / 2.0F, bounds.Min.x + side, bounds.GetCenter().y + side / 2.0F);
    const float radius = context.metric(ThemeMetric::ControlRadius);

    if (state.pressed) {
        value = !value;
    }

    if (value) {
        list.AddRectFilled(box.Min, box.Max, ink(context.color(state.hovered ? ThemeColor::AccentHover : ThemeColor::Accent)), radius);
        Painter::checkMark(list, box.Min, side, context.color(ThemeColor::OnAccent));
    } else {
        list.AddRectFilled(box.Min, box.Max, ink(context.color(ThemeColor::Raised)), radius);
        Painter::rectBorder(list, box.Min, box.Max, radius, context.metric(ThemeMetric::LineWidth), context.color(state.hovered ? ThemeColor::TextMuted : ThemeColor::BorderStrong));
    }

    if (!label.empty()) {
        const ImRect labelBounds(box.Max.x + choiceLabelSpacing * context.scale(), bounds.Min.y, bounds.Max.x, bounds.Max.y);
        alignedText(context, list, context.font(ThemeFont::Interface), labelBounds, context.color(disabled() ? ThemeColor::TextMuted : ThemeColor::Text), label, TextAlign::Start);
    }

    return state.pressed;
}

bool WidgetHelper::toggle(RenderContext& context, std::string_view id, const ImRect& bounds, bool& value) {
    const ImGuiID identity = ImGui::GetID(id.data(), id.data() + id.size());
    const float width = toggleTrackWidth * context.scale();
    const float height = toggleTrackHeight * context.scale();
    const ImRect track(bounds.Min.x, bounds.GetCenter().y - height / 2.0F, bounds.Min.x + width, bounds.GetCenter().y + height / 2.0F);
    const ButtonState state = interact(identity, track);

    if (state.pressed) {
        value = !value;
    }

    // The knob slides toward the state it reached, and the frames of that slide are asked for because nothing else would draw them.
    ImGuiStorage& storage = *ImGui::GetStateStorage();
    const float target = value ? 1.0F : 0.0F;
    float position = storage.GetFloat(identity, target);
    const float step = ImGui::GetIO().DeltaTime * toggleSpeed;
    position = position < target ? std::min(target, position + step) : std::max(target, position - step);
    storage.SetFloat(identity, position);

    if (position != target) {
        context.requestFrame();
    }

    ImDrawList& list = *ImGui::GetWindowDrawList();
    const Color off = context.color(state.hovered ? ThemeColor::BorderStrong : ThemeColor::Pressed);
    const Color on = context.color(state.hovered ? ThemeColor::AccentHover : ThemeColor::Accent);
    list.AddRectFilled(track.Min, track.Max, ink(Color::blend(off, on, position)), height / 2.0F);

    const float knob = height - toggleKnobInset * 2.0F * context.scale();
    const float travel = width - height;
    const ImVec2 center(track.Min.x + height / 2.0F + travel * position, track.GetCenter().y);
    list.AddCircleFilled(center, knob / 2.0F, ink(Color::blend(context.color(ThemeColor::Text), context.color(ThemeColor::OnAccent), position)));

    return state.pressed;
}

bool WidgetHelper::radio(const RenderContext& context, std::string_view id, const ImRect& bounds, bool selected, std::string_view label) {
    const ImGuiID identity = ImGui::GetID(id.data(), id.data() + id.size());
    const ButtonState state = interact(identity, bounds);
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const float radius = choiceBoxSize * context.scale() / 2.0F;
    const ImVec2 center(bounds.Min.x + radius, bounds.GetCenter().y);

    if (selected) {
        list.AddCircleFilled(center, radius, ink(context.color(state.hovered ? ThemeColor::AccentHover : ThemeColor::Accent)));
        list.AddCircleFilled(center, radius * 0.4F, ink(context.color(ThemeColor::OnAccent)));
    } else {
        list.AddCircleFilled(center, radius, ink(context.color(ThemeColor::Raised)));
        list.AddCircle(center, radius - 0.5F, ink(context.color(state.hovered ? ThemeColor::TextMuted : ThemeColor::BorderStrong)), 0, 1.0F);
    }

    if (!label.empty()) {
        const ImRect labelBounds(center.x + radius + choiceLabelSpacing * context.scale(), bounds.Min.y, bounds.Max.x, bounds.Max.y);
        alignedText(context, list, context.font(ThemeFont::Interface), labelBounds, context.color(disabled() ? ThemeColor::TextMuted : ThemeColor::Text), label, TextAlign::Start);
    }

    return state.pressed;
}

bool WidgetHelper::slider(const RenderContext& context, std::string_view id, const ImRect& bounds, double& value, double minimum, double maximum, double step) {
    const ImGuiID identity = ImGui::GetID(id.data(), id.data() + id.size());
    const ButtonState state = interact(identity, bounds, ImGuiButtonFlags_PressedOnClick);
    const float knob = sliderKnobSize * context.scale();
    const float left = bounds.Min.x + knob / 2.0F;
    const float width = std::max(1.0F, bounds.GetWidth() - knob);
    const double range = maximum - minimum;
    const double before = value;
    const bool stepped = ImGui::IsItemFocused() && (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_RightArrow));

    if (state.held && range > 0.0) {
        const double fraction = std::clamp(static_cast<double>((ImGui::GetIO().MousePos.x - left) / width), 0.0, 1.0);
        value = minimum + fraction * range;
    }

    if (stepped) {
        value += ImGui::IsKeyPressed(ImGuiKey_LeftArrow) ? -step : step;
    }

    // The value moves onto the steps only when the reader moved it, so a value the plugin gave is drawn as it is.
    if ((state.held || stepped) && step > 0.0) {
        value = std::clamp(minimum + std::round((value - minimum) / step) * step, minimum, maximum);
    }

    ImDrawList& list = *ImGui::GetWindowDrawList();
    const float groove = sliderGrooveHeight * context.scale();
    const float fraction = range > 0.0 ? static_cast<float>((value - minimum) / range) : 0.0F;
    const float centerY = bounds.GetCenter().y;
    const float knobX = left + width * fraction;
    list.AddRectFilled(ImVec2(left, centerY - groove / 2.0F), ImVec2(left + width, centerY + groove / 2.0F), ink(context.color(ThemeColor::Pressed)), groove / 2.0F);
    list.AddRectFilled(ImVec2(left, centerY - groove / 2.0F), ImVec2(knobX, centerY + groove / 2.0F), ink(context.color(ThemeColor::Accent)), groove / 2.0F);
    list.AddCircleFilled(ImVec2(knobX, centerY), knob / 2.0F, ink(context.color(state.hovered || state.held ? ThemeColor::OnAccent : ThemeColor::Text)));
    list.AddCircle(ImVec2(knobX, centerY), knob / 2.0F - 0.5F, ink(context.color(ThemeColor::BorderStrong)), 0, 1.0F);

    return value != before;
}

ImVec2 WidgetHelper::checkboxSize(const RenderContext& context, std::string_view label) {
    const float labelWidth = label.empty() ? 0.0F : choiceLabelSpacing * context.scale() + textSize(context, context.font(ThemeFont::Interface), label).x;
    return {std::ceil(choiceBoxSize * context.scale() + labelWidth), controlHeight(context)};
}

ImVec2 WidgetHelper::toggleSize(const RenderContext& context) {
    return {toggleTrackWidth * context.scale(), controlHeight(context)};
}

void WidgetHelper::pageHeader(const RenderContext& context, const ImRect& bounds, std::string_view title, std::string_view caption) {
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const float scale = context.scale();
    list.AddRectFilled(bounds.Min, bounds.Max, ink(context.color(ThemeColor::Panel)));
    Painter::horizontalDivider(list, ImVec2(bounds.Min.x, bounds.Max.y - context.metric(ThemeMetric::LineWidth)), bounds.GetWidth(), context.metric(ThemeMetric::LineWidth), context.color(ThemeColor::Border));

    const FontRole titleFont = context.font(ThemeFont::PageTitle);
    const float titleWidth = textSize(context, titleFont, title).x;
    const float left = bounds.Min.x + pageHeaderLeft * scale;
    alignedText(context, list, titleFont, ImRect(left, bounds.Min.y, left + titleWidth, bounds.Max.y - context.metric(ThemeMetric::LineWidth)), context.color(ThemeColor::Text), title, TextAlign::Start);

    if (!caption.empty()) {
        const FontRole captionFont = context.font(ThemeFont::Interface);
        const float captionLeft = left + titleWidth + pageHeaderGap * scale;
        alignedText(context, list, captionFont, ImRect(captionLeft, bounds.Min.y, captionLeft + textSize(context, captionFont, caption).x, bounds.Max.y - context.metric(ThemeMetric::LineWidth)), context.color(ThemeColor::TextMuted), caption, TextAlign::Start);
    }
}

float WidgetHelper::pageHeaderLeading(const RenderContext& context, std::string_view title, std::string_view caption) {
    const float scale = context.scale();
    float width = pageHeaderLeft * scale + textSize(context, context.font(ThemeFont::PageTitle), title).x + pageHeaderGap * scale;

    if (!caption.empty()) {
        width += textSize(context, context.font(ThemeFont::Interface), caption).x + pageHeaderGap * scale;
    }

    return std::ceil(width);
}

// A section heading is written in upper case and painted in the accent, applied here so no catalog carries the convention.
void WidgetHelper::sectionTitle(const RenderContext& context, ImDrawList& list, const ImRect& bounds, std::string_view text) {
    alignedText(context, list, context.font(ThemeFont::SectionTitle), bounds, context.color(ThemeColor::Accent), TextCaseHelper::upper(text), TextAlign::Start);
}

float WidgetHelper::listRowHeight(const RenderContext& context) {
    return lineHeight(context, context.font(ThemeFont::Interface)) + listRowPadding * context.scale();
}

// A selected row is filled with the accent and written in the ink made for it, and a navigation row marks itself with the accent bar instead.
ButtonState WidgetHelper::listRow(const RenderContext& context, ImGuiID id, const ImRect& bounds, const RowContent& content, bool selected, RowStyle style) {
    const ButtonState state = interact(id, bounds);
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const float scale = context.scale();
    const float radius = context.metric(ThemeMetric::ControlRadius);
    const bool filled = selected && style == RowStyle::Default;

    if (filled) {
        list.AddRectFilled(bounds.Min, bounds.Max, ink(context.color(ThemeColor::Accent)), radius);
    } else if (selected || state.hovered) {
        list.AddRectFilled(bounds.Min, bounds.Max, ink(context.color(ThemeColor::Hover)), radius);
    }

    if (selected && style == RowStyle::Navigation) {
        list.AddRectFilled(bounds.Min, ImVec2(bounds.Min.x + navigationMarker * scale, bounds.Max.y), ink(context.color(ThemeColor::Accent)));
    }

    const float padding = listPadding * scale;
    const float icon = context.metric(ThemeMetric::SmallIconSize);
    const FontRole font = context.font(ThemeFont::Interface);
    const Color text = context.color(filled ? ThemeColor::OnAccent : disabled() ? ThemeColor::TextMuted : ThemeColor::Text);
    float x = bounds.Min.x + padding;

    if (content.icon.has_value()) {
        IconCatalog::draw(context.fonts(), list, *content.icon, ImFloor(ImVec2(x, bounds.GetCenter().y - icon / 2.0F)), icon, filled ? text : context.color(content.iconColor.value_or(ThemeColor::TextMuted)));
        x += icon + glyphSpacing * scale;
    }

    // The detail keeps its whole width at the right end, and the text is shortened before it.
    const float detailWidth = content.detail.empty() ? 0.0F : textSize(context, font, content.detail).x + glyphSpacing * scale;
    const ImRect detailBounds(x, bounds.Min.y, bounds.Max.x - padding, bounds.Max.y);
    const ImRect textBounds(x, bounds.Min.y, std::max(x, bounds.Max.x - padding - detailWidth), bounds.Max.y);

    if (!content.detail.empty()) {
        alignedText(context, list, font, detailBounds, context.color(filled ? ThemeColor::OnAccent : ThemeColor::TextMuted), content.detail, TextAlign::End);
    }

    alignedText(context, list, font, textBounds, text, content.text, TextAlign::Start);

    return state;
}

// A field that has the keyboard, or that the navigation cursor rests on, draws the accent border itself, so ImGui draws no ring of its own around it.
bool WidgetHelper::focused() {
    return ImGui::IsItemActive() || (ImGui::IsItemFocused() && GImGui->NavCursorVisible);
}

// The next control drawn takes the keyboard, and the control that had it lets it go at once, so one drawn later in another window, such as the dialog button just pressed, cannot claim it back.
void WidgetHelper::takeKeyboard() {
    ImGui::SetKeyboardFocusHere();
    ImGui::SetNavID(0, ImGui::GetCurrentWindow()->DC.NavLayerCurrent, GImGui->CurrentFocusScopeId, ImRect());
}

// The navigation cursor rests on the control drawn last without pressing it, shown so the reader sees where it went, and a field being edited lets the keyboard go.
void WidgetHelper::takeNavigation() {
    if (ImGui::GetActiveID() != 0 && ImGui::GetActiveID() != ImGui::GetItemID()) {
        ImGui::ClearActiveID();
    }

    ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
    ImGui::SetNavCursorVisible(true);
}

void WidgetHelper::focusBorder(const RenderContext& context, const ImRect& bounds) {
    Painter::rectBorder(*ImGui::GetWindowDrawList(), bounds.Min, bounds.Max, context.metric(ThemeMetric::ControlRadius), context.metric(ThemeMetric::LineWidth), context.color(ThemeColor::Focus));
}

void WidgetHelper::frame(const RenderContext& context, const ImRect& bounds, Color fill) {
    ImGui::GetWindowDrawList()->AddRectFilled(bounds.Min, bounds.Max, ink(fill), context.metric(ThemeMetric::ControlRadius));
}

// A tooltip opens as the dark panel of the theme, and what it holds is drawn between this call and the end of the tooltip when it answers true.
bool WidgetHelper::beginTooltip(const RenderContext& context) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(context.metric(ThemeMetric::ControlHorizontalPadding), context.metric(ThemeMetric::ControlVerticalPadding)));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, context.color(ThemeColor::Tooltip).vector());
    ImGui::PushStyleColor(ImGuiCol_Border, context.color(ThemeColor::BorderStrong).vector());
    const bool open = ImGui::BeginTooltip();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();

    return open;
}

void WidgetHelper::endTooltip() {
    ImGui::EndTooltip();
}

float WidgetHelper::tooltipWidth(const RenderContext& context) {
    return tooltipMaximumWidth * context.scale();
}

void WidgetHelper::tooltip(const RenderContext& context, std::string_view text) {
    if (!beginTooltip(context)) {
        return;
    }

    {
        const FontScope font(context.fonts(), context.font(ThemeFont::Interface));
        ImGui::PushStyleColor(ImGuiCol_Text, context.color(ThemeColor::OnTooltip).vector());
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tooltipWidth(context));
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

    endTooltip();
}

// The last item shows its tooltip once the pointer has rested on it, and the frame that shows it is asked for because nothing else would draw it.
void WidgetHelper::itemTooltip(RenderContext& context, std::string_view text) {
    if (text.empty() || !ImGui::IsItemHovered()) {
        return;
    }

    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        const ImGuiStyle& style = ImGui::GetStyle();
        context.requestFrameAt(context.time() + std::max(style.HoverDelayShort, style.HoverStationaryDelay));
        return;
    }

    tooltip(context, text);
}

// A popup opens below the control it belongs to, above it when only there it fits, and aligned to its right edge when its left one would push it out, so it stays inside the window.
void WidgetHelper::placePopup(const RenderContext& context, std::string_view name, const ImRect& anchor) {
    const float gap = popupGap * context.scale();
    const ImGuiID id = ImGui::GetID(name.data(), name.data() + name.size());

    for (const ImGuiPopupData& popup : GImGui->OpenPopupStack) {
        if (popup.PopupId != id || popup.Window == nullptr || !popup.Window->WasActive) {
            continue;
        }

        const ImVec2 size = ImGui::CalcWindowNextAutoFitSize(popup.Window);
        const ImRect allowed = ImGui::GetPopupAllowedExtentRect(popup.Window);
        const bool below = anchor.Max.y + gap + size.y <= allowed.Max.y || allowed.Max.y - anchor.Max.y >= anchor.Min.y - allowed.Min.y;
        const float x = anchor.Min.x + size.x <= allowed.Max.x ? anchor.Min.x : anchor.Max.x - size.x;
        const float y = below ? anchor.Max.y + gap : anchor.Min.y - gap - size.y;
        ImGui::SetNextWindowPos(ImVec2(std::clamp(x, allowed.Min.x, std::max(allowed.Min.x, allowed.Max.x - size.x)), std::clamp(y, allowed.Min.y, std::max(allowed.Min.y, allowed.Max.y - size.y))));
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(anchor.Min.x, anchor.Max.y + gap));
}

} // namespace workpane::ui
