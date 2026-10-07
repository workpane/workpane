#include "ui/components/views/CodeEditor.h"

#include "localization/Localization.h"
#include "ui/FontFamilies.h"
#include "ui/FontScope.h"
#include "ui/Fonts.h"
#include "ui/Menus.h"
#include "ui/Painter.h"
#include "ui/WidgetHelper.h"
#include "ui/components/views/CodeLanguage.h"
#include "ui/components/views/DarkModern.h"
#include "ui/model/ContentFontSize.h"
#include "ui/theme/FontRole.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iterator>
#include <map>
#include <utility>

namespace workpane::ui {

// A role is named as the scheme names it, and only the first roles, which color text, can be given to a highlighted range.
std::optional<TextEditor::Color> CodeEditor::role(std::string_view name, std::size_t roles) {
    for (std::size_t index = 0; index < roles; ++index) {
        if (schemeRoles[index].first == name) {
            return schemeRoles[index].second;
        }
    }

    return std::nullopt;
}

// A position is a line and a column counted from one, the way the cursor is reported.
TextEditor::DocPos CodeEditor::position(const json::Json& arguments, std::string_view context, std::optional<Error>& failure) {
    std::int64_t line = 0;
    std::int64_t column = 0;
    json::ObjectReader reader(arguments, std::string(context));
    reader.readInteger("line", line, 1, largestPosition).readInteger("column", column, 1, largestPosition);

    if (auto finished = reader.finish(); !finished.hasValue()) {
        failure = finished.error();
    }

    return {static_cast<std::size_t>(line - 1), static_cast<std::size_t>(column - 1)};
}

CodeEditor::CodeEditor(NodeId id) : Component(id) {
    m_editor->SetAutoIndentEnabled(true);
    m_editor->SetShowMatchingBrackets(true);
    m_editor->SetCompletePairedGlyphs(true);
    m_editor->SetLineNumberLeftMargin(glyphMargin);
    m_editor->SetTextLeftMargin(textMargin);
    // clang-format off
    m_editor->SetChangeCallback([this]() { m_changed = true; }, changeDelayMilliseconds);
    m_editor->SetCustomCaretRenderer([this](const TextEditor::CustomCaret& caret) { drawCaret(caret); });
    m_editor->SetCustomLineNumberRenderer([this](const TextEditor::CustomLineNumber& number) { drawLineNumber(number); });
    // clang-format on
}

std::string_view CodeEditor::kind() const {
    return "codeEditor";
}

Result<void> CodeEditor::command(RenderContext& context, std::string_view name, const json::Json& arguments) {
    std::optional<Error> failure;
    loadPending();

    // Revealing puts the cursor on a position and scrolls it to the middle, which is how a problem, a reference or a search result is opened.
    if (name == "reveal") {
        const TextEditor::DocPos target = position(arguments, "codeEditor.reveal", failure);

        if (failure.has_value()) {
            return Result<void>::failure(*failure);
        }

        m_editor->SetCursor(target);
        m_editor->ScrollToLine(target.line, TextEditor::Scroll::alignMiddle);
        m_editor->SetFocus();
        context.requestFrame();
        return Result<void>::success();
    }

    // Flushing reports an edit the delay of the change event still holds, and answers with a flushed event once the text is current.
    if (name == "flush" && arguments.empty()) {
        m_flushRequested = true;
        context.requestFrame();
        return Result<void>::success();
    }

    // The text a plugin found for a position shows while the pointer still rests there.
    if (name == "hover-text") {
        std::int64_t line = 0;
        std::int64_t column = 0;
        std::string text;
        json::ObjectReader reader(arguments, "codeEditor.hover-text");
        reader.readInteger("line", line, 1, largestPosition).readInteger("column", column, 1, largestPosition).readText("text", text);

        if (const auto finished = reader.finish(); !finished.hasValue()) {
            return finished;
        }

        m_hoverTextPosition = TextEditor::DocPos(static_cast<std::size_t>(line - 1), static_cast<std::size_t>(column - 1));
        m_hoverText = std::move(text);
        context.requestFrame();
        return Result<void>::success();
    }

    // Proposals answer the latest request for completion and open under the cursor, narrowed to what the reader typed since.
    if (name == "suggest") {
        std::int64_t request = 0;
        const json::Json* items = &json::ObjectReader::absent();
        json::ObjectReader reader(arguments, "codeEditor.suggest");
        reader.readInteger("request", request, 1, largestRequest).readArray("items", items);

        if (auto finished = reader.finish(); !finished.hasValue()) {
            return finished;
        }

        auto proposals = CodeCompletion::parse(*items);

        if (!proposals.hasValue()) {
            return Result<void>::failure(proposals.error());
        }

        // Only the latest request is answered, and only once, so proposals that arrive late, after the list closed or after the cursor left their word are dropped.
        const TextEditor::DocPos cursor = m_editor->GetMainCursorPosition();
        const bool latest = request == m_completionNumber && m_completionStart.has_value();
        const bool within = latest && cursor.line == m_completionStart->line && !(cursor < *m_completionStart);

        if (within) {
            m_completion.show(std::move(proposals.value()), *m_completionStart, m_editor->GetSectionText(*m_completionStart, cursor), m_completionAsked);
        }

        if (latest) {
            m_completionStart.reset();
        }

        context.requestFrame();
        return Result<void>::success();
    }

    if (name == "focus" && arguments.empty()) {
        m_editor->SetFocus();
        return Result<void>::success();
    }

    return Component::command(context, name, arguments);
}

// Colors come from the scheme a plugin gives, and otherwise from Dark Modern over the window of the active theme.
TextEditor::Palette CodeEditor::palette(const Theme& theme) const {
    TextEditor::Palette colors{};
    std::vector<std::pair<TextEditor::Color, Color>> painted{{TextEditor::Color::background, theme.color(ThemeColor::Window)}, {TextEditor::Color::matchingBracketBackground, theme.color(ThemeColor::Hover)}, {TextEditor::Color::matchingBracketError, theme.color(ThemeColor::Danger)}};
    std::ranges::copy(DarkModern::colors(), std::back_inserter(painted));
    std::ranges::copy(m_scheme, std::back_inserter(painted));

    for (const auto& [role, color] : painted) {
        colors[static_cast<std::size_t>(role)] = color.packed();
    }

    return colors;
}

Color CodeEditor::background(const Theme& theme) const {
    // clang-format off
    const auto found = std::ranges::find_if(m_scheme, [](const auto& entry) { return entry.first == TextEditor::Color::background; });
    // clang-format on
    return found != m_scheme.end() ? found->second : theme.color(ThemeColor::Window);
}

Alignment CodeEditor::defaultRowAlignment() const {
    return Alignment::Stretch;
}

void CodeEditor::readProperties(json::ObjectReader& reader) {
    if (reader.contains("value")) {
        std::string value;
        reader.read("value", value);
        m_pendingValue = std::move(value);
    }

    readLanguage(reader);
    readScheme(reader);
    readMarkers(reader);
    readHighlights(reader);
    readDecorations(reader);
    readCompletion(reader);

    reader.read("readOnly", m_readOnly, json::Presence::Optional).read("lineNumbers", m_lineNumbers, json::Presence::Optional).read("wordWrap", m_wordWrap, json::Presence::Optional).read("whitespace", m_whitespace, json::Presence::Optional).read("minimap", m_minimap, json::Presence::Optional).read("insertSpaces", m_insertSpaces, json::Presence::Optional);
    reader.readInteger("tabSize", m_tabSize, 1, 16, json::Presence::Optional).readNumber("fontSize", m_fontSize, ContentFontSize::minimum, ContentFontSize::maximum, json::Presence::Optional).read("fontFamily", m_fontFamily, json::Presence::Optional).read("hovers", m_hovers, json::Presence::Optional).read("definitions", m_definitions, json::Presence::Optional);

    if (m_fontFamily.empty() || m_fontFamily.size() > FontFamilies::longestName) {
        fail({"editor_font_family_invalid", "An editor names a font family of a bounded length", "codeEditor.fontFamily"});
    }
}

Component::Restore CodeEditor::keep() {
    return kept(m_pendingValue, m_language, m_defined, m_named, m_languageStale, m_highlightsStale, m_scheme, m_currentLine, m_lineMarkStale, m_occurrence, m_paletteStale, m_markers, m_lineMarkers, m_markersStale, m_highlights, m_decorations, m_completing, m_triggers, m_readOnly, m_lineNumbers, m_wordWrap, m_whitespace, m_minimap, m_insertSpaces, m_tabSize, m_fontSize, m_fontFamily, m_hovers, m_definitions);
}

void CodeEditor::applied() {
    if (m_languageStale) {
        m_editor->SetLanguage(m_defined != nullptr ? m_defined.get() : m_named);
        m_languageStale = false;
    }

    m_editor->SetShowLineNumbersEnabled(m_lineNumbers);
    m_editor->SetWordWrapEnabled(m_wordWrap);
    m_editor->SetShowWhitespacesEnabled(m_whitespace);
    m_editor->SetShowMiniMapEnabled(m_minimap);
    m_editor->SetInsertSpacesOnTabs(m_insertSpaces);
    m_editor->SetTabSize(static_cast<std::size_t>(m_tabSize));

    if (!m_completing) {
        m_completion.close();
    }
}

// A text its plugin gives is loaded when the editor next works, after every other property of its patch, so a refused patch never loads a document and the tab settings of the same patch already hold.
void CodeEditor::loadPending() {
    if (!m_pendingValue.has_value()) {
        return;
    }

    const std::string value = std::move(*m_pendingValue);
    m_pendingValue.reset();

    if (value == m_editor->GetText()) {
        return;
    }

    replaceText(value);
    m_markersStale = true;
    m_highlightsStale = true;
    m_findStale = true;
    m_completion.close();
    m_completionStart.reset();
    m_hoverTextPosition.reset();
    m_hoverText.clear();
}

// A new text keeps the cursor, the selection and the first visible line where the reader had them, as far as the new text reaches, so a file read again from disk stays in place.
void CodeEditor::replaceText(const std::string& value) {
    if (!m_drawnOnce) {
        m_editor->load(value);
        return;
    }

    const TextEditor::DocSelection selection = m_editor->GetMainCursorSelection();
    const TextEditor::DocPos cursor = m_editor->GetMainCursorPosition();
    const std::size_t top = m_editor->VisPos2DocPos(TextEditor::VisPos(m_editor->GetFirstVisibleRow(), 0)).line;
    m_editor->load(value);

    if (selection.start != selection.end) {
        m_editor->SelectRegion(selection.start, selection.end);
    } else {
        m_editor->SetCursor(cursor);
    }

    m_editor->ScrollToLine(top, TextEditor::Scroll::alignTop);
}

// A language is a name the library carries or a definition from the plugin, and the same definition given again changes nothing.
void CodeEditor::readLanguage(json::ObjectReader& reader) {
    if (!reader.contains("language")) {
        return;
    }

    const json::Json* language = &json::ObjectReader::absent();
    reader.readAny("language", language);

    if (*language == m_language) {
        return;
    }

    if (language->is_string()) {
        const auto resolved = CodeLanguage::named(language->get<std::string>());

        if (!resolved.hasValue()) {
            fail(resolved.error());
            return;
        }

        m_named = resolved.value();
        m_defined.reset();
        m_language = *language;
        m_languageStale = true;
        m_highlightsStale = true;
        return;
    }

    if (!language->is_object()) {
        fail({"editor_language_invalid", "A language is a name or a definition", "codeEditor.language"});
        return;
    }

    auto defined = CodeLanguage::defined(*language, "codeEditor.language");

    if (!defined.hasValue()) {
        fail(defined.error());
        return;
    }

    m_defined = std::move(defined.value());
    m_named = nullptr;
    m_language = *language;
    m_languageStale = true;
    m_highlightsStale = true;
}

// A scheme names every syntax and surface color at once, so a document never mixes two schemes.
void CodeEditor::readScheme(json::ObjectReader& reader) {
    if (!reader.contains("scheme")) {
        return;
    }

    const json::Json* scheme = &json::ObjectReader::absent();
    reader.readObject("scheme", scheme);
    json::ObjectReader schemeReader(*scheme, "codeEditor.scheme");
    std::vector<std::pair<TextEditor::Color, Color>> parsed;

    for (const auto& [name, role] : schemeRoles) {
        const auto color = schemeColor(schemeReader, name);

        if (!color.has_value()) {
            return;
        }

        parsed.emplace_back(role, *color);
    }

    const auto currentLine = schemeColor(schemeReader, "currentLine");
    const auto occurrence = schemeColor(schemeReader, "occurrence");

    if (!currentLine.has_value() || !occurrence.has_value()) {
        return;
    }

    if (auto finished = schemeReader.finish(); !finished.hasValue()) {
        fail(finished.error());
        return;
    }

    m_scheme = std::move(parsed);
    m_currentLine = *currentLine;
    m_lineMarkStale = true;
    m_occurrence = *occurrence;
    m_paletteStale = true;
}

// A scheme color is a number sign followed by six hexadecimal digits, and a missing one is reported when the scheme is finished.
std::optional<Color> CodeEditor::schemeColor(json::ObjectReader& reader, std::string_view name) {
    std::string value;
    reader.readText(name, value);
    const auto color = Color::parse(value);

    if (!color.has_value() && !value.empty()) {
        fail({"ui_color_invalid", "A color is written as a number sign followed by six hexadecimal digits", "codeEditor.scheme." + std::string(name)});
        return std::nullopt;
    }

    return color.value_or(Color{});
}

// Markers are kept in the order of their start, and each marked line takes the strongest tone of the markers starting on it.
void CodeEditor::readMarkers(json::ObjectReader& reader) {
    if (!reader.contains("markers")) {
        return;
    }

    const json::Json* markers = &json::ObjectReader::absent();
    reader.readArray("markers", markers);

    if (markers->size() > largestList) {
        fail({"editor_markers_invalid", "The markers of a document are bounded", "codeEditor.markers"});
        return;
    }

    std::vector<Marker> parsed;
    std::map<std::size_t, std::size_t> strongest;

    for (const auto& item : *markers) {
        auto read = marker(item);

        if (!read.hasValue()) {
            fail(read.error());
            return;
        }

        parsed.push_back(std::move(read.value()));
    }

    std::ranges::stable_sort(parsed, {}, &Marker::start);

    // Each line keeps the strongest marker starting on it, the earliest among equals, whose icon leads its number.
    for (std::size_t index = 0; index < parsed.size(); ++index) {
        const auto [found, inserted] = strongest.try_emplace(parsed[index].start.line, index);
        found->second = strength(parsed[index].tone) > strength(parsed[found->second].tone) ? index : found->second;
    }

    m_markers = std::move(parsed);
    m_lineMarkers = std::move(strongest);
    m_markersStale = true;
}

// A marker names its line from one, a tone and a message, and may name the columns it covers from one, a detail and the places related to it.
// A marker without a column covers its whole line, and one whose end comes before its start or that names an end without a column is refused.
Result<CodeEditor::Marker> CodeEditor::marker(const json::Json& item) {
    std::int64_t line = 0;
    std::int64_t column = 0;
    std::int64_t endLine = 0;
    std::int64_t endColumn = 0;
    Marker parsed{{}, std::nullopt, ThemeColor::Danger, {}, {}, {}};
    const json::Json* related = &json::ObjectReader::emptyList();
    json::ObjectReader reader(item, "codeEditor.markers");
    reader.readInteger("line", line, 1, largestPosition).readInteger("column", column, 1, largestPosition, json::Presence::Optional).readInteger("endLine", endLine, 1, largestPosition, json::Presence::Optional).readInteger("endColumn", endColumn, 1, largestPosition, json::Presence::Optional);
    reader.readChoice("tone", parsed.tone, {{"danger", ThemeColor::Danger}, {"warning", ThemeColor::Warning}, {"information", ThemeColor::Information}}).readText("message", parsed.message).readText("detail", parsed.detail, json::Presence::Optional).readArray("related", related, json::Presence::Optional);

    if (auto finished = reader.finish(); !finished.hasValue()) {
        return Result<Marker>::failure(finished.error());
    }

    parsed.start = {static_cast<std::size_t>(line - 1), static_cast<std::size_t>(std::max<std::int64_t>(column - 1, 0))};

    if (column != 0) {
        parsed.end = TextEditor::DocPos(static_cast<std::size_t>((endLine == 0 ? line : endLine) - 1), static_cast<std::size_t>((endColumn == 0 ? column : endColumn) - 1));
    }

    if ((column == 0 && (endLine != 0 || endColumn != 0)) || (parsed.end.has_value() && *parsed.end < parsed.start)) {
        return Result<Marker>::failure({"editor_marker_range", "A marker ends where it starts or later and names its end only after a column", "codeEditor.markers"});
    }

    if (related->size() > largestRelated) {
        return Result<Marker>::failure({"editor_marker_related", "The places related to a marker are bounded", "codeEditor.markers.related"});
    }

    for (const auto& entry : *related) {
        Related place;
        json::ObjectReader placeReader(entry, "codeEditor.markers.related");
        placeReader.readText("place", place.place).readText("message", place.message);

        if (auto finished = placeReader.finish(); !finished.hasValue()) {
            return Result<Marker>::failure(finished.error());
        }

        parsed.related.push_back(std::move(place));
    }

    return Result<Marker>::success(std::move(parsed));
}

int CodeEditor::strength(ThemeColor tone) {
    return tone == ThemeColor::Danger ? 2 : tone == ThemeColor::Warning ? 1 : 0;
}

Icon CodeEditor::icon(ThemeColor tone) {
    return tone == ThemeColor::Danger ? Icon::Error : tone == ThemeColor::Warning ? Icon::Warning : Icon::Information;
}

std::string CodeEditor::where(const RenderContext& context, const Marker& marker) {
    const std::string line = std::to_string(marker.start.line + 1);

    if (!marker.end.has_value()) {
        return context.localization().translate("workpane.editor.line", std::array{line});
    }

    return context.localization().translate("workpane.editor.position", std::array{line, std::to_string(marker.start.index + 1)});
}

// A highlight names a line and a column from one, a length in characters and the syntax role it is colored with.
void CodeEditor::readHighlights(json::ObjectReader& reader) {
    if (!reader.contains("highlights")) {
        return;
    }

    const json::Json* highlights = &json::ObjectReader::absent();
    reader.readArray("highlights", highlights);

    if (highlights->size() > largestList) {
        fail({"editor_highlights_invalid", "The highlights of a document are bounded", "codeEditor.highlights"});
        return;
    }

    std::vector<CodeText::Highlight> parsed;

    for (const auto& item : *highlights) {
        std::int64_t line = 0;
        std::int64_t column = 0;
        std::int64_t length = 0;
        std::string name;
        json::ObjectReader highlightReader(item, "codeEditor.highlights");
        highlightReader.readInteger("line", line, 1, largestPosition).readInteger("column", column, 1, largestPosition).readInteger("length", length, 1, static_cast<std::int64_t>(largestList)).readText("role", name);

        if (auto finished = highlightReader.finish(); !finished.hasValue()) {
            fail(finished.error());
            return;
        }

        const auto color = role(name, syntaxRoles);

        if (!color.has_value()) {
            fail({"editor_highlight_invalid", "A highlight names a syntax role", "codeEditor.highlights"});
            return;
        }

        parsed.push_back({static_cast<std::size_t>(line - 1), static_cast<std::size_t>(column - 1), static_cast<std::size_t>(length), *color});
    }

    m_highlights = std::move(parsed);
    m_highlightsStale = true;
}

// A decoration marks a range: the other uses of a symbol, code that has no effect drawn faded, or code no longer to be used struck through.
void CodeEditor::readDecorations(json::ObjectReader& reader) {
    if (!reader.contains("decorations")) {
        return;
    }

    const json::Json* decorations = &json::ObjectReader::absent();
    reader.readArray("decorations", decorations);

    if (decorations->size() > largestList) {
        fail({"editor_decorations_invalid", "The decorations of a document are bounded", "codeEditor.decorations"});
        return;
    }

    std::vector<Decoration> parsed;

    for (const auto& item : *decorations) {
        std::int64_t line = 0;
        std::int64_t column = 0;
        std::int64_t endLine = 0;
        std::int64_t endColumn = 0;
        std::string name;
        json::ObjectReader decorationReader(item, "codeEditor.decorations");
        decorationReader.readInteger("line", line, 1, largestPosition).readInteger("column", column, 1, largestPosition).readInteger("endLine", endLine, 1, largestPosition).readInteger("endColumn", endColumn, 1, largestPosition).readText("style", name);

        if (auto finished = decorationReader.finish(); !finished.hasValue()) {
            fail(finished.error());
            return;
        }

        // clang-format off
        const auto style = std::ranges::find_if(decorationStyles, [&name](const auto& entry) { return entry.first == name; });
        // clang-format on

        if (style == decorationStyles.end() || endLine < line || (endLine == line && endColumn < column)) {
            fail({"editor_decoration_invalid", "A decoration names a style and a range that does not end before it starts", name});
            return;
        }

        parsed.push_back({TextEditor::DocPos(static_cast<std::size_t>(line - 1), static_cast<std::size_t>(column - 1)), TextEditor::DocPos(static_cast<std::size_t>(endLine - 1), static_cast<std::size_t>(endColumn - 1)), style->second});
    }

    m_decorations = std::move(parsed);
}

// Completion is asked for as the reader types a word, types a character the server names or presses the completion keys, and the characters that ask are a short bounded list.
void CodeEditor::readCompletion(json::ObjectReader& reader) {
    std::vector<std::string> triggers = m_triggers;
    reader.read("completion", m_completing, json::Presence::Optional).read("completionTriggers", triggers, json::Presence::Optional);

    // clang-format off
    if (triggers.size() > largestTriggers || std::ranges::any_of(triggers, [](const std::string& trigger) { return trigger.empty() || trigger.size() > 4; })) {
        fail({"editor_triggers_invalid", "The characters that ask for completion are a short list of short texts", "codeEditor.completionTriggers"});
        return;
    }
    // clang-format on

    m_triggers = std::move(triggers);
}

ImVec2 CodeEditor::measureContent(RenderContext& context, float availableWidth) {
    return {availableWidth, editorMinimumHeight * context.scale()};
}

void CodeEditor::render(RenderContext& context, const ImRect& bounds) {
    loadPending();

    // The underlines take the tones of the theme, so they are drawn again whenever the palette changes.
    if (m_paletteStale || m_paletteTheme != &context.theme()) {
        m_editor->SetPalette(palette(context.theme()));
        m_paletteTheme = &context.theme();
        m_paletteStale = false;
        m_markersStale = true;
    }

    if (m_markersStale) {
        applyMarkers(context);
    }

    // Highlights are painted again after an edit, because the lines that changed were colored by the language alone.
    const bool edited = m_editor->GetUndoIndex() != m_paintedUndo || m_editor->GetLineCount() != m_paintedLines;

    if (m_highlightsStale || (edited && !m_highlights.empty())) {
        m_editor->paint(m_highlights, m_highlightsStale);
        m_highlightsStale = false;
        m_paintedUndo = m_editor->GetUndoIndex();
        m_paintedLines = m_editor->GetLineCount();
    }

    m_rendering = &context;
    m_drawn = true;
    m_drawnOnce = true;
    m_editor->SetReadOnlyEnabled(m_readOnly || !common().enabled);
    markCurrentLine();

    // The keys that choose a proposal are taken from the editor while the list is open, so the caret stays where it is.
    const bool typed = m_typing && !ImGui::GetIO().InputQueueCharacters.empty();

    if (m_completion.open() && m_typing) {
        holdCompletionKeys();
    }

    {
        context.families().request(m_fontFamily);
        const FontScope font(context.fonts(), FontFace::Monospace, static_cast<float>(m_fontSize), m_fontFamily);
        // A row is one and a half em rounded to a whole pixel, as in Visual Studio Code, whatever height ImGui rounded the face to.
        m_editor->SetLineSpacing(std::round(lineHeight * static_cast<float>(m_fontSize) * context.scale()) / ImGui::GetFontSize());
        ImGui::SetCursorScreenPos(bounds.Min);
        // The library outlines a focused editor in the navigation color, and the product draws no focus around a text it edits.
        ImGui::PushStyleColor(ImGuiCol_NavCursor, IM_COL32(0, 0, 0, 0));
        m_findStale = m_editor->Render("##editor", bounds.GetSize()) || m_findStale;
        ImGui::PopStyleColor();
        m_editor->settleLoad();
    }

    // The editor is the child window its library opened last, which has the keyboard while the navigation rests in it and the pointer only while no bar or list drawn over it does.
    const ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGuiWindow* editorWindow = window->DC.ChildWindows.empty() ? nullptr : window->DC.ChildWindows.back();
    const bool typing = editorWindow != nullptr && GImGui->NavWindow == editorWindow;
    const bool pointed = editorWindow != nullptr && GImGui->HoveredWindow == editorWindow && common().enabled;
    m_rendering = nullptr;

    if (editorWindow != nullptr) {
        drawDecorations(*editorWindow, background(context.theme()));
    }

    // The library reports an edit only while it draws, so a frame is asked for at the moment the report is due.
    if (m_editor->changePending()) {
        context.requestFrameAt(context.time() + m_editor->secondsUntilChange());
    }

    findKeys(context, typing);
    definitionClick(context, pointed);
    menu(context, pointed);
    followCompletion(typing && common().enabled, typed);
    completionKeys(context);
    m_typing = typing;

    // The list stands under the caret, which the last frame placed.
    const std::vector<ImRect> caret = m_editor->rangeRects(m_editor->GetMainCursorPosition(), m_editor->GetMainCursorPosition());

    if (m_completion.open() && caret.empty()) {
        m_completion.close();
    }

    if (const bool clicked = !caret.empty() && m_completion.draw(context, caret.front()); clicked && m_completion.chosen() != nullptr) {
        accept(*m_completion.chosen());
    }

    if (m_finder.active() && std::exchange(m_findStale, false)) {
        refreshFind();
    }

    if (m_finder.active()) {
        drawFind(context, bounds);
    }

    // A search still reading the document goes on at every frame and brings its match into view once it finds it.
    if (m_finder.searching()) {
        if (m_finder.advance(*m_editor)) {
            showMatch();
        }

        context.requestFrame();
    }

    hover(context, pointed);
    applyZoom(context, typing && common().enabled);
    report(context);

    if (m_changed) {
        m_changed = false;
        context.emit(id(), "change", {{"value", m_editor->GetText()}}, {{"value", "value"}});
    }
}

// The current line is filled under its text on every row it wraps to while nothing is selected, through the line marker of the library, which paints under the text.
void CodeEditor::markCurrentLine() {
    const std::optional<std::size_t> wanted = m_editor->CurrentCursorHasSelection() ? std::nullopt : std::optional<std::size_t>(m_editor->GetMainCursorPosition().line);
    const std::size_t undo = m_editor->GetUndoIndex();

    if (!m_lineMarkStale && wanted == m_markedLine && undo == m_markedUndo) {
        return;
    }

    m_editor->ClearMarkers();

    if (wanted.has_value()) {
        m_editor->AddMarker(*wanted, 0, m_currentLine.packed(), "", "");
    }

    m_markedLine = wanted;
    m_markedUndo = undo;
    m_lineMarkStale = false;
}

// An edit the change event has not reported yet is reported at once when the plugin flushes or when the editor is no longer drawn, so a hidden document never holds text its plugin has not seen.
void CodeEditor::update(RenderContext& context) {
    const bool hidden = !std::exchange(m_drawn, false);
    loadPending();

    if (m_editor->changePending() && (m_flushRequested || hidden)) {
        m_editor->settleChange();
        context.emit(id(), "change", {{"value", m_editor->GetText()}}, {{"value", "value"}});
    }

    if (std::exchange(m_flushRequested, false)) {
        context.emit(id(), "flushed", nlohmann::json::object());
    }
}

// The reading size belongs to the surface being read, so the zoom keys answer only while the editor has the keyboard.
void CodeEditor::applyZoom(RenderContext& context, bool typing) {
    if (!typing) {
        return;
    }

    const auto step = ContentFontSize::shortcutStep();

    if (!step.has_value()) {
        return;
    }

    m_fontSize = ContentFontSize::stepped(m_fontSize, *step);
    context.emit(id(), "zoom", {{"fontSize", m_fontSize}}, {{"fontSize", "fontSize"}});
}

// Opening find starts from the selected text when it stays on one line, and the first match at or after the cursor is the one read.
void CodeEditor::openFind(RenderContext& context) {
    const TextEditor::DocSelection selection = m_editor->GetMainCursorSelection();

    if (selection.start.line == selection.end.line && selection.start != selection.end) {
        m_findBar.setQuery(m_editor->GetSectionText(selection), m_findBar.caseSensitive(), m_findBar.wholeWord());
    }

    m_findBar.focus();
    m_finder.search(*m_editor, m_findBar.text(), m_findBar.caseSensitive(), m_findBar.wholeWord(), selection.start);
    showMatch();
    context.requestFrame();
}

// An edit made while the bar is open finds the matches again from the one being read, so the count stays true and a replacement never lands on text that moved.
void CodeEditor::refreshFind() {
    const auto current = m_finder.current();
    m_finder.search(*m_editor, m_findBar.text(), m_findBar.caseSensitive(), m_findBar.wholeWord(), current.has_value() ? current->start : m_editor->GetMainCursorPosition());
}

// The find keys answer while the editor has the keyboard: mod and F open find, and the find keys of the platform step through an open search.
void CodeEditor::findKeys(RenderContext& context, bool typing) {
    if (!typing) {
        return;
    }

    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F)) {
        openFind(context);
        return;
    }

    if (const int step = m_finder.active() ? FindBar::stepping() : 0; step != 0) {
        m_finder.step(step);
        showMatch();
    }
}

