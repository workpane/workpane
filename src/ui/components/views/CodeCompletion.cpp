#include "ui/components/views/CodeCompletion.h"

#include "ui/TextCase.h"
#include "ui/Widgets.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace workpane::ui {

// A proposal names its label and the text it inserts, and optionally its detail, its documentation and the range it replaces, which never ends before it starts.
Result<std::vector<CodeCompletion::Proposal>> CodeCompletion::parse(const json::Json& items) {
    if (!items.is_array() || items.size() > largestProposals) {
        return Result<std::vector<Proposal>>::failure({"editor_suggestions_invalid", "Completion proposals are a bounded list", "codeEditor.suggest"});
    }

    std::vector<Proposal> proposals;

    for (const auto& item : items) {
        Proposal proposal;
        const json::Json* place = &json::ObjectReader::emptyObject();
        json::ObjectReader reader(item, "codeEditor.suggest.items");
        reader.readText("label", proposal.label).readText("insert", proposal.insert).read("detail", proposal.detail, json::Presence::Optional).read("documentation", proposal.documentation, json::Presence::Optional).readObject("range", place, json::Presence::Optional);

        if (auto finished = reader.finish(); !finished.hasValue()) {
            return Result<std::vector<Proposal>>::failure(finished.error());
        }

        auto range = readRange(*place);

        if (!range.hasValue()) {
            return Result<std::vector<Proposal>>::failure(range.error());
        }

        proposal.range = range.value();
        proposals.push_back(std::move(proposal));
    }

    return Result<std::vector<Proposal>>::success(std::move(proposals));
}

// A range counts its lines and columns from one, and a proposal without one replaces the word under the cursor.
Result<std::optional<TextEditor::DocSelection>> CodeCompletion::readRange(const json::Json& place) {
    if (place.empty()) {
        return Result<std::optional<TextEditor::DocSelection>>::success(std::nullopt);
    }

    const std::int64_t limit = 1LL << 31;
    std::int64_t line = 0;
    std::int64_t column = 0;
    std::int64_t endLine = 0;
    std::int64_t endColumn = 0;
    json::ObjectReader reader(place, "codeEditor.suggest.items.range");
    reader.readInteger("line", line, 1, limit).readInteger("column", column, 1, limit).readInteger("endLine", endLine, 1, limit).readInteger("endColumn", endColumn, 1, limit);

    if (auto finished = reader.finish(); !finished.hasValue()) {
        return Result<std::optional<TextEditor::DocSelection>>::failure(finished.error());
    }

    if (endLine < line || (endLine == line && endColumn < column)) {
        return Result<std::optional<TextEditor::DocSelection>>::failure({"editor_suggestions_invalid", "The range of a completion proposal never ends before it starts", "codeEditor.suggest"});
    }

    return Result<std::optional<TextEditor::DocSelection>>::success(TextEditor::DocSelection(TextEditor::DocPos(static_cast<std::size_t>(line - 1), static_cast<std::size_t>(column - 1)), TextEditor::DocPos(static_cast<std::size_t>(endLine - 1), static_cast<std::size_t>(endColumn - 1))));
}

// Proposals asked for by the reader stay open to say there are none, while proposals that came from typing close when nothing matches.
void CodeCompletion::show(std::vector<Proposal> proposals, TextEditor::DocPos start, std::string_view typed, bool asked) {
    m_proposals = std::move(proposals);
    m_start = start;
    m_asked = asked;
    m_open = true;
    m_popped = false;
    filter(typed);
}

// A word the list already follows keeps the proposal the reader chose and where the list was scrolled.
void CodeCompletion::narrow(std::string_view typed) {
    if (typed == m_typed) {
        return;
    }

    filter(typed);
}

