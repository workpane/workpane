#include "support/InterfaceHarness.h"
#include "support/Resources.h"
#include "support/SystemRecord.h"
#include "ui/Color.h"
#include "ui/FindBar.h"
#include "ui/Fonts.h"
#include "ui/Widgets.h"
#include "ui/model/Component.h"
#include "ui/model/RenderContext.h"
#include "ui/model/UiEvent.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace workpane::ui {

using nlohmann::json;

// Drives a code editor the way a plugin does: it gives the language, the scheme, the markers and the proposals, and reads the text, the cursor and the requests.
class CodeEditorTest : public ::testing::Test {
  protected:
    static json lua() {
        return {{"name", "Lua"}, {"lineComment", "--"}, {"blockComment", {{"start", "--[["}, {"end", "]]"}}}, {"doubleQuotes", true}, {"singleQuotes", true}, {"escape", "\\"}, {"keywords", json::array({"local", "function", "end"})}, {"identifiers", json::array({"print"})}};
    }

    void mount(json properties) {
        properties["height"] = 300;
        const auto mounted = m_harness.mount({{"id", 1}, {"kind", "column"}, {"props", json::object()}, {"children", json::array({{{"id", 2}, {"kind", "codeEditor"}, {"props", properties}, {"children", json::array()}}})}});
        ASSERT_TRUE(mounted.hasValue()) << mounted.error().code << " " << mounted.error().detail;
        m_harness.frames(2);
    }

    std::optional<UiEvent> event(std::string_view name) {
        std::optional<UiEvent> found;

        for (auto& candidate : m_harness.takeEvents()) {
            if (candidate.name == name) {
                found = candidate;
            }
        }

        return found;
    }

    Result<void> command(std::string_view name, json arguments = json::object()) {
        return m_harness.node(2)->command(m_harness.context(), name, arguments);
    }

    // The library waits on the clock of the machine before it reports a change or asks for proposals, so the frames run until the event arrives or two seconds passed.
    std::optional<UiEvent> awaited(std::string_view name) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        std::optional<UiEvent> found;

        while (!found.has_value() && std::chrono::steady_clock::now() < deadline) {
            m_harness.frame();
            found = event(name);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        return found;
    }

    // Every scheme role takes one color, so a test gives a distinct color only to the role it looks for.
    static json scheme() {
        json colors = json::object();

        for (const std::string_view role : {"text", "keyword", "declaration", "number", "string", "punctuation", "preprocessor", "identifier", "knownIdentifier", "comment", "background", "cursor", "selection", "lineNumber", "currentLineNumber", "guide", "activeGuide", "currentLine", "occurrence"}) {
            colors[std::string(role)] = "#102030";
        }

        return colors;
    }

    static const ImGuiWindow* editorWindow() {
        for (const ImGuiWindow* window : GImGui->Windows) {
            if (window->Active && std::string_view(window->Name).find("##editor") != std::string_view::npos) {
                return window;
            }
        }

        return nullptr;
    }

    // The editor paints each part in its own color, so the vertices of one color in the editor are the shapes of one part.
    static std::vector<ImVec2> painted(ImU32 color) {
        std::vector<ImVec2> points;
        const ImGuiWindow* window = editorWindow();

        for (const ImDrawVert& vertex : window != nullptr ? window->DrawList->VtxBuffer : ImVector<ImDrawVert>()) {
            if (vertex.col == color) {
                points.push_back(vertex.pos);
            }
        }

        return points;
    }

    static std::optional<ImRect> bounds(const std::vector<ImVec2>& points) {
        std::optional<ImRect> found;

        for (const ImVec2& point : points) {
            found = found.has_value() ? ImRect(ImMin(found->Min, point), ImMax(found->Max, point)) : ImRect(point, point);
        }

        return found;
    }

    static std::optional<ImRect> caretBounds(ImU32 color) {
        return bounds(painted(color));
    }

    // A row of the editor is one and a half em tall, as the rows of Visual Studio Code on macOS.
    float row(float em = 10.0F) {
        return std::round(em * 1.5F * m_harness.context().scale());
    }

    // ImGui rounds the height of a face to whole pixels.
    float glyphHeight(float em = 10.0F) {
        return std::round(m_harness.context().fonts().size(FontFace::Monospace, em) * m_harness.context().scale());
    }

    const ImFontGlyph* glyph(ImWchar character, float em = 10.0F) {
        return m_harness.context().fonts().face(FontFace::Monospace)->GetFontBaked(glyphHeight(em))->FindGlyph(character);
    }

    // The tones a tooltip shows are read from the icons drawn in them, since nothing else in a tooltip wears a tone.
    std::vector<ThemeColor> tonesShown() {
        std::vector<ThemeColor> shown;

        for (const ThemeColor tone : {ThemeColor::Danger, ThemeColor::Warning, ThemeColor::Information}) {
            const ImU32 color = m_harness.context().color(tone).packed();
            // clang-format off
            const auto wears = [color](const ImDrawVert& vertex) { return vertex.col == color; };
            const auto carries = [&wears](const ImGuiWindow* window) { return std::ranges::any_of(window->DrawList->VtxBuffer, wears); };
            // clang-format on

            if (std::ranges::any_of(tooltips(), carries)) {
                shown.push_back(tone);
            }
        }

        return shown;
    }

    // The pointer crosses a line glyph by glyph and rests long enough on each word for its tooltip, collecting the tones every stop showed.
    std::vector<std::vector<ThemeColor>> crossLine(float y) {
        std::vector<std::vector<ThemeColor>> stops;

        for (float x = 1.0F; x < 160.0F; x += 3.0F) {
            m_harness.moveTo(ImVec2(x, y));
            m_harness.frames(40);
            stops.push_back(tonesShown());
        }

        return stops;
    }

    static std::vector<const ImGuiWindow*> tooltips() {
        std::vector<const ImGuiWindow*> shown;

        for (const ImGuiWindow* window : GImGui->Windows) {
            if (window->Active && (window->Flags & ImGuiWindowFlags_Tooltip) != 0) {
                shown.push_back(window);
            }
        }

        return shown;
    }

    tests::InterfaceHarness m_harness;
};

TEST_F(CodeEditorTest, ColorsALanguageItsPluginDefines) {
    mount({{"value", "print(1)"}, {"language", lua()}});

    // Commenting from the keys of the editor uses the line comment of the definition, which proves the definition reached the editor.
    ASSERT_TRUE(command("reveal", {{"line", 1}, {"column", 1}}).hasValue());
    m_harness.frames(2);
    m_harness.press(tests::InterfaceHarness::command() | ImGuiKey_A);
    m_harness.press(tests::InterfaceHarness::command() | ImGuiKey_Slash);
    const auto commented = awaited("change");
    ASSERT_TRUE(commented.has_value());
    EXPECT_EQ(commented->value["value"], "-- print(1)");

    // A definition breaking its rules, a name nobody carries and anything else are refused whole.
    json broken = lua();
    broken["escape"] = "ab";
    EXPECT_EQ(m_harness.patch(2, {{"language", broken}}).error().code, "editor_language_invalid");
    EXPECT_EQ(m_harness.patch(2, {{"language", "cobol"}}).error().code, "editor_language_unknown");
    EXPECT_EQ(m_harness.patch(2, {{"language", 3}}).error().code, "editor_language_invalid");
    EXPECT_TRUE(m_harness.patch(2, {{"language", "lua"}}).hasValue());
}