// The bar is a window of its own over the editor, so it draws above the text and takes the pointer there.
void CodeEditor::drawFind(RenderContext& context, const ImRect& bounds) {
    const bool replacing = !m_editor->IsReadOnlyEnabled();
    const ImRect area = FindBar::area(context, bounds, replacing);
    ImGui::SetCursorScreenPos(area.Min);
    FindBar::Outcome outcome;

    if (ImGui::BeginChild("##find", area.GetSize(), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground)) {
        outcome = m_findBar.draw(context, bounds, {m_finder.count(), m_finder.position(), m_finder.bounded()}, replacing);
    }

    ImGui::EndChild();

    if (outcome.searched) {
        m_finder.search(*m_editor, m_findBar.text(), m_findBar.caseSensitive(), m_findBar.wholeWord(), m_editor->GetMainCursorSelection().start);
        showMatch();
    }

    if (outcome.step != 0) {
        m_finder.step(outcome.step);
        showMatch();
    }

    if (outcome.replaced || outcome.replacedAll) {
        replaceMatches(outcome.replacedAll);
    }

    if (outcome.closed) {
        m_finder.close();
        m_editor->SetFocus();
    }
}

// Replacing one match writes the replacement over the match being read and moves to the next, and replacing every match is one step of the undo history.
void CodeEditor::replaceMatches(bool every) {
    m_finder.finish(*m_editor);
    const auto current = m_finder.current();

    if (!current.has_value() || m_editor->IsReadOnlyEnabled()) {
        return;
    }

    if (every) {
        m_editor->replaceEvery(m_finder.matches(), m_findBar.replacement());
    }

    if (!every) {
        m_editor->ReplaceSectionText(*current, m_findBar.replacement());
    }

    m_finder.search(*m_editor, m_findBar.text(), m_findBar.caseSensitive(), m_findBar.wholeWord(), m_editor->GetMainCursorPosition());
    showMatch();
}

