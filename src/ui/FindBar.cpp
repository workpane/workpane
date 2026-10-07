#include "ui/FindBar.h"

#include "localization/Localization.h"
#include "ui/IconCatalog.h"
#include "ui/TextFieldOptions.h"
#include "ui/TextFieldResult.h"
#include "ui/WidgetHelper.h"
#include "ui/theme/Theme.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace workpane::ui {

// The find keys of the platform step through the matches, Command and G on macOS and F3 elsewhere, with shift going back.
int FindBar::stepping() {
    const ImGuiIO& io = ImGui::GetIO();
    const bool next = io.ConfigMacOSXBehaviors ? io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_G, true) : !io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_F3, true);

    if (!next) {
        return 0;
    }

    return io.KeyShift ? -1 : 1;
}

void FindBar::focus() {
    m_focusRequest = true;
    m_typing = true;
}

void FindBar::release() {
    m_focusRequest = false;
    m_typing = false;
}

void FindBar::setQuery(std::string text, bool caseSensitive, bool wholeWord) {
    m_text = std::move(text);
    m_caseSensitive = caseSensitive;
    m_wholeWord = wholeWord;
}

const std::string& FindBar::text() const {
    return m_text;
}

bool FindBar::caseSensitive() const {
    return m_caseSensitive;
}

bool FindBar::wholeWord() const {
    return m_wholeWord;
}

const std::string& FindBar::replacement() const {
    return m_replacement;
}

bool FindBar::typing() const {
    return m_typing;
}

// The bar sits in the top right corner of the view it searches, one row high or two with the replacement.
ImRect FindBar::area(const RenderContext& context, const ImRect& bounds, bool replacing) {
    const float height = WidgetHelper::controlHeight(context);
    const float gap = barGap * context.scale();
    const float width = std::min(barWidth * context.scale(), bounds.GetWidth() - gap * 2.0F);
    const float rows = replacing ? 2.0F : 1.0F;
    return {bounds.Max.x - width - gap * 2.0F, bounds.Min.y + gap, bounds.Max.x - gap * 2.0F, bounds.Min.y + gap + height * rows + gap * (rows + 1.0F)};
}