// The constructs a language names color what its words cannot: a Markdown heading like a declaration, a decorator like the preprocessor, and a tag like a keyword with its attributes like declarations.
TEST_F(CodeEditorTest, ColorsTheConstructsALanguageNames) {
    json colors = scheme();
    colors["declaration"] = "#d01020";
    colors["preprocessor"] = "#d03040";
    colors["keyword"] = "#d05060";
    json language = {{"name", "Sample"}, {"constructs", json::array({"markdown", "decorators", "tags"})}};
    mount({{"value", "# Title\n@cached.value\n<a href=1>"}, {"language", language}, {"scheme", colors}, {"fontSize", 10}});
    m_harness.frames(3);

    const auto heading = bounds(painted(IM_COL32(0xd0, 0x10, 0x20, 0xFF)));
    const auto decorated = bounds(painted(IM_COL32(0xd0, 0x30, 0x40, 0xFF)));
    const auto tagged = bounds(painted(IM_COL32(0xd0, 0x50, 0x60, 0xFF)));
    ASSERT_TRUE(heading.has_value());
    ASSERT_TRUE(decorated.has_value());
    ASSERT_TRUE(tagged.has_value());
    EXPECT_LT(heading->Min.y, decorated->Min.y);
    EXPECT_LT(decorated->Min.y, tagged->Min.y);

    language["constructs"] = json::array({"regex"});
    EXPECT_EQ(m_harness.patch(2, {{"language", language}}).error().code, "editor_language_invalid");
}

TEST_F(CodeEditorTest, ReportsTheCursorAndRevealsAPosition) {
    mount({{"value", "alpha\nbeta gamma"}});
    std::ignore = m_harness.takeEvents();

    ASSERT_TRUE(command("reveal", {{"line", 2}, {"column", 6}}).hasValue());
    m_harness.frames(2);
    const auto moved = event("cursor");
    ASSERT_TRUE(moved.has_value());
    EXPECT_EQ(moved->value, (json{{"line", 2}, {"column", 6}, {"selection", ""}}));

    // The reader types where the position was revealed, and a position outside the grammar is refused.
    m_harness.type("X");
    const auto typed = awaited("change");
    ASSERT_TRUE(typed.has_value());
    EXPECT_EQ(typed->value["value"], "alpha\nbeta Xgamma");
    EXPECT_FALSE(command("reveal", {{"line", 0}, {"column", 1}}).hasValue());
    EXPECT_FALSE(command("reveal", {{"line", 1}}).hasValue());
}

#if defined(__APPLE__)
// Command with the left or the right arrow moves to the start or the end of the line, as every text view of macOS does, and selects up to there with Shift.
TEST_F(CodeEditorTest, MovesToTheEndsOfTheLineWithCommandAndTheArrows) {
    mount({{"value", "alpha\nbeta gamma"}});
    ASSERT_TRUE(command("reveal", {{"line", 2}, {"column", 6}}).hasValue());
    m_harness.frames(2);
    std::ignore = m_harness.takeEvents();

    m_harness.press(tests::InterfaceHarness::command() | ImGuiKey_RightArrow);
    m_harness.frames(2);
    const auto end = event("cursor");
    ASSERT_TRUE(end.has_value());
    EXPECT_EQ(end->value, (json{{"line", 2}, {"column", 11}, {"selection", ""}}));

    m_harness.press(tests::InterfaceHarness::command() | ImGuiMod_Shift | ImGuiKey_LeftArrow);
    m_harness.frames(2);
    const auto selected = event("cursor");
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected->value, (json{{"line", 2}, {"column", 1}, {"selection", "beta gamma"}}));

    m_harness.press(tests::InterfaceHarness::command() | ImGuiKey_RightArrow);
    m_harness.frames(2);
    m_harness.press(tests::InterfaceHarness::command() | ImGuiKey_LeftArrow);
    m_harness.frames(2);
    const auto start = event("cursor");
    ASSERT_TRUE(start.has_value());
    EXPECT_EQ(start->value, (json{{"line", 2}, {"column", 1}, {"selection", ""}}));
}
#endif

// Find opens from the keys of the editor, counts the matches, steps forward and back past the ends, keeps whole words when asked, and replaces one match or every match.
TEST_F(CodeEditorTest, FindsStepsAndReplacesFromItsOwnBar) {
    mount({{"value", "alpha beta\nbeta alpha\nalphabet"}});
    ASSERT_TRUE(command("reveal", {{"line", 1}, {"column", 1}}).hasValue());
    m_harness.frames(2);
    std::ignore = m_harness.takeEvents();

    // The match being read is selected, so the cursor stands at its end with the match as the selection.
    // clang-format off
    const auto selected = [this]() { const auto found = event("cursor"); return found.has_value() ? json{found->value["line"], found->value["column"], found->value["selection"]} : json(); };
    // clang-format on
    m_harness.press(tests::InterfaceHarness::command() | ImGuiKey_F);
    m_harness.frames(2);
    m_harness.type("alpha");
    m_harness.frames(2);
    EXPECT_EQ(selected(), json::array({1, 6, "alpha"}));

    m_harness.press(ImGuiKey_Enter);
    m_harness.frames(2);
    EXPECT_EQ(selected(), json::array({2, 11, "alpha"}));
    m_harness.press(ImGuiMod_Shift | ImGuiKey_Enter);
    m_harness.frames(2);
    EXPECT_EQ(selected(), json::array({1, 6, "alpha"}));
    m_harness.press(ImGuiMod_Shift | ImGuiKey_Enter);
    m_harness.frames(2);
    EXPECT_EQ(selected(), json::array({3, 6, "alpha"}));

    // Whole words leave out the word the query is only a part of, so the next button steps from the second match back to the first.
    const float scale = m_harness.context().scale();
    const float gap = 4.0F * scale;
    const float button = m_harness.context().metric(ThemeMetric::CompactButtonSize);
    const ImRect bar = FindBar::area(m_harness.context(), ImRect(0.0F, 0.0F, tests::InterfaceHarness::width, 300.0F), true);
    const float top = bar.Min.y + (Widgets::controlHeight(m_harness.context()) + gap * 2.0F) / 2.0F;
    // clang-format off
    const auto slot = [&](float index) { return ImVec2(bar.Max.x - gap - index * (button + gap / 2.0F) - button / 2.0F, top); };
    // clang-format on
    m_harness.click(slot(3.0F));
    m_harness.frames(2);
    EXPECT_EQ(selected(), json::array({1, 6, "alpha"}));
    m_harness.click(slot(1.0F));
    m_harness.frames(2);
    EXPECT_EQ(selected(), json::array({2, 11, "alpha"}));
    m_harness.click(slot(1.0F));
    m_harness.frames(2);
    EXPECT_EQ(selected(), json::array({1, 6, "alpha"}));

    // The replacement row sits under the query, with Replace All at its right end.
    const float row = bar.Max.y - Widgets::controlHeight(m_harness.context()) / 2.0F - gap;
    m_harness.click(ImVec2(bar.Min.x + 24.0F * scale, row));
    m_harness.type("omega");
    m_harness.press(ImGuiKey_Enter);
    const auto replaced = awaited("change");
    ASSERT_TRUE(replaced.has_value());
    EXPECT_EQ(replaced->value["value"], "omega beta\nbeta alpha\nalphabet");

    m_harness.click(ImVec2(bar.Max.x - 12.0F * scale, row));
    const auto every = awaited("change");
    ASSERT_TRUE(every.has_value());
    EXPECT_EQ(every->value["value"], "omega beta\nbeta omega\nalphabet");

    // Escape closes the bar and gives the keyboard back to the text, where every match comes back in one step of the undo history.
    m_harness.click(ImVec2(bar.Min.x + 24.0F * scale, top));
    m_harness.press(ImGuiKey_Escape);
    m_harness.frames(2);
    m_harness.press(tests::InterfaceHarness::command() | ImGuiKey_Z);
    const auto undone = awaited("change");
    ASSERT_TRUE(undone.has_value());
    EXPECT_EQ(undone->value["value"], "omega beta\nbeta alpha\nalphabet");
}