// The proposals whose label starts with what was typed are shown in the order the server gave, ignoring case.
void CodeCompletion::filter(std::string_view typed) {
    const std::string wanted = TextCase::lower(typed);
    m_typed = typed;
    m_shown.clear();

    for (std::size_t index = 0; index < m_proposals.size(); ++index) {
        if (TextCase::lower(m_proposals[index].label).starts_with(wanted)) {
            m_shown.push_back(index);
        }
    }

    m_chosen = 0;
    m_scrollToChosen = true;
    m_open = m_open && (!m_shown.empty() || m_asked);
}

void CodeCompletion::step(int direction) {
    if (m_shown.empty()) {
        return;
    }

    const auto size = static_cast<long>(m_shown.size());
    m_chosen = static_cast<std::size_t>(((static_cast<long>(m_chosen) + direction) % size + size) % size);
    m_scrollToChosen = true;
}

void CodeCompletion::close() {
    m_open = false;
    m_proposals.clear();
    m_shown.clear();
}

bool CodeCompletion::open() const {
    return m_open;
}

TextEditor::DocPos CodeCompletion::start() const {
    return m_start;
}

const CodeCompletion::Proposal* CodeCompletion::chosen() const {
    return m_shown.empty() ? nullptr : &m_proposals[m_shown[m_chosen]];
}

// The list opens under the caret, or above it when the window has no room below, the documentation of the chosen proposal stands beside it, and a click on a proposal answers its index.
bool CodeCompletion::draw(RenderContext& context, const ImRect& caret) {
    if (!m_open) {
        dismiss();
        return false;
    }

    // A list ImGui closed because the reader clicked elsewhere stays closed.
    if (!ImGui::IsPopupOpen(popupName) && m_popped) {
        close();
        return false;
    }

    const float scale = context.scale();
    const float row = Widgets::controlHeight(context);
    const float inset = rowInset * scale;
    const Proposal* current = chosen();
    const bool documented = current != nullptr && !current->documentation.empty();
    const float width = listWidth * scale + (documented ? documentationWidth * scale : 0.0F);
    const float height = row * static_cast<float>(std::clamp<std::size_t>(m_shown.size(), 1, shownRows)) + inset * 2.0F;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImRect allowed(viewport->WorkPos, ImVec2(viewport->WorkPos.x + viewport->WorkSize.x, viewport->WorkPos.y + viewport->WorkSize.y));
    const bool below = caret.Max.y + height <= allowed.Max.y || allowed.Max.y - caret.Max.y >= caret.Min.y - allowed.Min.y;
    const float x = std::clamp(caret.Min.x, allowed.Min.x, std::max(allowed.Min.x, allowed.Max.x - width));
    const float y = std::clamp(below ? caret.Max.y : caret.Min.y - height, allowed.Min.y, std::max(allowed.Min.y, allowed.Max.y - height));

    if (!ImGui::IsPopupOpen(popupName)) {
        ImGui::OpenPopup(popupName);
    }

    m_popped = true;

    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(inset, inset));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0F, 0.0F));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, context.color(ThemeColor::Raised).vector());
    ImGui::PushStyleColor(ImGuiCol_Border, context.color(ThemeColor::Border).vector());
    bool clicked = false;

    if (!ImGui::BeginPopup(popupName, ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar)) {
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
        m_open = false;
        return false;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImRect list(origin, ImVec2(origin.x + listWidth * scale - inset * 2.0F, origin.y + height - inset * 2.0F));

    if (m_shown.empty()) {
        Widgets::alignedText(context, *ImGui::GetWindowDrawList(), context.font(ThemeFont::Interface), ImRect(list.Min, ImVec2(list.Max.x, list.Min.y + row)), context.color(ThemeColor::TextMuted), context.translate("workpane.editor.no-suggestions"), TextAlign::Start);
    }

    if (!m_shown.empty() && ImGui::BeginChild("##proposals", list.GetSize(), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(m_shown.size()), row);

        // Only the rows on screen are drawn, and the chosen one is laid out even off screen so the list can scroll to it.
        if (m_scrollToChosen) {
            clipper.IncludeItemByIndex(static_cast<int>(m_chosen));
        }

        while (clipper.Step()) {
            for (int visible = clipper.DisplayStart; visible < clipper.DisplayEnd; ++visible) {
                drawProposal(context, static_cast<std::size_t>(visible), row, inset, clicked);
            }
        }
    }

    if (!m_shown.empty()) {
        ImGui::EndChild();
    }

    if (documented) {
        drawDocumentation(context, list);
    }

    ImGui::EndPopup();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);

    return clicked;
}