void CodeEditor::showMatch() {
    const auto current = m_finder.current();

    if (!current.has_value()) {
        return;
    }

    const std::size_t first = m_editor->VisPos2DocPos(TextEditor::VisPos(m_editor->GetFirstVisibleRow(), 0)).line;
    const std::size_t last = m_editor->VisPos2DocPos(TextEditor::VisPos(m_editor->GetLastVisibleRow(), 0)).line;
    m_editor->SelectRegion(current->start, current->end);

    if (current->start.line < first || current->start.line >= last) {
        m_editor->ScrollToLine(current->start.line, TextEditor::Scroll::alignMiddle);
    }
}

bool CodeEditor::wordCharacter(ImWchar character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') || character == '_' || character > 0x7F;
}

void CodeEditor::holdCompletionKeys() {
    const ImGuiID owner = ImGui::GetID("##completionKeys");

    for (const ImGuiKey key : completionKeyList) {
        ImGui::SetKeyOwner(key, owner, ImGuiInputFlags_LockThisFrame);
    }
}

// While the list is open the arrows and the page keys choose, Enter and Tab accept, and Escape closes it.
void CodeEditor::completionKeys(RenderContext& context) {
    if (!m_completion.open() || !m_typing) {
        return;
    }

    const ImGuiID owner = ImGui::GetID("##completionKeys");
    // clang-format off
    const auto pressed = [owner](ImGuiKey key) { return ImGui::IsKeyPressed(key, ImGuiInputFlags_Repeat, owner); };
    // clang-format on
    const int rows = static_cast<int>(CodeCompletion::shownRows);
    const int direction = pressed(ImGuiKey_UpArrow) ? -1 : pressed(ImGuiKey_DownArrow) ? 1 : pressed(ImGuiKey_PageUp) ? -rows : pressed(ImGuiKey_PageDown) ? rows : 0;
    const bool closing = pressed(ImGuiKey_Escape);
    const bool accepting = pressed(ImGuiKey_Enter) || pressed(ImGuiKey_Tab);

    // Only a key the list answered changes what it shows, so an open list that waits asks for no frame.
    if (direction == 0 && !closing && !accepting) {
        return;
    }

    context.requestFrame();
    m_completion.step(direction);

    if (closing) {
        m_completion.close();
        return;
    }

    if (accepting) {
        if (const CodeCompletion::Proposal* proposal = m_completion.chosen(); proposal != nullptr) {
            accept(*proposal);
        }

        m_completion.close();
    }
}