// A search of a long document reads a bounded part of it at each frame, asks for the next frame until it reaches the end and then reads the match after the cursor, counted in glyphs.
TEST_F(CodeEditorTest, FindsInALongDocumentOverSeveralFrames) {
    std::string document;

    for (int line = 0; line < 8000; ++line) {
        document += "row \xC3\xA9 item \xC3\xA9\n";
    }

    mount({{"value", document}});
    m_harness.frames(2);
    ASSERT_TRUE(command("reveal", {{"line", 7000}, {"column", 1}}).hasValue());
    m_harness.frames(2);
    std::ignore = m_harness.takeEvents();
    m_harness.press(tests::InterfaceHarness::command() | ImGuiKey_F);
    m_harness.frames(2);
    m_harness.type("item");
    EXPECT_TRUE(m_harness.context().frameRequested());
    std::optional<UiEvent> found;

    for (int frame = 0; frame < 20 && !found.has_value(); ++frame) {
        m_harness.frame();
        found = event("cursor");
    }

    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->value["line"], 7000);
    EXPECT_EQ(found->value["column"], 11);
    EXPECT_EQ(found->value["selection"], "item");
    m_harness.frames(2);
    EXPECT_FALSE(m_harness.context().frameRequested());
}

// An edit made while the bar is open finds the matches again, so a replacement lands on the match and never on the text that took its old place.
TEST_F(CodeEditorTest, FindsAgainAfterAnEditBeforeReplacing) {
    mount({{"value", "alpha one\nalpha two"}});
    ASSERT_TRUE(command("reveal", {{"line", 1}, {"column", 1}}).hasValue());
    m_harness.frames(2);
    m_harness.press(tests::InterfaceHarness::command() | ImGuiKey_F);
    m_harness.frames(2);
    m_harness.type("alpha");
    m_harness.frames(2);

    ASSERT_TRUE(m_harness.patch(2, {{"value", "xx alpha one\nalpha two"}}).hasValue());
    m_harness.frames(2);
    const float scale = m_harness.context().scale();
    const ImRect bar = FindBar::area(m_harness.context(), ImRect(0.0F, 0.0F, tests::InterfaceHarness::width, 300.0F), true);
    const float row = bar.Max.y - Widgets::controlHeight(m_harness.context()) / 2.0F - 4.0F * scale;
    m_harness.click(ImVec2(bar.Min.x + 24.0F * scale, row));
    m_harness.type("omega");
    m_harness.press(ImGuiKey_Enter);
    const auto replaced = awaited("change");

    ASSERT_TRUE(replaced.has_value());
    EXPECT_EQ(replaced->value["value"], "xx omega one\nalpha two");
}

TEST_F(CodeEditorTest, TakesASchemeMarkersAndHighlightsOnlyWhenTheyAreWhole) {
    const json colors = scheme();
    const json markers = json::array({{{"line", 1}, {"tone", "danger"}, {"message", "Undefined name"}}, {{"line", 1}, {"tone", "warning"}, {"message", "Unused value"}}});
    const json highlights = json::array({{{"line", 1}, {"column", 1}, {"length", 5}, {"role", "declaration"}}});
    mount({{"value", "print(1)"}, {"language", lua()}, {"scheme", colors}, {"markers", markers}, {"highlights", highlights}});

    json partial = colors;
    partial.erase("comment");
    json outlineless = colors;
    outlineless.erase("currentLine");
    json wrong = colors;
    wrong["comment"] = "green";
    EXPECT_FALSE(m_harness.patch(2, {{"scheme", partial}}).hasValue());
    EXPECT_EQ(m_harness.patch(2, {{"scheme", outlineless}}).error().code, "json_field_missing");
    EXPECT_EQ(m_harness.patch(2, {{"scheme", wrong}}).error().code, "ui_color_invalid");
    EXPECT_FALSE(m_harness.patch(2, {{"markers", json::array({{{"line", 1}, {"tone", "loud"}, {"message", "x"}}})}}).hasValue());
    EXPECT_EQ(m_harness.patch(2, {{"markers", json::array({{{"line", 1}, {"endColumn", 3}, {"tone", "danger"}, {"message", "x"}}})}}).error().code, "editor_marker_range");
    EXPECT_EQ(m_harness.patch(2, {{"markers", json::array({{{"line", 2}, {"column", 3}, {"endLine", 1}, {"tone", "danger"}, {"message", "x"}}})}}).error().code, "editor_marker_range");
    EXPECT_EQ(m_harness.patch(2, {{"markers", json::array({{{"line", 1}, {"column", 3}, {"endColumn", 2}, {"tone", "danger"}, {"message", "x"}}})}}).error().code, "editor_marker_range");
    EXPECT_EQ(m_harness.patch(2, {{"markers", json::array({{{"line", 1}, {"column", 1}, {"tone", "danger"}, {"message", "x"}, {"related", json::array({{{"place", "a.lua:1:1"}}})}}})}}).error().code, "json_field_missing");
    EXPECT_TRUE(m_harness.patch(2, {{"markers", json::array({{{"line", 1}, {"column", 1}, {"endLine", 1}, {"endColumn", 6}, {"tone", "danger"}, {"message", "x"}, {"detail", "lua(undefined)"}, {"related", json::object()}}})}}).hasValue());
    EXPECT_EQ(m_harness.patch(2, {{"highlights", json::array({{{"line", 1}, {"column", 1}, {"length", 1}, {"role", "background"}}})}}).error().code, "editor_highlight_invalid");
    EXPECT_EQ(m_harness.patch(2, {{"fontFamily", ""}}).error().code, "editor_font_family_invalid");
    EXPECT_EQ(m_harness.patch(2, {{"fontFamily", std::string(300, 'a')}}).error().code, "editor_font_family_invalid");
    EXPECT_TRUE(m_harness.patch(2, {{"highlights", json::array()}, {"markers", json::array()}, {"fontFamily", "Nothing Mono"}}).hasValue());
    m_harness.frames(2);
}

// A family of the machine writes the text once it was read, and every row keeps the height of one and a half em.
TEST_F(CodeEditorTest, WritesInTheFamilyOfTheMachineOnceItWasRead) {
    m_harness.system().fonts = {{"Inter", tests::Resources::fonts() / "Inter-Regular.ttf"}};
    mount({{"value", "MMMM"}, {"fontSize", 10}, {"fontFamily", "Inter"}});
    const Fonts& fonts = m_harness.context().fonts();

    for (int attempt = 0; attempt < 400 && !fonts.has("Inter"); ++attempt) {
        m_harness.frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    ASSERT_TRUE(fonts.has("Inter"));
    m_harness.frames(2);
    ImFont* chosen = fonts.face(FontFace::Monospace, "Inter");
    const ImFontGlyph* glyph = chosen->GetFontBaked(fonts.size(FontFace::Monospace, 10.0F, "Inter"))->FindGlyph('M');
    bool drawn = false;

    for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists) {
        for (const ImDrawVert& vertex : list->VtxBuffer) {
            drawn = drawn || (vertex.uv.x == glyph->U0 && vertex.uv.y == glyph->V0);
        }
    }

    EXPECT_NE(chosen, fonts.face(FontFace::Monospace));
    EXPECT_TRUE(drawn);
}

// Typing a word asks the plugin for proposals, and the proposal the reader accepts replaces the word.
TEST_F(CodeEditorTest, AsksForCompletionAndInsertsTheAcceptedProposal) {
    mount({{"value", ""}, {"language", lua()}, {"completion", true}});
    m_harness.click(ImVec2(200.0F, 100.0F));
    m_harness.type("pri");
    const auto requested = awaited("complete-request");

    ASSERT_TRUE(requested.has_value());
    EXPECT_EQ(requested->value, (json{{"request", 1}, {"line", 1}, {"column", 4}, {"word", "pri"}}));

    ASSERT_TRUE(command("suggest", {{"request", 1}, {"items", json::array({{{"label", "print"}, {"insert", "print"}, {"detail", "function"}}, {{"label", "printf"}, {"insert", "printf"}}})}}).hasValue());
    m_harness.frames(2);
    m_harness.press(ImGuiKey_Enter);
    const auto accepted = awaited("change");
    ASSERT_TRUE(accepted.has_value());
    EXPECT_EQ(accepted->value["value"], "print");
    EXPECT_FALSE(command("suggest", {{"request", 1}, {"items", "print"}}).hasValue());
    EXPECT_FALSE(command("suggest", {{"request", 1}, {"items", json::array({{{"label", ""}, {"insert", "x"}}})}}).hasValue());
    EXPECT_FALSE(command("suggest", {{"request", 1}, {"items", json::array({{{"label", "x"}, {"insert", "x"}, {"range", {{"line", 1}, {"column", 3}, {"endLine", 1}, {"endColumn", 1}}}}})}}).hasValue());
    EXPECT_FALSE(command("suggest", {{"items", json::array()}}).hasValue());
}