// A proposal is one row of the list, filled while it is chosen or under the pointer, and a press on it chooses it.
void CodeCompletion::drawProposal(RenderContext& context, std::size_t index, float row, float inset, bool& clicked) {
    const Proposal& proposal = m_proposals[m_shown[index]];
    const ImVec2 top = ImGui::GetCursorScreenPos();
    const ImRect area(top, ImVec2(top.x + ImGui::GetContentRegionAvail().x, top.y + row));
    const ButtonState state = Widgets::interact(ImGui::GetID(static_cast<int>(index)), area);
    const bool selected = index == m_chosen;
    ImDrawList& drawList = *ImGui::GetWindowDrawList();

    if (selected || state.hovered) {
        drawList.AddRectFilled(area.Min, area.Max, Widgets::ink(context.color(selected ? ThemeColor::Accent : ThemeColor::Hover)), context.metric(ThemeMetric::ControlRadius));
    }

    const ImRect text(area.Min.x + inset, area.Min.y, area.Max.x - inset, area.Max.y);
    const float detailWidth = std::min(Widgets::textSize(context, context.font(ThemeFont::Caption), proposal.detail).x, text.GetWidth() / 2.0F);
    Widgets::alignedText(context, drawList, context.font(ThemeFont::Monospace), ImRect(text.Min.x, text.Min.y, text.Max.x - detailWidth - inset, text.Max.y), context.color(selected ? ThemeColor::OnAccent : ThemeColor::Text), Widgets::elided(context, context.font(ThemeFont::Monospace), proposal.label, text.GetWidth() - detailWidth - inset), TextAlign::Start);
    Widgets::alignedText(context, drawList, context.font(ThemeFont::Caption), ImRect(text.Max.x - detailWidth, text.Min.y, text.Max.x, text.Max.y), context.color(selected ? ThemeColor::OnAccent : ThemeColor::TextMuted), Widgets::elided(context, context.font(ThemeFont::Caption), proposal.detail, detailWidth), TextAlign::End);

    if (selected && std::exchange(m_scrollToChosen, false)) {
        ImGui::SetScrollHereY();
    }

    if (state.pressed) {
        m_chosen = index;
        clicked = true;
    }
}

// A list closed by a key, by an acceptance or by the plugin closes its popup as well, so no hidden popup stays open behind the editor.
void CodeCompletion::dismiss() {
    const ImGuiID id = ImGui::GetID(popupName);
    const ImGuiContext& imgui = *GImGui;

    for (int level = 0; level < imgui.OpenPopupStack.Size; ++level) {
        if (imgui.OpenPopupStack[level].PopupId == id) {
            ImGui::ClosePopupToLevel(level, false);
            return;
        }
    }
}

// The documentation is written beside the list, as tall as the list and clipped to it, since a proposal is read there only while it is chosen.
void CodeCompletion::drawDocumentation(RenderContext& context, const ImRect& list) const {
    const float inset = rowInset * context.scale();
    const ImRect area(list.Max.x + inset * 2.0F, list.Min.y, list.Max.x + documentationWidth * context.scale(), list.Max.y);
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    drawList.AddLine(ImVec2(area.Min.x - inset, area.Min.y), ImVec2(area.Min.x - inset, area.Max.y), Widgets::ink(context.color(ThemeColor::Border)));
    drawList.PushClipRect(area.Min, area.Max, true);
    Widgets::paragraph(context, drawList, context.font(ThemeFont::Interface), area, context.color(ThemeColor::Text), chosen()->documentation, TextAlign::Start);
    drawList.PopClipRect();
}

} // namespace workpane::ui