// An open list follows the word being typed and closes when the cursor leaves it, and a closed one is asked for by a word, by a character the server names or by the completion keys.
void CodeEditor::followCompletion(bool typing, bool typed) {
    if (!m_completing) {
        return;
    }

    const TextEditor::DocPos cursor = m_editor->GetMainCursorPosition();

    if (m_completion.open()) {
        const TextEditor::DocPos start = m_completion.start();
        const std::string word = cursor.line == start.line && start < cursor ? m_editor->GetSectionText(start, cursor) : std::string();
        // clang-format off
        const bool inside = cursor.line == start.line && !(cursor < start) && std::ranges::all_of(word, [](char character) { return wordCharacter(static_cast<unsigned char>(character)); });
        // clang-format on

        if (!inside) {
            m_completion.close();
            return;
        }

        m_completion.narrow(word);
        return;
    }

    const bool asking = typing && ImGui::IsKeyChordPressed(ImGui::GetIO().ConfigMacOSXBehaviors ? ImGuiMod_Super | ImGuiKey_Space : ImGuiMod_Ctrl | ImGuiKey_Space);

    if (asking) {
        askCompletion(m_editor->FindWordStart(cursor, true), true);
        return;
    }

    if (!typed || cursor.index == 0) {
        return;
    }

    const std::string before = m_editor->GetSectionText(TextEditor::DocPos(cursor.line, 0), cursor);
    // clang-format off
    const bool triggered = std::ranges::any_of(m_triggers, [&before](const std::string& trigger) { return before.ends_with(trigger); });
    // clang-format on

    if (triggered) {
        askCompletion(cursor, false);
        return;
    }

    if (!before.empty() && wordCharacter(static_cast<unsigned char>(before.back()))) {
        askCompletion(m_editor->FindWordStart(cursor, true), false);
    }
}