// A long list of proposals opens without drawing every row, waits without asking for frames while the reader does nothing, and still follows the arrows to a proposal off screen.
TEST_F(CodeEditorTest, KeepsALongCompletionListStillUntilAKeyMovesIt) {
    mount({{"value", ""}, {"language", lua()}, {"completion", true}});
    m_harness.click(ImVec2(200.0F, 100.0F));
    m_harness.type("item");
    ASSERT_TRUE(awaited("complete-request").has_value());
    json items = json::array();

    for (int index = 0; index < 1500; ++index) {
        items.push_back({{"label", "item" + std::to_string(index)}, {"insert", "item" + std::to_string(index)}});
    }

    ASSERT_TRUE(command("suggest", {{"request", 1}, {"items", items}}).hasValue());
    m_harness.frames(3);
    m_harness.frame();
    EXPECT_FALSE(m_harness.context().frameRequested());

    for (int step = 0; step < 12; ++step) {
        m_harness.press(ImGuiKey_DownArrow);
    }

    m_harness.press(ImGuiKey_Enter);
    const auto accepted = awaited("change");
    ASSERT_TRUE(accepted.has_value());
    EXPECT_EQ(accepted->value["value"], "item12");
}

// A character the server names asks for proposals at once, the list narrows as the reader types, the arrows choose, a proposal replaces the range its server gave, and Escape closes the list without touching the text.
TEST_F(CodeEditorTest, ChoosesAProposalByItsServerRangeAndItsDocumentation) {
    mount({{"value", ""}, {"language", lua()}, {"completion", true}, {"completionTriggers", json::array({"."})}});
    m_harness.click(ImVec2(200.0F, 100.0F));
    m_harness.type("self.");
    const auto requested = awaited("complete-request");
    ASSERT_TRUE(requested.has_value());
    EXPECT_EQ(requested->value, (json{{"request", 1}, {"line", 1}, {"column", 6}, {"word", ""}}));

    const json range = {{"line", 1}, {"column", 6}, {"endLine", 1}, {"endColumn", 6}};
    ASSERT_TRUE(command("suggest", {{"request", 1}, {"items", json::array({{{"label", "alpha"}, {"insert", "alpha()"}, {"range", range}, {"documentation", "Answers the first letter."}}, {{"label", "beta"}, {"insert", "beta()"}, {"range", range}}})}}).hasValue());
    m_harness.frames(2);
    ASSERT_FALSE(GImGui->OpenPopupStack.empty());

    // The documentation of the chosen proposal is written beside the list, which makes the list wider than the list alone.
    const ImGuiWindow* list = GImGui->OpenPopupStack.back().Window;
    ASSERT_NE(list, nullptr);
    EXPECT_GT(list->Size.x, 400.0F * m_harness.context().scale());
    m_harness.type("b");
    m_harness.frames(2);
    m_harness.press(ImGuiKey_Tab);
    const auto accepted = awaited("change");
    ASSERT_TRUE(accepted.has_value());
    EXPECT_EQ(accepted->value["value"], "self.beta()");

    m_harness.type(".");
    ASSERT_TRUE(awaited("complete-request").has_value());
    ASSERT_TRUE(command("suggest", {{"request", 2}, {"items", json::array({{{"label", "gamma"}, {"insert", "gamma"}}})}}).hasValue());
    m_harness.frames(2);
    m_harness.press(ImGuiKey_Escape);
    m_harness.frames(2);
    const auto untouched = awaited("change");
    ASSERT_TRUE(untouched.has_value());
    EXPECT_EQ(untouched->value["value"], "self.beta().");
}

// A text its plugin gives keeps its tabs and is never reported back as an edit of the reader, while the tab key still inserts spaces.
TEST_F(CodeEditorTest, KeepsTheTabsOfTheTextItsPluginGives) {
    mount({{"value", "all:\n\techo done"}, {"insertSpaces", true}, {"tabSize", 4}});
    EXPECT_FALSE(awaited("change").has_value());

    ASSERT_TRUE(command("reveal", {{"line", 1}, {"column", 1}}).hasValue());
    m_harness.frames(2);
    m_harness.press(ImGuiKey_Tab);
    const auto typed = awaited("change");
    ASSERT_TRUE(typed.has_value());
    EXPECT_EQ(typed->value["value"], "    all:\n\techo done");

    ASSERT_TRUE(m_harness.patch(2, {{"value", "\tone\n\ttwo"}}).hasValue());
    m_harness.frames(2);
    EXPECT_FALSE(awaited("change").has_value());
    ASSERT_TRUE(command("flush").hasValue());
    m_harness.frame();
    EXPECT_FALSE(event("change").has_value());
}

// Proposals that arrive after the cursor left their word are dropped, and a proposal whose range the cursor no longer ends is never written.
TEST_F(CodeEditorTest, DropsProposalsForAWordTheCursorLeft) {
    mount({{"value", ""}, {"language", lua()}, {"completion", true}});
    m_harness.click(ImVec2(200.0F, 100.0F));
    m_harness.type("x pri");
    ASSERT_TRUE(awaited("complete-request").has_value());

    m_harness.press(ImGuiKey_Home);
    m_harness.frames(2);
    ASSERT_TRUE(command("suggest", {{"request", 1}, {"items", json::array({{{"label", "print"}, {"insert", "print"}}})}}).hasValue());
    m_harness.frames(2);
    EXPECT_TRUE(GImGui->OpenPopupStack.empty());
    m_harness.press(ImGuiKey_Enter);
    const auto pressed = awaited("change");
    ASSERT_TRUE(pressed.has_value());
    EXPECT_EQ(pressed->value["value"], "\nx pri");
}

// A text that shrinks while the view stands far below its new end is shown from its start, with or without word wrap, and never reads a line it no longer holds.
TEST_F(CodeEditorTest, ShowsATextThatShrankBelowTheView) {
    std::string lines;

    for (int line = 0; line < 300; ++line) {
        lines += "a line long enough to wrap when the editor wraps its lines at the width of its view, number " + std::to_string(line) + "\n";
    }

    for (const bool wrapped : {false, true}) {
        mount({{"value", lines}, {"wordWrap", wrapped}});
        ASSERT_TRUE(command("reveal", {{"line", 295}, {"column", 1}}).hasValue());
        m_harness.frames(3);
        m_harness.press(tests::InterfaceHarness::command() | ImGuiKey_A);
        m_harness.press(ImGuiKey_Backspace);
        m_harness.frames(3);
        const auto emptied = awaited("change");
        ASSERT_TRUE(emptied.has_value());
        EXPECT_EQ(emptied->value["value"], "");
        EXPECT_EQ(editorWindow()->Scroll.y, 0.0F);

        ASSERT_TRUE(m_harness.patch(2, {{"value", lines}}).hasValue());
        ASSERT_TRUE(command("reveal", {{"line", 295}, {"column", 1}}).hasValue());
        m_harness.frames(3);
        ASSERT_TRUE(m_harness.patch(2, {{"value", "short"}}).hasValue());
        m_harness.frames(3);
        EXPECT_EQ(editorWindow()->Scroll.y, 0.0F);
    }
}

