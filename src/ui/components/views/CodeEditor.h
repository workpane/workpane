#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/Color.h"
#include "ui/FindBar.h"
#include "ui/Fonts.h"
#include "ui/IconCatalog.h"
#include "ui/components/views/CodeCompletion.h"
#include "ui/components/views/CodeFinder.h"
#include "ui/components/views/CodeText.h"
#include "ui/components/views/DarkModern.h"
#include "ui/model/CommonProperties.h"
#include "ui/model/Component.h"
#include "ui/model/ContentFontSize.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/theme/Theme.h"

#include <TextEditor.h>
#include <imgui_internal.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace workpane::ui {

// A source document edited in place, reporting its text a quarter of a second after the first edit it has not reported yet and its cursor as it moves.
// A plugin gives it a language, a color scheme, markers on lines or ranges, highlighted ranges, the text shown where the pointer rests and completion proposals.
class CodeEditor final : public Component {
  public:
    explicit CodeEditor(NodeId id);

    [[nodiscard]] std::string_view kind() const override;
    [[nodiscard]] Result<void> command(RenderContext& context, std::string_view name, const json::Json& arguments) override;
    void update(RenderContext& context) override;

  protected:
    [[nodiscard]] Alignment defaultRowAlignment() const override;
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    void applied() override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    struct Related final {
        std::string place;
        std::string message;
    };

    enum class DecorationStyle { Occurrence, Faded, Struck };

    struct Decoration final {
        TextEditor::DocPos start;
        TextEditor::DocPos end;
        DecorationStyle style;
    };

    struct Marker final {
        TextEditor::DocPos start;
        std::optional<TextEditor::DocPos> end;
        ThemeColor tone;
        std::string message;
        std::string detail;
        std::vector<Related> related;
    };

    static constexpr int changeDelayMilliseconds{250};
    static constexpr float editorMinimumHeight{160.0F};
    static constexpr double hoverDelaySeconds{0.5};
    static constexpr double blinkSeconds{0.5};
    static constexpr float lineHeight{1.5F};
    static constexpr std::size_t glyphMargin{3};
    static constexpr std::size_t textMargin{1};
    static constexpr float tooltipSpacing{6.0F};
    static constexpr std::size_t largestSelection{1000};
    static constexpr std::size_t largestList{200000};
    static constexpr std::int64_t largestRequest{9007199254740991};
    static constexpr std::int64_t largestPosition{20000000};
    static constexpr std::size_t largestRelated{64};
    static constexpr std::array<std::pair<std::string_view, TextEditor::Color>, 17> schemeRoles{{{"text", TextEditor::Color::text}, {"keyword", TextEditor::Color::keyword}, {"declaration", TextEditor::Color::declaration}, {"number", TextEditor::Color::number}, {"string", TextEditor::Color::string}, {"punctuation", TextEditor::Color::punctuation}, {"preprocessor", TextEditor::Color::preprocessor}, {"identifier", TextEditor::Color::identifier}, {"knownIdentifier", TextEditor::Color::knownIdentifier}, {"comment", TextEditor::Color::comment}, {"background", TextEditor::Color::background}, {"cursor", TextEditor::Color::cursor}, {"selection", TextEditor::Color::selection}, {"lineNumber", TextEditor::Color::lineNumber}, {"currentLineNumber", TextEditor::Color::currentLineNumber}, {"guide", TextEditor::Color::whitespace}, {"activeGuide", TextEditor::Color::matchingBracketActive}}};
    static constexpr std::size_t syntaxRoles{10};
    static constexpr std::array<std::pair<std::string_view, DecorationStyle>, 3> decorationStyles{{{"occurrence", DecorationStyle::Occurrence}, {"faded", DecorationStyle::Faded}, {"struck", DecorationStyle::Struck}}};
    static constexpr float occurrenceShare{0.45F};
    static constexpr float fadedShare{0.5F};
    static constexpr const char* menuName{"##editorMenu"};
    static constexpr std::size_t largestTriggers{16};
    static constexpr std::array<ImGuiKey, 7> completionKeyList{ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_PageUp, ImGuiKey_PageDown, ImGuiKey_Enter, ImGuiKey_Tab, ImGuiKey_Escape};

    [[nodiscard]] static std::optional<TextEditor::Color> role(std::string_view name, std::size_t roles);
    [[nodiscard]] static TextEditor::DocPos position(const json::Json& arguments, std::string_view context, std::optional<Error>& failure);
    [[nodiscard]] static Result<Marker> marker(const json::Json& item);
    [[nodiscard]] static int strength(ThemeColor tone);
    [[nodiscard]] static Icon icon(ThemeColor tone);
    [[nodiscard]] static std::string where(const RenderContext& context, const Marker& marker);
    [[nodiscard]] std::optional<Color> schemeColor(json::ObjectReader& reader, std::string_view name);
    [[nodiscard]] std::pair<TextEditor::DocPos, TextEditor::DocPos> underline(const Marker& marker) const;