void CodeEditor::askCompletion(TextEditor::DocPos start, bool asked) {
    const TextEditor::DocPos cursor = m_editor->GetMainCursorPosition();
    m_completionStart = start;
    m_completionAsked = asked;
    ++m_completionNumber;
    m_completionRequest = nlohmann::json{{"request", m_completionNumber}, {"line", cursor.line + 1}, {"column", cursor.index + 1}, {"word", start < cursor ? m_editor->GetSectionText(start, cursor) : std::string()}};
}

// A proposal replaces the range its server gave, stretched to the cursor when the reader typed past it, or the word under the cursor.
// A range the cursor no longer stands at the end of, or that the document no longer holds, is dropped instead of editing text the reader never meant.
void CodeEditor::accept(const CodeCompletion::Proposal& proposal) {
    const TextEditor::DocPos cursor = m_editor->GetMainCursorPosition();
    const TextEditor::DocPos from = proposal.range.has_value() ? proposal.range->start : m_completion.start();
    TextEditor::DocPos to = proposal.range.has_value() ? proposal.range->end : cursor;
    const std::string insert = proposal.insert;
    m_completion.close();

    if (to.line == cursor.line && to < cursor) {
        to = cursor;
    }

    if (from.line != cursor.line || cursor < from || to < from || !m_editor->holds(from) || !m_editor->holds(to)) {
        return;
    }

    m_editor->ReplaceSectionText(from, to, insert);
    m_findStale = true;
}