// The buttons sit against the right edge of the bar, the count before them when it fits and the field takes what is left, and Enter steps forward or back with shift.
FindBar::Outcome FindBar::draw(RenderContext& context, const ImRect& bounds, const Count& count, bool replacing) {
    const float height = WidgetHelper::controlHeight(context);
    const float gap = barGap * context.scale();
    const ImRect bar = area(context, bounds, replacing);
    ImDrawList& list = *ImGui::GetWindowDrawList();
    list.AddRectFilled(bar.Min, bar.Max, WidgetHelper::ink(context.color(ThemeColor::Raised)), context.metric(ThemeMetric::ControlRadius));
    list.AddRect(bar.Min, bar.Max, WidgetHelper::ink(context.color(ThemeColor::Border)), context.metric(ThemeMetric::ControlRadius));

    Outcome outcome;
    const ImRect first(bar.Min.x, bar.Min.y, bar.Max.x, bar.Min.y + height + gap * 2.0F);
    const float button = context.metric(ThemeMetric::CompactButtonSize);
    const std::string written = m_text.empty() ? std::string() : counted(context, count);
    const float countWidth = WidgetHelper::textSize(context, context.font(ThemeFont::Caption), written).x + gap;
    float right = first.Max.x - gap;
    // clang-format off
    const auto place = [&right, &first, gap, button]() {
        const ImRect slot(right - button, first.GetCenter().y - button / 2.0F, right, first.GetCenter().y + button / 2.0F);
        right -= button + gap / 2.0F;

        return slot;
    };
    // clang-format on

    outcome.closed = WidgetHelper::button(context, "##findClose", place(), {}, Icon::Close, ButtonVariant::Icon);
    WidgetHelper::itemTooltip(context, context.translate("workpane.find.close"));

    if (WidgetHelper::button(context, "##findNext", place(), {}, Icon::Forward, ButtonVariant::Icon)) {
        outcome.step = 1;
    }

    WidgetHelper::itemTooltip(context, context.translate("workpane.find.next"));

    if (WidgetHelper::button(context, "##findPrevious", place(), {}, Icon::Back, ButtonVariant::Icon)) {
        outcome.step = -1;
    }

    WidgetHelper::itemTooltip(context, context.translate("workpane.find.previous"));

    if (WidgetHelper::button(context, "##findWord", place(), {}, Icon::WholeWord, ButtonVariant::Icon, m_wholeWord)) {
        m_wholeWord = !m_wholeWord;
        outcome.searched = true;
    }

    WidgetHelper::itemTooltip(context, context.translate("workpane.find.whole-word"));

    if (WidgetHelper::button(context, "##findCase", place(), {}, Icon::CaseSensitive, ButtonVariant::Icon, m_caseSensitive)) {
        m_caseSensitive = !m_caseSensitive;
        outcome.searched = true;
    }

    WidgetHelper::itemTooltip(context, context.translate("workpane.find.match-case"));

    // A bar too narrow for the count leaves it out, and the field that follows gives way the same way once the buttons fill the bar.
    if (right - countWidth >= first.Min.x + gap) {
        WidgetHelper::alignedText(context, list, context.font(ThemeFont::Caption), ImRect(right - countWidth, first.Min.y, right, first.Max.y), context.color(ThemeColor::TextMuted), written, TextAlign::End);
        right -= countWidth + gap;
    }

    // The field asks for the keyboard once when find opens, so a click on the view takes it back for good.
    const std::string placeholder = context.translate("workpane.find.placeholder");
    const TextFieldOptions options{.placeholder = placeholder, .focus = std::exchange(m_focusRequest, false)};
    const TextFieldResult result = WidgetHelper::textField(context, "##findText", ImRect(first.Min.x + gap, first.Min.y + gap, right, first.Max.y - gap), m_text, options);
    m_typing = result.active || result.submitted;
    outcome.searched = outcome.searched || result.changed;

    if (result.submitted) {
        outcome.step = ImGui::GetIO().KeyShift ? -1 : 1;
        m_focusRequest = true;
    }

    if (result.active && outcome.step == 0) {
        outcome.step = stepping();
    }

    // The field lets go of the keyboard in the frame Escape is pressed, so a field that just let go closes the bar too.
    if ((result.active || result.deactivated) && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        outcome.closed = true;
    }

    if (replacing) {
        drawReplace(context, ImRect(bar.Min.x, first.Max.y - gap, bar.Max.x, bar.Max.y), outcome);
    }

    m_typing = m_typing && !outcome.closed;

    return outcome;
}

std::string FindBar::counted(const RenderContext& context, const Count& count) {
    if (count.total == 0) {
        return context.translate("workpane.find.no-matches");
    }

    return context.localization().translate(count.bounded ? "workpane.find.matches-bounded" : "workpane.find.matches", std::vector<std::string>{std::to_string(count.position), std::to_string(count.total)});
}

void FindBar::drawReplace(RenderContext& context, const ImRect& row, Outcome& outcome) {
    const float gap = barGap * context.scale();
    const std::string one = context.translate("workpane.find.replace");
    const std::string every = context.translate("workpane.find.replace-all");
    const float everyWidth = WidgetHelper::buttonSize(context, every, std::nullopt, ButtonVariant::Toolbar).x;
    const float oneWidth = WidgetHelper::buttonSize(context, one, std::nullopt, ButtonVariant::Toolbar).x;
    const float top = row.Min.y + gap;
    const float bottom = row.Max.y - gap;
    float right = row.Max.x - gap;

    outcome.replacedAll = WidgetHelper::button(context, "##replaceAll", ImRect(right - everyWidth, top, right, bottom), every, std::nullopt, ButtonVariant::Toolbar);
    right -= everyWidth + gap / 2.0F;
    outcome.replaced = WidgetHelper::button(context, "##replaceOne", ImRect(right - oneWidth, top, right, bottom), one, std::nullopt, ButtonVariant::Toolbar);
    right -= oneWidth + gap;

    const std::string placeholder = context.translate("workpane.find.replace-placeholder");
    const TextFieldOptions options{.placeholder = placeholder};
    const TextFieldResult result = WidgetHelper::textField(context, "##replaceText", ImRect(row.Min.x + gap, top, right, bottom), m_replacement, options);
    m_typing = m_typing || result.active || result.submitted;
    outcome.replaced = outcome.replaced || result.submitted;

    if ((result.active || result.deactivated) && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        outcome.closed = true;
    }
}

} // namespace workpane::ui
