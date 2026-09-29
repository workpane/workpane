#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/model/RenderContext.h"

#include <TextEditor.h>
#include <imgui_internal.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::ui {

// The proposals of a language server under the cursor, each with its label, its detail, its documentation, the text it inserts and the range it replaces.
// The list narrows to what the reader keeps typing, keeps its choice in view and shows the documentation of the chosen proposal beside it.
class CodeCompletion final {
  public:
    static constexpr std::size_t shownRows{10};

    struct Proposal final {
        std::string label;
        std::string detail;
        std::string documentation;
        std::string insert;
        std::optional<TextEditor::DocSelection> range;
    };

    [[nodiscard]] static Result<std::vector<Proposal>> parse(const json::Json& items);

    void show(std::vector<Proposal> proposals, TextEditor::DocPos start, std::string_view typed, bool asked);
    void narrow(std::string_view typed);
    void step(int direction);
    void close();
    [[nodiscard]] bool open() const;
    [[nodiscard]] TextEditor::DocPos start() const;
    [[nodiscard]] const Proposal* chosen() const;
    [[nodiscard]] bool draw(RenderContext& context, const ImRect& caret);

  private:
    static constexpr std::size_t largestProposals{2000};
    static constexpr float listWidth{360.0F};
    static constexpr float documentationWidth{320.0F};
    static constexpr float rowInset{6.0F};
    static constexpr const char* popupName{"##completion"};

    [[nodiscard]] static Result<std::optional<TextEditor::DocSelection>> readRange(const json::Json& place);

    static void dismiss();

    void filter(std::string_view typed);
    void drawProposal(RenderContext& context, std::size_t index, float row, float inset, bool& clicked);
    void drawDocumentation(RenderContext& context, const ImRect& list) const;

    std::vector<Proposal> m_proposals;
    std::vector<std::size_t> m_shown;
    std::string m_typed;
    std::size_t m_chosen{0};
    TextEditor::DocPos m_start;
    bool m_open{false};
    bool m_popped{false};
    bool m_asked{false};
    bool m_scrollToChosen{false};
};

} // namespace workpane::ui