    [[nodiscard]] TextEditor::Palette palette(const Theme& theme) const;
    [[nodiscard]] Color background(const Theme& theme) const;
    void loadPending();
    void replaceText(const std::string& value);
    void readLanguage(json::ObjectReader& reader);
    void readScheme(json::ObjectReader& reader);
    void readMarkers(json::ObjectReader& reader);
    void readHighlights(json::ObjectReader& reader);
    void readDecorations(json::ObjectReader& reader);
    void readCompletion(json::ObjectReader& reader);
    void holdCompletionKeys();
    void completionKeys(RenderContext& context);
    void followCompletion(bool typing, bool typed);
    void askCompletion(TextEditor::DocPos start, bool asked);
    void accept(const CodeCompletion::Proposal& proposal);
    [[nodiscard]] static bool wordCharacter(ImWchar character);
    void applyZoom(RenderContext& context, bool typing);
    void openFind(RenderContext& context);
    void findKeys(RenderContext& context, bool typing);
    void drawFind(RenderContext& context, const ImRect& bounds);
    void replaceMatches(bool every);
    void refreshFind();
    void showMatch();
    void menu(RenderContext& context, bool pointed);
    void definitionClick(RenderContext& context, bool pointed);
    void markCurrentLine();
    void drawDecorations(ImGuiWindow& editorWindow, Color background) const;
    void applyMarkers(RenderContext& context);
    void report(RenderContext& context);
    void hover(RenderContext& context, bool pointed);
    [[nodiscard]] std::vector<const Marker*> starting(std::size_t line) const;
    [[nodiscard]] std::vector<const Marker*> covering(TextEditor::DocPos glyph) const;
    void drawTooltip(const RenderContext& context, const std::vector<const Marker*>& markers, std::string_view text) const;
    void drawCaret(const TextEditor::CustomCaret& caret) const;
    void drawLineNumber(const TextEditor::CustomLineNumber& number);

    std::unique_ptr<CodeText> m_editor{std::make_unique<CodeText>()};
    std::shared_ptr<TextEditor::Language> m_defined;
    const TextEditor::Language* m_named{nullptr};
    bool m_languageStale{false};
    std::optional<std::string> m_pendingValue;
    json::Json m_language = "none";
    std::vector<std::pair<TextEditor::Color, Color>> m_scheme;
    Color m_currentLine{DarkModern::currentLine()};
    Color m_occurrence{DarkModern::occurrence()};
    std::vector<Decoration> m_decorations;
    bool m_definitions{false};
    bool m_readOnly{false};
    bool m_lineNumbers{true};
    bool m_wordWrap{false};
    bool m_whitespace{false};
    bool m_minimap{false};
    bool m_insertSpaces{true};
    std::int64_t m_tabSize{4};
    std::vector<Marker> m_markers;
    std::map<std::size_t, std::size_t> m_lineMarkers;
    std::optional<std::size_t> m_markedLine;
    std::size_t m_markedUndo{0};
    bool m_lineMarkStale{true};
    std::vector<CodeText::Highlight> m_highlights;
    CodeCompletion m_completion;
    std::vector<std::string> m_triggers;
    std::optional<TextEditor::DocPos> m_completionStart;
    std::int64_t m_completionNumber{0};
    bool m_completionAsked{false};
    bool m_typing{false};
    double m_fontSize{ContentFontSize::standard};
    std::string m_fontFamily{Fonts::bundledMonospace};
    bool m_changed{false};
    bool m_drawn{false};
    bool m_drawnOnce{false};
    bool m_flushRequested{false};
    bool m_paletteStale{true};
    bool m_markersStale{false};
    bool m_highlightsStale{false};
    bool m_findStale{false};
    bool m_hovers{false};
    bool m_completing{false};
    const Theme* m_paletteTheme{nullptr};
    std::size_t m_paintedUndo{0};
    std::size_t m_paintedLines{0};
    std::optional<nlohmann::json> m_completionRequest;
    RenderContext* m_rendering{nullptr};
    std::optional<std::size_t> m_gutterLine;
    std::optional<TextEditor::DocPos> m_hoverPosition;
    double m_hoverSince{0.0};
    bool m_hoverReported{false};
    bool m_pasteable{false};
    std::optional<nlohmann::json> m_hoverRequest;
    std::optional<TextEditor::DocPos> m_hoverTextPosition;
    std::string m_hoverText;
    double m_caretSince{0.0};
    std::optional<TextEditor::DocPos> m_reportedCursor;
    std::string m_reportedSelection;
    FindBar m_findBar;
    CodeFinder m_finder;
};

} // namespace workpane::ui