// The menu reads the clipboard once as it opens, however many frames the pointer moves over it, since reading it on X11 waits for the program that owns it.
TEST_F(CodeEditorTest, ReadsTheClipboardOnceForItsMenu) {
    mount({{"value", "alpha\nbeta"}});
    static int reads = 0;
    // clang-format off
    ImGui::GetPlatformIO().Platform_GetClipboardTextFn = [](ImGuiContext*) -> const char* { ++reads; return "pasted"; };
    // clang-format on
    m_harness.rightClick(ImVec2(200.0F, 100.0F));
    ASSERT_EQ(GImGui->OpenPopupStack.Size, 1);

    for (int step = 0; step < 10; ++step) {
        m_harness.moveTo(ImVec2(210.0F + static_cast<float>(step) * 4.0F, 110.0F + static_cast<float>(step) * 6.0F));
    }

    EXPECT_EQ(reads, 1);
}

// The list keeps the proposal the arrows chose while the word stays the same, so Enter accepts it instead of the first one.
TEST_F(CodeEditorTest, AcceptsTheProposalTheArrowsChose) {
    mount({{"value", ""}, {"language", lua()}, {"completion", true}});
    m_harness.click(ImVec2(200.0F, 100.0F));
    m_harness.type("pri");
    ASSERT_TRUE(awaited("complete-request").has_value());

    ASSERT_TRUE(command("suggest", {{"request", 1}, {"items", json::array({{{"label", "print"}, {"insert", "print"}}, {{"label", "printf"}, {"insert", "printf"}}})}}).hasValue());
    m_harness.frames(2);
    m_harness.press(ImGuiKey_DownArrow);
    m_harness.frames(3);
    m_harness.press(ImGuiKey_Enter);
    const auto accepted = awaited("change");
    ASSERT_TRUE(accepted.has_value());
    EXPECT_EQ(accepted->value["value"], "printf");
}

// Only the latest request is answered and only once, so an older answer and an answer that arrives after Escape never open the list.
TEST_F(CodeEditorTest, DropsAnswersOlderThanTheLastRequestOrLaterThanTheList) {
    mount({{"value", ""}, {"language", lua()}, {"completion", true}});
    m_harness.click(ImVec2(200.0F, 100.0F));
    m_harness.type("p");
    ASSERT_EQ(awaited("complete-request")->value["request"], 1);
    m_harness.type("r");
    ASSERT_EQ(awaited("complete-request")->value["request"], 2);

    const json items = json::array({{{"label", "print"}, {"insert", "print"}}});
    ASSERT_TRUE(command("suggest", {{"request", 1}, {"items", items}}).hasValue());
    m_harness.frames(2);
    EXPECT_TRUE(GImGui->OpenPopupStack.empty());

    ASSERT_TRUE(command("suggest", {{"request", 2}, {"items", items}}).hasValue());
    m_harness.frames(2);
    EXPECT_FALSE(GImGui->OpenPopupStack.empty());

    m_harness.press(ImGuiKey_Escape);
    m_harness.frames(2);
    ASSERT_TRUE(GImGui->OpenPopupStack.empty());
    ASSERT_TRUE(command("suggest", {{"request", 2}, {"items", items}}).hasValue());
    m_harness.frames(2);
    EXPECT_TRUE(GImGui->OpenPopupStack.empty());
}

// Every marker keeps its own tone after a deletion, so the strongest underline stays in the danger tone after any text shrinks.
TEST_F(CodeEditorTest, KeepsTheToneOfEveryMarkerAfterADeletion) {
    const json markers = json::array({{{"line", 1}, {"column", 1}, {"endLine", 1}, {"endColumn", 6}, {"tone", "danger"}, {"message", "a"}}, {{"line", 1}, {"column", 7}, {"endLine", 1}, {"endColumn", 11}, {"tone", "warning"}, {"message", "b"}}, {{"line", 1}, {"column", 12}, {"endLine", 1}, {"endColumn", 17}, {"tone", "information"}, {"message", "c"}}});
    mount({{"value", "alpha beta gamma\nx"}, {"markers", markers}, {"fontSize", 10}});
    const ImU32 danger = m_harness.context().color(ThemeColor::Danger).packed();
    const float advance = glyph('#')->AdvanceX;
    const float textLeft = editorWindow()->Pos.x + 5.0F * advance;
    // The underline lies within the text of the line, while the message of the marker is written after it.
    // clang-format off
    const auto underlined = [&]() { return std::ranges::any_of(painted(danger), [&](const ImVec2& point) { return point.x >= textLeft && point.x < textLeft + 17.0F * advance; }); };
    // clang-format on
    ASSERT_TRUE(underlined());

    ASSERT_TRUE(command("reveal", {{"line", 2}, {"column", 2}}).hasValue());
    m_harness.frames(2);
    m_harness.press(ImGuiKey_Backspace);
    m_harness.frames(3);
    EXPECT_TRUE(underlined());
}

// An edit the delay of the change event still holds is reported at once when the plugin flushes, followed by the flushed event, and a flush with nothing pending answers only that event.
TEST_F(CodeEditorTest, ReportsAPendingEditAtOnceWhenFlushed) {
    mount({{"value", "alpha\nbeta"}});
    ASSERT_TRUE(command("reveal", {{"line", 2}, {"column", 1}}).hasValue());
    m_harness.frames(2);
    std::ignore = m_harness.takeEvents();

    m_harness.type("    ");
    ASSERT_TRUE(command("flush").hasValue());
    m_harness.frame();
    const auto events = m_harness.takeEvents();
    // clang-format off
    const auto named = [&events](std::string_view name) { return std::ranges::find_if(events, [name](const UiEvent& candidate) { return candidate.name == name; }); };
    // clang-format on

    ASSERT_NE(named("change"), events.end());
    ASSERT_NE(named("flushed"), events.end());
    EXPECT_LT(named("change"), named("flushed"));
    EXPECT_EQ(named("change")->value["value"], "alpha\n    beta");

    ASSERT_TRUE(command("flush").hasValue());
    m_harness.frame();
    EXPECT_TRUE(event("flushed").has_value());
    EXPECT_FALSE(awaited("change").has_value());
}

// An editor that is no longer drawn reports the edit it still held, so a document hidden right after typing never keeps text from its plugin.
TEST_F(CodeEditorTest, ReportsAPendingEditOnceItIsNoLongerDrawn) {
    const auto mounted = m_harness.mount({{"id", 1}, {"kind", "stack"}, {"props", {{"current", 0}}}, {"children", json::array({{{"id", 2}, {"kind", "codeEditor"}, {"props", {{"value", "alpha"}, {"height", 200}}}, {"children", json::array()}}, {{"id", 3}, {"kind", "label"}, {"props", {{"text", "Other"}}}, {"children", json::array()}}})}});
    ASSERT_TRUE(mounted.hasValue());
    ASSERT_TRUE(command("reveal", {{"line", 1}, {"column", 1}}).hasValue());
    m_harness.frames(2);
    std::ignore = m_harness.takeEvents();

    m_harness.type("    ");
    ASSERT_TRUE(m_harness.patch(1, {{"current", 1}}).hasValue());
    m_harness.frames(2);
    const auto reported = event("change");

    ASSERT_TRUE(reported.has_value());
    EXPECT_EQ(reported->value["value"], "    alpha");
}

// A new text from the plugin keeps the cursor where the reader had it, as a file read again from disk does.
TEST_F(CodeEditorTest, KeepsTheCursorWhenItsTextIsReplaced) {
    mount({{"value", "alpha\nbeta"}});
    ASSERT_TRUE(command("reveal", {{"line", 2}, {"column", 3}}).hasValue());
    m_harness.frames(2);
    std::ignore = m_harness.takeEvents();

    ASSERT_TRUE(m_harness.patch(2, {{"value", "alpha\nbeta gamma\ndelta"}}).hasValue());
    m_harness.frames(3);
    const auto moved = event("cursor");

    EXPECT_TRUE(!moved.has_value() || (moved->value["line"] == 2 && moved->value["column"] == 3));
    m_harness.type("X");
    const auto typed = awaited("change");
    ASSERT_TRUE(typed.has_value());
    EXPECT_EQ(typed->value["value"], "alpha\nbeXta gamma\ndelta");
}