// A secondary click moves the caret where it landed unless it landed in the selection, and opens the edit actions followed by the actions of the plugin.
void CodeEditor::menu(RenderContext& context, bool pointed) {
    const bool clicked = pointed && ImGui::IsMouseClicked(ImGuiMouseButton_Right);

    if (clicked && context.claimMenuClick()) {
        const TextEditor::DocPos landed = m_editor->GetDocPosAtMousePos(ImGui::GetIO().MousePos);
        const TextEditor::DocSelection selection = m_editor->GetMainCursorSelection();

        if (landed < selection.start || selection.end < landed || selection.start == selection.end) {
            m_editor->SetCursor(landed);
        }

        // The clipboard is read once as the menu opens, since reading it on X11 waits for the program that owns it.
        const char* clipboard = ImGui::GetClipboardText();
        m_pasteable = clipboard != nullptr && *clipboard != '\0';
        ImGui::OpenPopup(menuName);
    }

    if (!ImGui::IsPopupOpen(menuName)) {
        return;
    }

    const bool writable = !m_editor->IsReadOnlyEnabled();
    const bool selected = m_editor->AnyCursorHasSelection();
    std::vector<MenuItem> items{{"undo", TextValue::translated("workpane.editor.undo"), std::nullopt, {}, m_editor->CanUndo(), false, false}, {"redo", TextValue::translated("workpane.editor.redo"), std::nullopt, {}, m_editor->CanRedo(), false, false}, {{}, {}, std::nullopt, {}, true, true, false}, {"cut", TextValue::translated("workpane.editor.cut"), std::nullopt, {}, writable && selected, false, false}, {"copy", TextValue::translated("workpane.editor.copy"), std::nullopt, {}, selected, false, false}, {"paste", TextValue::translated("workpane.editor.paste"), std::nullopt, {}, writable && m_pasteable, false, false}, {{}, {}, std::nullopt, {}, true, true, false}, {"select-all", TextValue::translated("workpane.editor.select-all"), std::nullopt, {}, true, false, false}};

    if (!common().menu.empty()) {
        items.push_back({{}, {}, std::nullopt, {}, true, true, false});
        items.insert(items.end(), common().menu.begin(), common().menu.end());
    }

    const auto picked = Menus::popup(context, menuName, items);

    if (!picked.has_value()) {
        return;
    }

    // clang-format off
    const std::array<std::pair<std::string_view, std::function<void()>>, 6> actions{{{"undo", [this]() { m_editor->Undo(); }}, {"redo", [this]() { m_editor->Redo(); }}, {"cut", [this]() { m_editor->Cut(); }}, {"copy", [this]() { m_editor->Copy(); }}, {"paste", [this]() { m_editor->Paste(); }}, {"select-all", [this]() { m_editor->SelectAll(); }}}};
    // clang-format on

    for (const auto& [name, perform] : actions) {
        if (name == *picked) {
            perform();
            m_findStale = true;
            return;
        }
    }

    context.emit(id(), "menu", {{"item", *picked}});
}

// With definitions offered, a click on the text while the modifier is down asks for the definition of what was clicked instead of adding a cursor.
void CodeEditor::definitionClick(RenderContext& context, bool pointed) {
    const bool pointing = pointed && m_definitions && ImGui::GetIO().KeyCtrl && m_editor->IsMousePosOverTextArea(ImGui::GetIO().MousePos);

    if (!pointing) {
        return;
    }

    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        return;
    }

    const TextEditor::DocPos clicked = m_editor->GetDocPosAtMousePos(ImGui::GetIO().MousePos);
    m_editor->SetCursor(clicked);
    context.emit(id(), "definition-request", {{"line", clicked.line + 1}, {"column", clicked.index + 1}});
}