// The pointer resting on a word asks for its text once, and the answer is refused unless it names a position and a text.
TEST_F(CodeEditorTest, AsksForTheTextOfTheWordUnderThePointer) {
    mount({{"value", "local value = 1"}, {"language", lua()}, {"hovers", true}});
    m_harness.moveTo(ImVec2(60.0F, 8.0F));
    m_harness.frames(45);
    const auto hovered = event("hover");

    ASSERT_TRUE(hovered.has_value());
    EXPECT_EQ(hovered->value["line"], 1);
    EXPECT_FALSE(hovered->value["word"].get<std::string>().empty());

    ASSERT_TRUE(command("hover-text", {{"line", hovered->value["line"]}, {"column", hovered->value["column"]}, {"text", "A local variable"}}).hasValue());
    m_harness.frames(3);
    EXPECT_FALSE(event("hover").has_value());
    EXPECT_FALSE(command("hover-text", {{"line", 1}, {"column", 1}}).hasValue());
}

// The pointer crossing the first column of an empty line reaches no glyph, which once read before the start of that line.
TEST_F(CodeEditorTest, KeepsWorkingWhileThePointerCrossesAnEmptyLine) {
    mount({{"value", "local value = 1\n\nprint(value)"}, {"language", lua()}, {"hovers", true}, {"fontSize", 10}});
    const float line = m_harness.context().fonts().size(FontFace::Monospace, 10.0F);

    for (float x = 1.0F; x < 120.0F; x += 1.5F) {
        m_harness.moveTo(ImVec2(x, line * 1.5F));
        m_harness.frame();
    }

    ASSERT_TRUE(command("reveal", {{"line", 3}, {"column", 1}}).hasValue());
    m_harness.frames(2);
    EXPECT_TRUE(event("cursor").has_value());
}

// The caret is a bar as wide as the caret of the theme and as tall as its row, in the caret color of the scheme, lit from the moment it moves and dark for the next half second.
TEST_F(CodeEditorTest, DrawsACaretAsWideAsTheCaretOfTheThemeThatBlinks) {
    mount({{"value", "alpha"}, {"fontSize", 10}});
    ASSERT_TRUE(command("reveal", {{"line", 1}, {"column", 3}}).hasValue());
    m_harness.frames(3);
    const ImU32 caret = IM_COL32(0xAE, 0xAF, 0xAD, 0xFF);
    const auto bounds = caretBounds(caret);

    ASSERT_TRUE(bounds.has_value());
    EXPECT_NEAR(bounds->GetWidth(), m_harness.context().metric(ThemeMetric::CaretWidth), 0.01F);
    EXPECT_NEAR(bounds->GetHeight(), row(), 0.5F);

    m_harness.frames(36);
    EXPECT_FALSE(caretBounds(caret).has_value());
    m_harness.frames(30);
    EXPECT_TRUE(caretBounds(caret).has_value());
}

// The markers of a line explain themselves in one tooltip of the product on the number of the line, never in a second one of the library.
TEST_F(CodeEditorTest, ExplainsTheMarkersOfALineInOneTooltipOnItsNumber) {
    const json markers = json::array({{{"line", 2}, {"tone", "danger"}, {"message", "Undefined name"}}, {{"line", 2}, {"tone", "warning"}, {"message", "Unused value"}}});
    mount({{"value", "alpha\nbeta gamma"}, {"markers", markers}, {"fontSize", 10}});
    const float line = m_harness.context().fonts().size(FontFace::Monospace, 10.0F);
    std::vector<const ImGuiWindow*> shown;

    for (float x = 1.0F; x < 40.0F && shown.empty(); x += 1.0F) {
        m_harness.moveTo(ImVec2(x, line * 1.5F));
        m_harness.frames(2);
        shown = tooltips();
    }

    ASSERT_EQ(shown.size(), 1U);
    const ImU32 background = ImGui::ColorConvertFloat4ToU32(m_harness.context().color(ThemeColor::Tooltip).vector());
    const ImU32 divider = m_harness.context().color(ThemeColor::Border).packed();
    const ImU32 muted = m_harness.context().color(ThemeColor::TextMuted).packed();
    // clang-format off
    const auto dark = [background](const ImDrawVert& vertex) { return vertex.col == background; };
    const auto divides = [divider](const ImDrawVert& vertex) { return vertex.col == divider; };
    const auto placed = [muted](const ImDrawVert& vertex) { return vertex.col == muted; };
    // clang-format on
    EXPECT_TRUE(std::ranges::any_of(shown.front()->DrawList->VtxBuffer, dark));
    EXPECT_TRUE(std::ranges::any_of(shown.front()->DrawList->VtxBuffer, divides));
    EXPECT_TRUE(std::ranges::any_of(shown.front()->DrawList->VtxBuffer, placed));
    EXPECT_EQ(tonesShown(), (std::vector<ThemeColor>{ThemeColor::Danger, ThemeColor::Warning}));

    m_harness.moveTo(ImVec2(150.0F, line * 1.5F));
    m_harness.frames(45);
    EXPECT_TRUE(tooltips().empty());
}

// The number of a line shows every marker starting on it, while on the text only the markers covering the glyph under the pointer show.
TEST_F(CodeEditorTest, ExplainsOnTheTextOnlyTheMarkersCoveringTheGlyph) {
    const json markers = json::array({{{"line", 1}, {"column", 1}, {"endColumn", 6}, {"tone", "danger"}, {"message", "Undefined name"}, {"detail", "lua(undefined)"}}, {{"line", 1}, {"column", 7}, {"tone", "warning"}, {"message", "Unused value"}, {"related", json::array({{{"place", "main.lua:4:2"}, {"message", "Declared here"}}})}}, {{"line", 2}, {"tone", "information"}, {"message", "Second line"}}});
    mount({{"value", "alpha beta gamma\nsecond"}, {"markers", markers}, {"fontSize", 10}});
    const float line = m_harness.context().fonts().size(FontFace::Monospace, 10.0F);
    const auto first = crossLine(line * 0.5F);
    const auto second = crossLine(line * 1.5F);
    const std::vector<ThemeColor> danger{ThemeColor::Danger};
    const std::vector<ThemeColor> warning{ThemeColor::Warning};
    const std::vector<ThemeColor> information{ThemeColor::Information};
    const auto dangerAt = std::ranges::find(first, danger);
    const auto warningAt = std::ranges::find(first, warning);
    // clang-format off
    const auto mixed = [](const std::vector<ThemeColor>& tones) { return tones.size() > 1U; };
    // clang-format on

    ASSERT_NE(dangerAt, first.end());
    ASSERT_NE(warningAt, first.end());
    EXPECT_LT(dangerAt, warningAt);
    EXPECT_TRUE(std::any_of(first.begin(), dangerAt, mixed));
    EXPECT_TRUE(std::none_of(dangerAt, first.end(), mixed));
    EXPECT_TRUE(first.back().empty());
    EXPECT_NE(std::ranges::find(second, information), second.end());
    EXPECT_TRUE(std::ranges::none_of(second, mixed));
}

// A related place is written whole while the tooltip has room for it, so its muted glyphs are those of the position and of the place.
TEST_F(CodeEditorTest, WritesARelatedPlaceWholeWhileTheTooltipHasRoom) {
    const std::string place = "allocation.cpp:7:22";
    const json markers = json::array({{{"line", 1}, {"column", 1}, {"endColumn", 6}, {"tone", "danger"}, {"message", "Expected ')'"}, {"related", json::array({{{"place", place}, {"message", "To match this '('"}}})}}});
    mount({{"value", "alpha beta"}, {"markers", markers}, {"fontSize", 10}});
    const float line = m_harness.context().fonts().size(FontFace::Monospace, 10.0F);

    for (float x = 1.0F; x < 40.0F && tooltips().empty(); x += 1.0F) {
        m_harness.moveTo(ImVec2(x, line * 0.5F));
        m_harness.frames(2);
    }

    ASSERT_EQ(tooltips().size(), 1U);
    const ImU32 muted = m_harness.context().color(ThemeColor::TextMuted).packed();
    // clang-format off
    const auto wears = [muted](const ImDrawVert& vertex) { return vertex.col == muted; };
    const auto visible = [](char character) { return character != ' '; };
    // clang-format on
    const auto glyphs = std::ranges::count_if(std::string("Ln 1, Col 1") + place, visible);

    EXPECT_EQ(std::ranges::count_if(tooltips().front()->DrawList->VtxBuffer, wears), glyphs * 4);
}

// A word with a marker shows the text its plugin found under the marker, in the same tooltip.
TEST_F(CodeEditorTest, ShowsTheTextOfAWordUnderItsMarkers) {
    mount({{"value", "alpha beta"}, {"markers", json::array({{{"line", 1}, {"column", 1}, {"endColumn", 6}, {"tone", "danger"}, {"message", "Undefined name"}}})}, {"hovers", true}, {"fontSize", 10}});
    const float line = m_harness.context().fonts().size(FontFace::Monospace, 10.0F);
    std::optional<UiEvent> hovered;

    for (float x = 1.0F; x < 80.0F && !hovered.has_value(); x += 3.0F) {
        m_harness.moveTo(ImVec2(x, line * 0.5F));
        m_harness.frames(40);
        hovered = event("hover");
    }

    ASSERT_TRUE(hovered.has_value());
    ASSERT_EQ(hovered->value["column"], 1);
    const float before = tooltips().front()->Size.y;
    ASSERT_TRUE(command("hover-text", {{"line", 1}, {"column", 1}, {"text", "A global name"}}).hasValue());
    m_harness.frames(3);

    ASSERT_EQ(tooltips().size(), 1U);
    EXPECT_EQ(tonesShown(), (std::vector<ThemeColor>{ThemeColor::Danger}));
    EXPECT_GT(tooltips().front()->Size.y, before);
}

// The text of a row and its number sit in the middle of the line height, so the space a spaced row adds is shared above and below them.
TEST_F(CodeEditorTest, CentersTheTextAndTheNumberOfARowInItsLineHeight) {
    json colors = scheme();
    colors["text"] = "#123456";
    colors["currentLineNumber"] = "#654321";
    mount({{"value", "l"}, {"scheme", colors}, {"fontSize", 20}});
    const ImGuiWindow* window = editorWindow();
    const auto text = bounds(painted(IM_COL32(0x12, 0x34, 0x56, 0xFF)));
    const auto number = bounds(painted(IM_COL32(0x65, 0x43, 0x21, 0xFF)));
    const float lift = std::floor((row(20.0F) - glyphHeight(20.0F)) / 2.0F);

    ASSERT_NE(window, nullptr);
    ASSERT_TRUE(text.has_value());
    ASSERT_TRUE(number.has_value());
    EXPECT_GE(lift, 2.0F);
    EXPECT_NEAR(text->Min.y, std::trunc(window->Pos.y + lift) + glyph('l', 20.0F)->Y0, 0.5F);
    EXPECT_NEAR(number->Min.y, std::trunc(window->Pos.y + lift) + glyph('1', 20.0F)->Y0, 0.5F);
}

// A marker underlines its range in its tone at the bottom of its row, after a glyph margin of three glyphs, one digit and one glyph of space, and no line is tinted.
// Its message is left to the tooltip, so nothing is written after the text of its line.
TEST_F(CodeEditorTest, UnderlinesTheRangeOfAMarkerInItsTone) {
    mount({{"value", "alpha beta gamma"}, {"markers", json::array({{{"line", 1}, {"column", 7}, {"endColumn", 11}, {"tone", "warning"}, {"message", "Unused value"}}})}, {"fontSize", 10}});
    const ImGuiWindow* window = editorWindow();
    const Color warning = m_harness.context().color(ThemeColor::Warning);
    const float advance = glyph('#')->AdvanceX;
    const float text = window->Pos.x + 5.0F * advance;
    std::vector<ImVec2> underline;

    for (const ImVec2& point : painted(warning.packed())) {
        if (point.x > window->Pos.x + 4.0F * advance) {
            underline.push_back(point);
        }
    }

    const auto drawn = bounds(underline);
    // clang-format off
    const auto tinted = [&warning](const ImDrawVert& vertex) { const ImU32 alpha = vertex.col >> IM_COL32_A_SHIFT; return (vertex.col & 0x00FFFFFFU) == (warning.packed() & 0x00FFFFFFU) && alpha != 0U && alpha != 0xFFU; };
    // clang-format on

    ASSERT_TRUE(drawn.has_value());
    EXPECT_NEAR(drawn->Min.x, text + 6.0F * advance, 1.0F);
    EXPECT_GE(drawn->Max.x, text + 10.0F * advance - 1.0F);
    EXPECT_LE(drawn->Max.x, text + 10.0F * advance + row() * 0.25F);
    EXPECT_GE(drawn->Min.y, window->Pos.y + row() * 0.7F);
    EXPECT_LE(drawn->Max.y, window->Pos.y + row() + 1.0F);
    EXPECT_TRUE(std::ranges::none_of(window->DrawList->VtxBuffer, tinted));
}

// The glyph margin shows the icon of the strongest tone among the markers starting on each line, in the color of that tone.
TEST_F(CodeEditorTest, ShowsTheStrongestToneOfALineInTheGlyphMargin) {
    const json markers = json::array({{{"line", 1}, {"column", 1}, {"endColumn", 6}, {"tone", "warning"}, {"message", "Unused value"}}, {{"line", 1}, {"column", 7}, {"endColumn", 11}, {"tone", "danger"}, {"message", "Undefined name"}}, {{"line", 2}, {"tone", "information"}, {"message", "Second line"}}});
    mount({{"value", "alpha beta\nsecond"}, {"markers", markers}, {"fontSize", 10}});
    const ImGuiWindow* window = editorWindow();
    const float margin = window->Pos.x + 3.0F * glyph('#')->AdvanceX;
    // clang-format off
    const auto inMargin = [&](ThemeColor tone, float top) { const auto points = painted(m_harness.context().color(tone).packed()); return std::ranges::any_of(points, [&](const ImVec2& point) { return point.x < margin && point.y >= top && point.y <= top + row(); }); };
    // clang-format on

    EXPECT_TRUE(inMargin(ThemeColor::Danger, window->Pos.y));
    EXPECT_FALSE(inMargin(ThemeColor::Warning, window->Pos.y));
    EXPECT_TRUE(inMargin(ThemeColor::Information, window->Pos.y + row()));
}

// The current line is filled under its text in the current line color of the scheme, and the fill goes away while text is selected.
TEST_F(CodeEditorTest, FillsTheCurrentLineWhileNothingIsSelected) {
    json colors = scheme();
    colors["currentLine"] = "#203040";
    mount({{"value", "alpha\nbeta"}, {"scheme", colors}, {"fontSize", 10}});
    ASSERT_TRUE(command("reveal", {{"line", 2}, {"column", 2}}).hasValue());
    m_harness.frames(3);
    const ImGuiWindow* window = editorWindow();
    const auto outline = bounds(painted(IM_COL32(0x20, 0x30, 0x40, 0xFF)));

    ASSERT_TRUE(outline.has_value());
    EXPECT_NEAR(outline->Min.y, window->Pos.y + row(), 1.0F);
    EXPECT_NEAR(outline->Max.y, window->Pos.y + row() * 2.0F, 1.0F);
    EXPECT_NEAR(outline->Min.x, window->Pos.x + 5.0F * glyph('#')->AdvanceX, 1.0F);

    m_harness.press(tests::InterfaceHarness::command() | ImGuiKey_A);
    m_harness.frames(3);
    EXPECT_TRUE(painted(IM_COL32(0x20, 0x30, 0x40, 0xFF)).empty());

    // A line that wraps is filled on every row it takes, and the fill stays without line numbers.
    ASSERT_TRUE(m_harness.patch(2, {{"value", std::string(400, 'w')}, {"wordWrap", true}, {"lineNumbers", false}}).hasValue());
    ASSERT_TRUE(command("reveal", {{"line", 1}, {"column", 1}}).hasValue());
    m_harness.frames(3);
    const auto wrapped = bounds(painted(IM_COL32(0x20, 0x30, 0x40, 0xFF)));
    ASSERT_TRUE(wrapped.has_value());
    EXPECT_GT(wrapped->GetHeight(), row() * 1.5F);
}