// The other uses of a symbol are tinted, faded code is covered halfway by the background and struck code is crossed at the middle of its rows, over the text only and never over the numbers when the text scrolls sideways.
void CodeEditor::drawDecorations(ImGuiWindow& editorWindow, Color background) const {
    ImDrawList& list = *editorWindow.DrawList;
    const TextEditor::Palette& palette = m_editor->GetPalette();
    const ImU32 faded = WidgetHelper::ink(background.withAlpha(fadedShare));
    list.PushClipRect(ImVec2(editorWindow.Pos.x + m_editor->textLeft(), editorWindow.InnerClipRect.Min.y), editorWindow.InnerClipRect.Max, false);

    for (const Decoration& decoration : m_decorations) {
        for (const ImRect& rect : m_editor->rangeRects(decoration.start, decoration.end)) {
            if (decoration.style == DecorationStyle::Occurrence) {
                list.AddRectFilled(rect.Min, rect.Max, WidgetHelper::ink(m_occurrence.withAlpha(occurrenceShare)));
            }

            if (decoration.style == DecorationStyle::Faded) {
                list.AddRectFilled(rect.Min, rect.Max, faded);
            }

            if (decoration.style == DecorationStyle::Struck) {
                list.AddLine(ImVec2(rect.Min.x, rect.GetCenter().y), ImVec2(rect.Max.x, rect.GetCenter().y), palette.get(TextEditor::Color::text));
            }
        }
    }

    list.PopClipRect();
}

// Each marker underlines its range in its tone, the weaker tones first so a stronger one wins where two overlap.
void CodeEditor::applyMarkers(RenderContext& context) {
    std::vector<const Marker*> ordered;

    for (const Marker& marker : m_markers) {
        ordered.push_back(&marker);
    }

    // clang-format off
    std::ranges::stable_sort(ordered, {}, [](const Marker* marker) { return strength(marker->tone); });
    // clang-format on
    m_editor->ClearSquiggles();

    for (const Marker* marker : ordered) {
        const auto [start, end] = underline(*marker);
        m_editor->AddSquiggle(start, end, 0, context.color(marker->tone).packed());
    }

    m_markersStale = false;
}

// A marker underlines its range, a marker of a whole line the text after its indentation and an empty range the word at its start or the glyph there.
std::pair<TextEditor::DocPos, TextEditor::DocPos> CodeEditor::underline(const Marker& marker) const {
    if (!marker.end.has_value()) {
        const std::string line = m_editor->GetLineText(marker.start.line);
        // clang-format off
        const auto glyphs = static_cast<std::size_t>(std::ranges::count_if(line, [](char byte) { return (static_cast<unsigned char>(byte) & 0xC0U) != 0x80U; }));
        // clang-format on
        const std::size_t indentation = std::min(line.find_first_not_of(" \t"), glyphs);
        return {{marker.start.line, indentation}, {marker.start.line, glyphs}};
    }

    if (*marker.end != marker.start) {
        return {marker.start, *marker.end};
    }

    const TextEditor::DocPos start = m_editor->FindWordStart(marker.start, true);
    const TextEditor::DocPos end = m_editor->FindWordEnd(marker.start, true);
    return {start, end == start ? TextEditor::DocPos(start.line, start.index + 1) : end};
}

// The cursor is reported whenever it moves or its selection changes, with the selected text of a single line, and the requests raised while drawing follow it.
void CodeEditor::report(RenderContext& context) {
    const TextEditor::DocPos cursor = m_editor->GetMainCursorPosition();
    std::string selection;

    if (m_editor->CurrentCursorHasSelection()) {
        const TextEditor::DocSelection selected = m_editor->GetMainCursorSelection();
        selection = selected.start.line == selected.end.line ? m_editor->GetSectionText(selected) : std::string();
        selection = selection.size() > largestSelection ? std::string() : selection;
    }

    if (!m_reportedCursor.has_value() || *m_reportedCursor != cursor || selection != m_reportedSelection) {
        m_caretSince = context.time();
        m_reportedCursor = cursor;
        m_reportedSelection = selection;
        context.emit(id(), "cursor", {{"line", cursor.line + 1}, {"column", cursor.index + 1}, {"selection", selection}});
    }

    if (m_completionRequest.has_value()) {
        context.emit(id(), "complete-request", std::exchange(m_completionRequest, std::nullopt).value());
    }

    if (m_hoverRequest.has_value()) {
        context.emit(id(), "hover", std::exchange(m_hoverRequest, std::nullopt).value());
    }
}

// One tooltip of the product explains what the pointer rests on: the markers starting on a line on its number, and on the text the markers covering the glyph with the text a plugin found for its word.
void CodeEditor::hover(RenderContext& context, bool pointed) {
    const std::optional<std::size_t> gutter = std::exchange(m_gutterLine, std::nullopt);

    if (gutter.has_value()) {
        m_hoverPosition.reset();

        if (const auto markers = starting(*gutter); !markers.empty()) {
            drawTooltip(context, markers, {});
        }

        return;
    }

    const ImVec2 pointer = ImGui::GetMousePos();

    if (!pointed || !m_editor->IsMousePosOverTextArea(pointer) || !m_editor->IsMousePosOverGlyph(pointer)) {
        m_hoverPosition.reset();
        return;
    }

    const TextEditor::DocPos glyph = m_editor->GetDocPosAtMousePos(pointer);
    const TextEditor::DocPos word = m_editor->FindWordStart(glyph, true);

    if (!m_hoverPosition.has_value() || *m_hoverPosition != word) {
        m_hoverPosition = word;
        m_hoverSince = context.time();
        m_hoverReported = false;
    }

    if (context.time() - m_hoverSince < hoverDelaySeconds) {
        context.requestFrameAt(m_hoverSince + hoverDelaySeconds);
        return;
    }

    if (m_hovers && !m_hoverReported) {
        m_hoverReported = true;
        m_hoverRequest = nlohmann::json{{"line", word.line + 1}, {"column", word.index + 1}, {"word", m_editor->GetWordAtMousePos(pointer)}};
    }

    const auto markers = covering(glyph);
    const bool answered = m_hoverTextPosition.has_value() && *m_hoverTextPosition == word;

    if (markers.empty() && !answered) {
        return;
    }

    drawTooltip(context, markers, answered ? std::string_view(m_hoverText) : std::string_view());
}

std::vector<const CodeEditor::Marker*> CodeEditor::starting(std::size_t line) const {
    std::vector<const Marker*> found;

    for (const Marker& marker : m_markers) {
        if (marker.start.line > line) {
            break;
        }

        if (marker.start.line == line) {
            found.push_back(&marker);
        }
    }

    return found;
}

// A marker covers the glyphs of its range, the word at its start when its range is empty and its whole line when it names no column.
std::vector<const CodeEditor::Marker*> CodeEditor::covering(TextEditor::DocPos glyph) const {
    const TextEditor::DocPos word = m_editor->FindWordStart(glyph, true);
    std::vector<const Marker*> found;

    for (const Marker& marker : m_markers) {
        if (marker.start.line > glyph.line) {
            break;
        }

        const bool line = !marker.end.has_value() && marker.start.line == glyph.line;
        const bool empty = marker.end.has_value() && *marker.end == marker.start && m_editor->FindWordStart(marker.start, true) == word;
        const bool range = marker.end.has_value() && marker.start <= glyph && glyph < *marker.end;

        if (line || empty || range) {
            found.push_back(&marker);
        }
    }

    return found;
}