// A pair of brackets draws its guide only beside the lines indented past it, so a block whose body is not indented, such as a namespace, draws none.
// A blank line counts as indented as its neighbors are, or just past the shallower one, so the guide crosses the blank lines of a body and not those between two blocks.
TEST_F(CodeEditorTest, DrawsTheGuideOfABlockOnlyBesideLinesIndentedPastIt) {
    json colors = scheme();
    colors["guide"] = "#0a0b0c";
    mount({{"value", "{\nouter\n\n{\n\n    inner\nlabel\n    last\n\n}\n}\nend"}, {"scheme", colors}, {"fontSize", 10}, {"language", {{"name", "Braces"}}}});
    ASSERT_TRUE(command("reveal", {{"line", 12}, {"column", 1}}).hasValue());
    m_harness.frames(3);
    const ImGuiWindow* window = editorWindow();
    ASSERT_NE(window, nullptr);
    const ImDrawList& list = *window->DrawList;
    const ImU32 guide = IM_COL32(0x0a, 0x0b, 0x0c, 0xFF);
    std::set<int> guided;

    // Each guide is one segment per line, whose triangles span the row of that line.
    for (const ImDrawCmd& drawn : list.CmdBuffer) {
        for (unsigned int index = drawn.IdxOffset; index + 2 < drawn.IdxOffset + drawn.ElemCount; index += 3) {
            const ImDrawVert& first = list.VtxBuffer[static_cast<int>(drawn.VtxOffset + list.IdxBuffer[static_cast<int>(index)])];
            const ImDrawVert& second = list.VtxBuffer[static_cast<int>(drawn.VtxOffset + list.IdxBuffer[static_cast<int>(index + 1)])];
            const ImDrawVert& third = list.VtxBuffer[static_cast<int>(drawn.VtxOffset + list.IdxBuffer[static_cast<int>(index + 2)])];

            if (first.col != guide || second.col != guide || third.col != guide) {
                continue;
            }

            const float top = std::min({first.pos.y, second.pos.y, third.pos.y}) - window->Pos.y;
            guided.insert(static_cast<int>(std::round(top / row())) + 1);
        }
    }

    EXPECT_EQ(guided, (std::set<int>{5, 6, 8, 9}));
}

// The other uses of a symbol are tinted in the occurrence color, faded code is covered by the background and struck code is crossed in the text color, each over the columns it names.
TEST_F(CodeEditorTest, DecoratesTheRangesItsPluginNames) {
    json colors = scheme();
    colors["occurrence"] = "#405060";
    colors["background"] = "#010203";
    colors["text"] = "#a0b0c0";
    mount({{"value", "local alpha = alpha\nold()"}, {"scheme", colors}, {"fontSize", 10}, {"decorations", json::array({{{"line", 1}, {"column", 7}, {"endLine", 1}, {"endColumn", 12}, {"style", "occurrence"}}, {{"line", 1}, {"column", 15}, {"endLine", 1}, {"endColumn", 20}, {"style", "faded"}}, {{"line", 2}, {"column", 1}, {"endLine", 2}, {"endColumn", 4}, {"style", "struck"}}})}});
    m_harness.frames(3);
    const float advance = glyph('#')->AdvanceX;
    const ImGuiWindow* window = editorWindow();
    ASSERT_NE(window, nullptr);

    const auto occurrence = bounds(painted(Widgets::ink(Color::rgb(0x40, 0x50, 0x60).withAlpha(0.45F))));
    ASSERT_TRUE(occurrence.has_value());
    EXPECT_NEAR(occurrence->GetWidth(), advance * 5.0F, 1.0F);
    EXPECT_NEAR(occurrence->Min.y, window->Pos.y, 1.0F);

    const auto faded = bounds(painted(Widgets::ink(Color::rgb(0x01, 0x02, 0x03).withAlpha(0.5F))));
    ASSERT_TRUE(faded.has_value());
    EXPECT_NEAR(faded->GetWidth(), advance * 5.0F, 1.0F);

    const auto struck = bounds(painted(IM_COL32(0xa0, 0xb0, 0xc0, 0xFF)));
    ASSERT_TRUE(struck.has_value());
    EXPECT_GT(struck->Max.y, window->Pos.y + row());

    EXPECT_EQ(m_harness.patch(2, {{"decorations", json::array({{{"line", 1}, {"column", 3}, {"endLine", 1}, {"endColumn", 2}, {"style", "occurrence"}}})}}).error().code, "editor_decoration_invalid");
    EXPECT_EQ(m_harness.patch(2, {{"decorations", json::array({{{"line", 1}, {"column", 1}, {"endLine", 1}, {"endColumn", 2}, {"style", "glowing"}}})}}).error().code, "editor_decoration_invalid");
}

// A secondary click moves the caret where it landed and opens the edit actions before the actions of the plugin, whose pick is reported.
TEST_F(CodeEditorTest, OpensItsEditActionsBeforeThoseOfItsPlugin) {
    mount({{"value", "alpha beta\ngamma"}, {"fontSize", 10}, {"menu", json::array({{{"id", "definition"}, {"text", "Go to Definition"}}})}});
    const ImGuiWindow* window = editorWindow();
    ASSERT_NE(window, nullptr);
    const float left = window->Pos.x + 5.0F * glyph('#')->AdvanceX;
    std::ignore = m_harness.takeEvents();

    m_harness.rightClick(ImVec2(left + glyph('#')->AdvanceX * 2.5F, window->Pos.y + row() * 1.5F));
    m_harness.frames(2);
    const auto moved = event("cursor");
    ASSERT_TRUE(moved.has_value());
    EXPECT_EQ(moved->value["line"], 2);
    EXPECT_EQ(moved->value["column"], 3);
    ASSERT_FALSE(GImGui->OpenPopupStack.empty());

    // The plugin action comes after the eight edit entries and their separators.
    for (int step = 0; step < 7; ++step) {
        m_harness.press(ImGuiKey_DownArrow);
    }

    m_harness.press(ImGuiKey_Enter);
    m_harness.frames(2);
    const auto picked = event("menu");
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(picked->value["item"], "definition");
}

// With definitions offered, a click on a word while the modifier is down asks for its definition at the clicked position.
TEST_F(CodeEditorTest, AsksForTheDefinitionOfAWordClickedWithTheModifier) {
    mount({{"value", "alpha beta"}, {"fontSize", 10}, {"definitions", true}});
    const ImGuiWindow* window = editorWindow();
    ASSERT_NE(window, nullptr);
    const float left = window->Pos.x + 5.0F * glyph('#')->AdvanceX;
    std::ignore = m_harness.takeEvents();

    const auto modifier = static_cast<ImGuiKey>(tests::InterfaceHarness::command());
    ImGui::GetIO().AddKeyEvent(modifier, true);
    m_harness.click(ImVec2(left + glyph('#')->AdvanceX * 7.5F, window->Pos.y + row() * 0.5F));
    ImGui::GetIO().AddKeyEvent(modifier, false);
    m_harness.frames(2);
    const auto asked = event("definition-request");
    ASSERT_TRUE(asked.has_value());
    EXPECT_EQ(asked->value, (json{{"line", 1}, {"column", 8}}));
}

} // namespace workpane::ui