// The markers are listed between dividers, each with the icon of its tone, its message, its detail beside its position and the places related to it, and the text a plugin found closes the list.
void CodeEditor::drawTooltip(const RenderContext& context, const std::vector<const Marker*>& markers, std::string_view text) const {
    if (!WidgetHelper::beginTooltip(context)) {
        return;
    }

    const FontRole role = context.font(ThemeFont::Interface);
    const float gap = tooltipSpacing * context.scale();
    const float glyphs = WidgetHelper::fontSize(context, role);
    const float line = WidgetHelper::lineHeight(context, role);
    const float indent = glyphs + gap;
    const float lift = (line - glyphs) / 2.0F;
    const Color ink = context.color(ThemeColor::OnTooltip);
    const Color muted = context.color(ThemeColor::OnTooltipMuted);
    std::vector<std::string> positions;
    float width = text.empty() ? 0.0F : WidgetHelper::textSize(context, role, text).x;

    // The tooltip is as wide as its widest line up to the width of every tooltip, and longer lines wrap inside it.
    for (const Marker* marker : markers) {
        positions.push_back(where(context, *marker));
        const float detail = marker->detail.empty() ? 0.0F : WidgetHelper::textSize(context, role, marker->detail).x + gap;
        width = std::max({width, indent + WidgetHelper::textSize(context, role, marker->message).x, indent + detail + WidgetHelper::textSize(context, role, positions.back()).x});

        for (const Related& related : marker->related) {
            width = std::max(width, indent + WidgetHelper::textSize(context, role, related.place).x + gap + WidgetHelper::textSize(context, role, related.message).x);
        }
    }

    width = std::ceil(std::min(width, WidgetHelper::tooltipWidth(context)));
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float bleed = ImGui::GetCurrentWindow()->WindowPadding.x;
    // clang-format off
    const auto divide = [&](float top) {
        Painter::horizontalDivider(list, ImVec2(origin.x - bleed, top + gap), width + bleed * 2.0F, context.metric(ThemeMetric::LineWidth), context.color(ThemeColor::Border));

        return top + gap * 2.0F + context.metric(ThemeMetric::LineWidth);
    };
    // clang-format on

    const float left = origin.x + indent;
    const float room = width - indent;
    float y = origin.y;

    for (std::size_t index = 0; index < markers.size(); ++index) {
        const Marker& marker = *markers[index];
        y = index == 0 ? y : divide(y);
        IconCatalog::draw(context.fonts(), list, icon(marker.tone), ImVec2(origin.x, y + lift), glyphs, context.color(marker.tone));
        const float message = WidgetHelper::paragraphSize(context, role, marker.message, room).y;
        WidgetHelper::paragraph(context, list, role, ImRect(left, y, left + room, y + message), ink, marker.message, TextAlign::Start);
        y += message;

        const float located = WidgetHelper::textSize(context, role, positions[index]).x;
        const std::string detail = marker.detail.empty() ? std::string() : WidgetHelper::elided(context, role, marker.detail, std::max(0.0F, room - located - gap));
        const float detailWidth = detail.empty() ? 0.0F : WidgetHelper::textSize(context, role, detail).x + gap;
        WidgetHelper::text(context, list, role, ImVec2(left, y + lift), muted, detail);
        WidgetHelper::text(context, list, role, ImVec2(left + detailWidth, y + lift), muted, positions[index]);
        y += line;

        // A place keeps its whole name while the tooltip has room for it, and gives up at most half of a full tooltip.
        for (const Related& related : marker.related) {
            const std::string place = WidgetHelper::elided(context, role, related.place, std::max(room / 2.0F, room - gap - WidgetHelper::textSize(context, role, related.message).x));
            const float start = left + WidgetHelper::textSize(context, role, place).x + gap;
            const float height = WidgetHelper::paragraphSize(context, role, related.message, left + room - start).y;
            WidgetHelper::text(context, list, role, ImVec2(left, y + lift), muted, place);
            WidgetHelper::paragraph(context, list, role, ImRect(start, y, left + room, y + height), ink, related.message, TextAlign::Start);
            y += height;
        }
    }

    if (!text.empty()) {
        y = markers.empty() ? y : divide(y);
        const float height = WidgetHelper::paragraphSize(context, role, text, width).y;
        WidgetHelper::paragraph(context, list, role, ImRect(origin.x, y, origin.x + width, y + height), ink, text, TextAlign::Start);
        y += height;
    }

    ImGui::Dummy(ImVec2(width, y - origin.y));
    WidgetHelper::endTooltip();
}

// The caret is a bar as wide as the caret of the theme, which is the line caret of Visual Studio Code, and it blinks from the moment it last moved, waking the loop only when it turns on or off.
void CodeEditor::drawCaret(const TextEditor::CustomCaret& caret) const {
    RenderContext& context = *m_rendering;
    const double phase = std::fmod(context.time() - m_caretSince, blinkSeconds * 2.0);
    context.requestFrameAt(context.time() + (phase < blinkSeconds ? blinkSeconds : blinkSeconds * 2.0) - phase);

    if (phase >= blinkSeconds) {
        return;
    }

    const float width = context.metric(ThemeMetric::CaretWidth);
    caret.drawList->AddRectFilled(caret.glyphPos, ImVec2(caret.glyphPos.x + width, caret.glyphPos.y + caret.glyphSize.y), caret.caretColor);
}

// A number is drawn against the right edge of its box and centered in its row, and the glyph margin before it shows the icon of the strongest tone among the markers starting on the line.
// The margin and the number are where the pointer opens the tooltip of those markers.
void CodeEditor::drawLineNumber(const TextEditor::CustomLineNumber& number) {
    const RenderContext& context = *m_rendering;
    const std::string text = std::to_string(number.lineNumber + 1);
    const float left = ImGui::GetWindowPos().x;
    const float glyphs = ImGui::GetFontSize();
    const float width = ImGui::CalcTextSize(text.c_str()).x;
    number.drawList->AddText(ImVec2(number.pos.x + number.size.x - width, number.pos.y + std::floor((number.size.y - glyphs) / 2.0F)), number.color, text.c_str());

    if (const auto found = m_lineMarkers.find(number.lineNumber); found != m_lineMarkers.end()) {
        const ThemeColor tone = m_markers[found->second].tone;
        const float em = std::round(static_cast<float>(m_fontSize) * context.scale());
        const ImVec2 corner(std::floor(left + (number.pos.x - left - em) / 2.0F), std::floor(number.pos.y + (number.size.y - em) / 2.0F));
        IconCatalog::draw(context.fonts(), *number.drawList, icon(tone), corner, em, context.color(tone));
    }

    if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(ImVec2(left, number.pos.y), number.pos + number.size)) {
        m_gutterLine = number.lineNumber;
    }
}

} // namespace workpane::ui
