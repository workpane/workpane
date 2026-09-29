#include "support/InterfaceHarness.h"
#include "support/Resources.h"
#include "support/SystemRecord.h"
#include "support/TerminalProcessRecord.h"
#include "support/TerminalRecord.h"
#include "ui/Color.h"
#include "ui/FindBar.h"
#include "ui/Fonts.h"
#include "ui/WheelScale.h"
#include "ui/components/terminal/TerminalPalette.h"
#include "ui/model/Component.h"
#include "ui/model/RenderContext.h"
#include "ui/model/SurfaceStore.h"
#include "ui/model/UiEvent.h"
#include "ui/theme/FontRole.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace workpane::ui {

using nlohmann::json;

// Drives a terminal against a recorded shell: the test writes what the shell prints and reads what the terminal sent it.
class TerminalTest : public ::testing::Test {
  protected:
    static constexpr int attempts{400};

    void mount(json properties = json::object()) {
        properties["directory"] = std::filesystem::temp_directory_path().string();
        properties["height"] = 400;
        const auto mounted = m_harness.mount({{"id", 1}, {"kind", "column"}, {"props", json::object()}, {"children", json::array({{{"id", 2}, {"kind", "terminal"}, {"props", properties}, {"children", json::array()}}})}});
        ASSERT_TRUE(mounted.hasValue()) << mounted.error().code;
        m_harness.frames(2);
    }

    [[nodiscard]] tests::TerminalProcessRecord& process() {
        return *m_harness.terminals().processes.back();
    }

    void print(std::string_view output) {
        {
            const std::lock_guard lock(process().mutex);
            process().output += output;
        }

        m_harness.frames(2);
    }

    [[nodiscard]] std::string sent() {
        const std::lock_guard lock(process().mutex);
        return std::exchange(process().input, {});
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

    // The middle of a cell of a terminal mounted at the top left corner, whose text starts past its padding.
    // Turns the wheel as far as a number of lines of the terminal on each axis, handed to ImGui as the window hands it a step of the system.
    void wheel(float across, float down) {
        const float line = (cell(0, 1).y - cell(0, 0).y) / m_harness.context().scale();
        const ImVec2 units = ui::WheelScale::units(ImVec2(across, down), line);
        ImGui::GetIO().AddMouseWheelEvent(units.x, units.y);
    }

    ImVec2 cell(int column, int row) {
        const ui::Fonts& fonts = m_harness.context().fonts();
        const float scale = m_harness.context().scale();
        const float size = std::round(fonts.size(FontFace::Monospace, 11.0F) * scale);
        const float width = std::round(fonts.face(FontFace::Monospace)->CalcTextSizeA(size, FLT_MAX, 0.0F, "M").x);
        const float padding = 6.0F * scale;
        return {padding + (static_cast<float>(column) + 0.5F) * width, padding + (static_cast<float>(row) + 0.5F) * size};
    }

    // The cursor is drawn only on the live screen, so its color on screen tells whether the history is scrolled.
    [[nodiscard]] static bool cursorShown() {
        const ImU32 ink = TerminalPalette::named("balanced")->cursor().packed();

        for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists) {
            for (const ImDrawVert& vertex : list->VtxBuffer) {
                if (vertex.col == ink) {
                    return true;
                }
            }
        }

        return false;
    }

    // Draws frames and answers whether the cursor showed in each one.
    [[nodiscard]] std::vector<bool> cursorFrames(int frames) {
        std::vector<bool> shown;

        for (int frame = 0; frame < frames; ++frame) {
            m_harness.frame();
            shown.push_back(cursorShown());
        }

        return shown;
    }

    [[nodiscard]] static std::size_t count(const std::string& text, std::string_view part) {
        std::size_t found = 0;

        for (std::size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + part.size())) {
            ++found;
        }

        return found;
    }

    tests::InterfaceHarness m_harness;
};

TEST_F(TerminalTest, StartsTheShellInItsDirectoryAtTheSizeItIsDrawn) {
    mount();

    ASSERT_EQ(m_harness.terminals().processes.size(), 1U);
    EXPECT_EQ(process().launch.directory, std::filesystem::temp_directory_path());
    EXPECT_GT(process().launch.columns, 40);
    EXPECT_GT(process().launch.rows, 5);

    // A property that names a relative directory, shell or history file or an unknown scheme is refused whole.
    EXPECT_FALSE(m_harness.patch(2, {{"directory", "relative/path"}}).hasValue());
    EXPECT_EQ(m_harness.patch(2, {{"shell", "bin/zsh"}}).error().code, "terminal_path_invalid");
    EXPECT_EQ(m_harness.patch(2, {{"historyFile", "one.history"}}).error().code, "terminal_path_invalid");
    EXPECT_FALSE(m_harness.patch(2, {{"palette", "neon"}}).hasValue());
    EXPECT_FALSE(m_harness.patch(2, {{"history", -1}}).hasValue());
}

TEST_F(TerminalTest, ReportsTheTitleDirectoryBellAndEndOfItsProgram) {
    mount();
    print("\x1b]0;build logs\x07");
    EXPECT_EQ(event("title")->value["title"], "build logs");

    print("\x1b]7;file://host/tmp/work%20space\x07");
    EXPECT_EQ(event("directory")->value["path"], "/tmp/work space");
    print("\x1b]7;file://host/tmp/half%4gescape\x07");
    EXPECT_EQ(event("directory")->value["path"], "/tmp/half%4gescape");

    print("\x07");
    EXPECT_TRUE(event("bell").has_value());

    // The end arrives after the last output, which the screen already shows.
    {
        const std::lock_guard lock(process().mutex);
        process().output += "bye\r\n";
        process().exit = 3;
    }

    m_harness.frames(2);
    EXPECT_EQ(event("exit")->value["code"], 3);
}

TEST_F(TerminalTest, SendsWhatTheReaderTypesAndPastes) {
    mount();
    m_harness.click(ImVec2(200.0F, 100.0F));
    EXPECT_EQ(event("focus")->value["focused"], true);

    m_harness.type("ls");
    m_harness.press(ImGuiKey_Enter);
    EXPECT_EQ(sent(), "ls\r");

    // A program that asked for bracketed paste receives the text between the markers, with its line breaks turned into Enter.
    print("\x1b[?2004h");
    ImGui::SetClipboardText("one\ntwo");
    ASSERT_TRUE(command("paste").hasValue());
    EXPECT_EQ(sent(), "\x1b[200~one\rtwo\x1b[201~");

    // Pasted control characters are left out, so a clipboard cannot end the paste early and run what follows.
    ImGui::SetClipboardText("safe\x1b[201~rm -rf x\x03\tend");
    ASSERT_TRUE(command("paste").hasValue());
    EXPECT_EQ(sent(), "\x1b[200~safe[201~rm -rf x\tend\x1b[201~");
}

// A string a program never ends is dropped once it passes its bound, and the next one arrives whole.
TEST_F(TerminalTest, DropsAStringThatPassesItsBound) {
    mount();
    print("\x1b]0;" + std::string(8192, 'x') + "\x07");
    EXPECT_FALSE(event("title").has_value());

    print("\x1b]0;short\x07");
    EXPECT_EQ(event("title")->value["title"], "short");
}

// Every glyph sits on its own cell, so forty bold letters start forty cells apart however wide the emboldened face advances.
TEST_F(TerminalTest, DrawsEveryGlyphOnItsOwnCell) {
    mount({{"fontSize", 10}});
    print("\x1b[1;38;2;1;2;3mWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW\x1b[0mX\r\n");
    const ImU32 ink = IM_COL32(1, 2, 3, 255);
    const ui::Fonts& fonts = m_harness.context().fonts();
    const float size = fonts.size(FontFace::Monospace, 10.0F);
    const float cell = std::round(fonts.face(FontFace::Monospace)->CalcTextSizeA(size, FLT_MAX, 0.0F, "M").x);
    const float glyph = fonts.face(FontFace::MonospaceBold)->CalcTextSizeA(size, FLT_MAX, 0.0F, "W").x;
    float left = FLT_MAX;
    float right = -FLT_MAX;

    for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists) {
        for (const ImDrawVert& vertex : list->VtxBuffer) {
            if (vertex.col == ink) {
                left = std::min(left, vertex.pos.x);
                right = std::max(right, vertex.pos.x);
            }
        }
    }

    ASSERT_LT(left, right);
    EXPECT_LE(right - left, cell * 39.0F + std::ceil(glyph) + 1.0F);
}

// The input method of the system learns the cell of the cursor while the terminal has the keyboard, and nothing once it lost it.
TEST_F(TerminalTest, TellsTheInputMethodWhereItsCursorStands) {
    mount();
    print("abc");
    m_harness.frames(2);
    EXPECT_FALSE(GImGui->PlatformImeData.WantVisible);

    m_harness.click(cell(8, 4));
    print("");
    const ImGuiPlatformImeData caret = GImGui->PlatformImeData;
    ASSERT_TRUE(caret.WantVisible);
    EXPECT_TRUE(caret.WantTextInput);
    const ImVec2 expected = cell(3, 0);
    EXPECT_LT(caret.InputPos.x, expected.x);
    EXPECT_LT(caret.InputPos.y, expected.y);
    EXPECT_GT(caret.InputPos.x + caret.InputLineHeight, expected.x);
    EXPECT_GT(caret.InputPos.y + caret.InputLineHeight, expected.y);

    ASSERT_TRUE(command("find", {{"text", "abc"}}).hasValue());
    m_harness.frames(2);
    EXPECT_FALSE(GImGui->PlatformImeData.WantVisible && GImGui->PlatformImeData.InputPos.x == caret.InputPos.x && GImGui->PlatformImeData.InputPos.y == caret.InputPos.y);
}

// A family of the machine is read in the background and then writes the whole grid, so the columns follow the width of its cells.
TEST_F(TerminalTest, WritesInTheFamilyOfTheMachineOnceItWasRead) {
    m_harness.system().fonts = {{"Inter", tests::Resources::fonts() / "Inter-Regular.ttf"}};
    mount({{"fontSize", 10}, {"fontFamily", "Inter"}});
    const ui::Fonts& fonts = m_harness.context().fonts();

    for (int attempt = 0; attempt < attempts && !fonts.has("Inter"); ++attempt) {
        m_harness.frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    ASSERT_TRUE(fonts.has("Inter"));
    m_harness.frames(2);
    const float bundled = std::round(fonts.face(FontFace::Monospace)->CalcTextSizeA(fonts.size(FontFace::Monospace, 10.0F), FLT_MAX, 0.0F, "M").x);
    const float chosen = std::round(fonts.face(FontFace::Monospace, "Inter")->CalcTextSizeA(fonts.size(FontFace::Monospace, 10.0F, "Inter"), FLT_MAX, 0.0F, "M").x);
    const int before = process().launch.columns;
    const std::lock_guard lock(process().mutex);
    ASSERT_FALSE(process().sizes.empty());
    const int after = process().sizes.back().first;
    EXPECT_GT(chosen, bundled);
    EXPECT_LT(after, before);
    EXPECT_LE(std::abs(static_cast<float>(after) * chosen - static_cast<float>(before) * bundled), chosen + bundled);
}

// A family the machine does not have and a name too long for a family are told apart, the first drawn in the bundled face and the second refused.
TEST_F(TerminalTest, DrawsAFamilyTheMachineLacksInTheBundledFace) {
    mount({{"fontSize", 10}, {"fontFamily", "Nothing Mono"}});
    m_harness.frames(4);
    EXPECT_FALSE(m_harness.context().fonts().has("Nothing Mono"));
    EXPECT_EQ(m_harness.context().fonts().face(FontFace::Monospace, "Nothing Mono"), m_harness.context().fonts().face(FontFace::Monospace));

    const auto refused = m_harness.patch(2, {{"fontFamily", std::string(300, 'a')}});
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.error().code, "terminal_font_family_invalid");
    EXPECT_EQ(m_harness.patch(2, {{"fontFamily", ""}}).error().code, "terminal_font_family_invalid");
}

// A control drawn before the terminal, such as a destination of the mode bar, answers the first click while the terminal has the keyboard.
TEST_F(TerminalTest, LetsAControlDrawnBeforeItTakeTheFirstClick) {
    const json terminal = {{"id", 2}, {"kind", "terminal"}, {"props", {{"directory", std::filesystem::temp_directory_path().string()}, {"height", 300}}}, {"children", json::array()}};
    const json button = {{"id", 3}, {"kind", "button"}, {"props", {{"text", "Elsewhere"}}}, {"children", json::array()}};
    ASSERT_TRUE(m_harness.mount({{"id", 1}, {"kind", "column"}, {"props", {{"padding", 0}}}, {"children", json::array({button, terminal})}}).hasValue());
    m_harness.frames(2);
    const ImVec2 size = m_harness.node(3)->measure(m_harness.context(), tests::InterfaceHarness::width);

    m_harness.click(ImVec2(200.0F, size.y + 100.0F));
    ASSERT_EQ(event("focus")->value["focused"], true);

    m_harness.click(ImVec2(size.x / 2.0F, size.y / 2.0F));
    const auto events = m_harness.takeEvents();
    // clang-format off
    const auto clicked = [](const UiEvent& candidate) { return candidate.name == "click"; };
    const auto released = [](const UiEvent& candidate) { return candidate.name == "focus" && candidate.value["focused"] == false; };
    // clang-format on
    EXPECT_TRUE(std::ranges::any_of(events, clicked));
    EXPECT_TRUE(std::ranges::any_of(events, released));
}

TEST_F(TerminalTest, CopiesTheSelectionWithoutPaddingAndClearsTheHistory) {
    mount();
    print("first line   \r\nsecond\r\n");
    m_harness.click(cell(1, 0));
    m_harness.press(ImGui::GetIO().ConfigMacOSXBehaviors ? (tests::InterfaceHarness::command() | ImGuiKey_A) : (ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_A));
    ASSERT_TRUE(command("copy").hasValue());
    const std::string copied = ImGui::GetClipboardText();

    EXPECT_EQ(copied.substr(0, 18), "first line\nsecond\n");

    // Clearing asks the shell for its prompt again.
    ASSERT_TRUE(command("clear").hasValue());
    EXPECT_EQ(sent(), "\x0c");
}

// An event a command of the terminal raises carries the surface of the terminal, whichever surface the frame visited last.
TEST_F(TerminalTest, TagsTheEventsOfItsCommandsWithItsSurface) {
    mount();
    const json label = {{"id", 1}, {"kind", "label"}, {"props", {{"text", "Last"}}}, {"children", json::array()}};
    ASSERT_TRUE(m_harness.surfaces().mount("view:zzz:last", "zzz", {}, label, m_harness.context()).hasValue());
    print("find me\r\n");
    std::ignore = m_harness.takeEvents();

    ASSERT_TRUE(m_harness.surfaces().command(tests::InterfaceHarness::surface, "test", 2, "find", {{"text", "find"}}, m_harness.context()).hasValue());
    m_harness.frame();
    const auto found = event("find");

    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->surface, tests::InterfaceHarness::surface);
}

TEST_F(TerminalTest, FindsEveryMatchAndStepsThroughThem) {
    mount();
    print("alpha beta\r\nbeta gamma\r\nalphabet\r\n");
    ASSERT_TRUE(command("find", {{"text", "beta"}}).hasValue());
    const auto found = event("find");

    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->value["count"], 2);
    EXPECT_EQ(found->value["current"], 2);

    // The find keys of the platform step through the open search while the terminal has the keyboard.
    m_harness.frames(2);
    m_harness.click(cell(1, 3));
    m_harness.press(ImGui::GetIO().ConfigMacOSXBehaviors ? (tests::InterfaceHarness::command() | ImGuiKey_G) : ImGuiKey_F3);
    const auto stepped = event("find");
    ASSERT_TRUE(stepped.has_value());
    EXPECT_EQ(stepped->value["current"], 1);

    // A whole word leaves out the word it is only a part of.
    ASSERT_TRUE(command("find", {{"text", "alpha"}, {"wholeWord", true}}).hasValue());
    EXPECT_EQ(event("find")->value["count"], 1);
    ASSERT_TRUE(command("find", {{"text", "ALPHA"}, {"caseSensitive", true}}).hasValue());
    EXPECT_EQ(event("find")->value["count"], 0);
}

// A search of a long history reads a bounded part of it at each frame, counts with a bound until it reaches the rows on screen and then reads the newest match.
TEST_F(TerminalTest, FindsInALongHistoryOverSeveralFrames) {
    mount({{"history", 10000}});
    std::string lines;

    for (int line = 0; line < 7000; ++line) {
        lines += line % 10 == 0 ? "line item\r\n" : "line\r\n";
    }

    print(lines);
    ASSERT_TRUE(command("find", {{"text", "item"}}).hasValue());
    const auto started = event("find");
    ASSERT_TRUE(started.has_value());
    EXPECT_EQ(started->value["bounded"], true);
    EXPECT_EQ(started->value["current"], 0);
    EXPECT_LT(started->value["count"].get<int>(), 700);
    std::optional<UiEvent> finished;

    for (int frame = 0; frame < 20 && !finished.has_value(); ++frame) {
        m_harness.frame();

        if (const auto found = event("find"); found.has_value() && !found->value["bounded"].get<bool>()) {
            finished = found;
        }
    }

    ASSERT_TRUE(finished.has_value());
    EXPECT_EQ(finished->value["count"], 700);
    EXPECT_EQ(finished->value["current"], 700);
}

// An open search follows new output with its marks and its count, while the selection stays where the reader left it.
TEST_F(TerminalTest, FollowsNewOutputWithoutMovingTheReader) {
    mount();
    print("beta\r\n");
    ASSERT_TRUE(command("find", {{"text", "beta"}}).hasValue());
    EXPECT_EQ(event("find")->value["count"], 1);

    m_harness.drag(cell(0, 0), cell(2, 0));
    print("more beta\r\n");
    m_harness.frames(30);
    const auto followed = event("find");

    ASSERT_TRUE(followed.has_value());
    EXPECT_EQ(followed->value["count"], 2);
    ASSERT_TRUE(command("copy").hasValue());
    EXPECT_EQ(std::string(ImGui::GetClipboardText()), "bet");
}

// The matches of lines that went into the history are kept, and the matches of lines the history dropped are forgotten.
TEST_F(TerminalTest, CountsTheMatchesTheHistoryStillHolds) {
    mount({{"history", 5}});
    print("beta\r\n");
    ASSERT_TRUE(command("find", {{"text", "beta"}}).hasValue());
    EXPECT_EQ(event("find")->value["count"], 1);

    std::string filler;

    for (int line = 0; line < 25; ++line) {
        filler += "x\r\n";
    }

    print(filler);
    m_harness.frames(30);
    EXPECT_FALSE(event("find").has_value());

    print(filler);
    m_harness.frames(30);
    const auto forgotten = event("find");
    ASSERT_TRUE(forgotten.has_value());
    EXPECT_EQ(forgotten->value["count"], 0);
}

// A click on the find bar reaches the bar alone, even while the program asked for the mouse.
TEST_F(TerminalTest, KeepsTheClicksOnTheFindBarFromTheProgram) {
    mount();
    print("\x1b[?1000h\x1b[?1006h");
    ASSERT_TRUE(command("find", {{"text", "x"}}).hasValue());
    m_harness.frames(2);
    std::ignore = sent();
    const ImRect bar = ui::FindBar::area(m_harness.context(), ImRect(ImVec2(0.0F, 0.0F), ImVec2(tests::InterfaceHarness::width, 400.0F)), false);

    m_harness.click(bar.GetCenter());
    m_harness.frames(2);
    EXPECT_EQ(sent(), "");
}

// A terminal not drawn yet refuses by name every command that needs its screen, and a command it does not know stays unknown.
TEST_F(TerminalTest, RefusesTheCommandsOfItsScreenBeforeItIsDrawn) {
    const auto terminal = json{{"id", 2}, {"kind", "terminal"}, {"props", {{"directory", std::filesystem::temp_directory_path().string()}, {"height", 400}}}, {"children", json::array()}};
    ASSERT_TRUE(m_harness.mount({{"id", 1}, {"kind", "column"}, {"props", json::object()}, {"children", json::array({terminal})}}).hasValue());

    for (const std::string name : {"copy", "paste", "clear", "find"}) {
        const auto refused = m_harness.node(2)->command(m_harness.context(), name, json::object());
        ASSERT_FALSE(refused.hasValue()) << name;
        EXPECT_EQ(refused.error().code, "terminal_not_running") << name;
    }

    EXPECT_EQ(m_harness.node(2)->command(m_harness.context(), "scroll", json::object()).error().code, "ui_command_unknown");
    EXPECT_TRUE(m_harness.node(2)->command(m_harness.context(), "focus", json::object()).hasValue());
}

// A program that asked for the mouse receives the right and middle buttons as themselves, one report for every three lines the wheel moves on both axes, which is a notch on Windows and Linux, and a trackpad gesture once it adds up to three lines.
TEST_F(TerminalTest, ReportsTheButtonsAndEveryNotchOfTheWheelToItsProgram) {
    mount();
    print("\x1b[?1000h\x1b[?1006h");
    std::ignore = sent();
    ImGuiIO& io = ImGui::GetIO();

    m_harness.rightClick(cell(4, 2));
    EXPECT_EQ(count(sent(), "\x1b[<2;"), 2U);
    EXPECT_EQ(GImGui->OpenPopupStack.Size, 0);

    io.AddMouseButtonEvent(ImGuiMouseButton_Middle, true);
    m_harness.frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Middle, false);
    m_harness.frame();
    EXPECT_EQ(count(sent(), "\x1b[<1;"), 2U);

    wheel(0.0F, 9.5F);
    m_harness.frame();
    EXPECT_EQ(count(sent(), "\x1b[<64;"), 3U);
    wheel(6.5F, 0.0F);
    m_harness.frame();
    EXPECT_EQ(count(sent(), "\x1b[<66;"), 2U);

    for (int step = 0; step < 3; ++step) {
        wheel(0.0F, -1.2F);
        m_harness.frame();
    }

    EXPECT_EQ(count(sent(), "\x1b[<65;"), 1U);
}

// Without a program asking for the mouse, a trackpad scrolls the history once its small movements add up to a line.
TEST_F(TerminalTest, ScrollsItsHistoryByWhatATrackpadAddsUp) {
    mount();
    std::string lines;

    for (int line = 0; line < 120; ++line) {
        lines += "line " + std::to_string(line) + "\r\n";
    }

    print(lines);
    m_harness.moveTo(cell(4, 2));
    ASSERT_TRUE(cursorShown());

    wheel(0.0F, 0.4F);
    m_harness.frame();
    wheel(0.0F, 0.4F);
    m_harness.frame();
    EXPECT_TRUE(cursorShown());

    wheel(0.0F, 0.4F);
    m_harness.frames(2);
    EXPECT_FALSE(cursorShown());
}

// Shift with the paging keys moves through the history, shift with end comes back, and shift with insert pastes.
TEST_F(TerminalTest, PagesThroughItsHistoryAndPastesWithShift) {
    mount();
    std::string lines;

    for (int line = 0; line < 120; ++line) {
        lines += "line " + std::to_string(line) + "\r\n";
    }

    print(lines);
    m_harness.click(cell(4, 2));
    std::ignore = sent();

    m_harness.press(ImGuiMod_Shift | ImGuiKey_PageUp);
    EXPECT_FALSE(cursorShown());
    m_harness.press(ImGuiMod_Shift | ImGuiKey_End);
    EXPECT_TRUE(cursorShown());
    m_harness.press(ImGuiMod_Shift | ImGuiKey_Home);
    EXPECT_FALSE(cursorShown());
    m_harness.press(ImGuiMod_Shift | ImGuiKey_PageDown);
    m_harness.press(ImGuiMod_Shift | ImGuiKey_End);
    EXPECT_TRUE(cursorShown());
    EXPECT_EQ(sent(), "");

    ImGui::SetClipboardText("pasted");
    m_harness.press(ImGuiMod_Shift | ImGuiKey_Insert);
    EXPECT_EQ(sent(), "pasted");
}

// A reader scrolled to the top of the history keeps a valid view when the history shrinks without output, as when a smaller text brings lines back onto a taller screen or the history limit falls.
TEST_F(TerminalTest, KeepsItsViewWhenTheHistoryShrinksWhileScrolledBack) {
    mount();
    std::string lines;

    for (int line = 0; line < 120; ++line) {
        lines += "line " + std::to_string(line) + "\r\n";
    }

    print(lines);
    m_harness.click(cell(4, 2));
    m_harness.press(ImGuiMod_Shift | ImGuiKey_Home);
    EXPECT_FALSE(cursorShown());

    ASSERT_TRUE(m_harness.patch(2, {{"fontSize", 8}}).hasValue());
    m_harness.frames(3);
    ASSERT_TRUE(m_harness.patch(2, {{"history", 10}}).hasValue());
    m_harness.frames(3);
    EXPECT_FALSE(cursorShown());

    m_harness.press(ImGuiMod_Shift | ImGuiKey_End);
    EXPECT_TRUE(cursorShown());
}

// A second click selects a word, a third the whole line, and a drag with alt held selects the same columns of every line it crosses.
TEST_F(TerminalTest, SelectsALineOnATripleClickAndARectangleWithAlt) {
    mount();
    print("alpha beta\r\ngamma delta\r\n");

    m_harness.click(cell(1, 0));
    m_harness.click(cell(1, 0));
    m_harness.click(cell(1, 0));
    ASSERT_TRUE(command("copy").hasValue());
    EXPECT_EQ(std::string(ImGui::GetClipboardText()), "alpha beta");

    ImGui::GetIO().AddKeyEvent(ImGuiMod_Alt, true);
    m_harness.frames(20);
    m_harness.drag(cell(0, 0), cell(4, 1));
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Alt, false);
    m_harness.frame();
    ASSERT_TRUE(command("copy").hasValue());
    EXPECT_EQ(std::string(ImGui::GetClipboardText()), "alpha\ngamma");
}

// The menu reads the clipboard once as it opens, however many frames the pointer moves over it, since reading it on X11 waits for the program that owns it.
// A program writes the clipboard with an OSC 52 sequence, as a yank of Vim or a copy of tmux over SSH does, only while its plugin allows it, and a sequence arriving before that leaves the clipboard as it was.
TEST_F(TerminalTest, WritesTheClipboardForItsProgramOnlyWhileAllowed) {
    static std::string clipboard;
    // clang-format off
    ImGui::GetPlatformIO().Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) { clipboard = text; };
    // clang-format on
    mount();

    print("\x1b]52;c;cmVmdXNlZA==\x07");
    EXPECT_EQ(clipboard, "");

    ASSERT_TRUE(m_harness.patch(2, {{"clipboardWrites", true}}).hasValue());
    print("\x1b]52;c;aGVsbG8gd29ybGQ=\x07");
    EXPECT_EQ(clipboard, "hello world");

    print("\x1b]52;c;bGFzdCBsaW5l\x1b\\");
    EXPECT_EQ(clipboard, "last line");
}

// A cursor stays steady until its plugin gives it an interval, then blinks from the last key while the terminal has the keyboard, stays steady while its program asks for a steady one and rests lit once the keyboard paused for fifteen seconds.
TEST_F(TerminalTest, BlinksItsCursorOnlyAtTheIntervalItsPluginGives) {
    mount();
    m_harness.click(cell(4, 2));
    const std::vector<bool> steady = cursorFrames(90);
    EXPECT_EQ(std::ranges::count(steady, false), 0);

    ASSERT_TRUE(m_harness.patch(2, {{"cursorBlink", 300}}).hasValue());
    m_harness.type("a");
    EXPECT_TRUE(cursorShown());
    const std::vector<bool> blinking = cursorFrames(50);
    EXPECT_EQ(std::count(blinking.begin(), blinking.begin() + 16, false), 0);
    EXPECT_EQ(std::count(blinking.begin() + 19, blinking.begin() + 34, true), 0);
    EXPECT_EQ(std::count(blinking.begin() + 37, blinking.end(), false), 0);
    EXPECT_LE(m_harness.context().frameDeadline() - m_harness.context().time(), 0.9 - 50.0 / 60.0 + 1e-6);

    print("\x1b[2 q");
    m_harness.type("b");
    const std::vector<bool> asked = cursorFrames(50);
    EXPECT_EQ(std::ranges::count(asked, false), 0);

    print("\x1b[1 q");
    m_harness.type("c");
    const std::vector<bool> paused = cursorFrames(15 * 60 + 30);
    EXPECT_GT(std::ranges::count(paused, false), 0);
    EXPECT_EQ(std::count(paused.end() - 20, paused.end(), false), 0);
    EXPECT_TRUE(std::isinf(m_harness.context().frameDeadline()));

    m_harness.click(ImVec2(20.0F, 600.0F));
    const std::vector<bool> away = cursorFrames(50);
    EXPECT_EQ(std::ranges::count(away, false), 0);

    EXPECT_EQ(m_harness.patch(2, {{"cursorBlink", 50}}).error().code, "terminal_blink_invalid");
    EXPECT_EQ(m_harness.patch(2, {{"cursorBlink", 2001}}).error().code, "json_field_range");
    EXPECT_TRUE(m_harness.patch(2, {{"cursorBlink", 0}}).hasValue());
}

TEST_F(TerminalTest, ReadsTheClipboardOnceForItsMenu) {
    mount();
    static int reads = 0;
    // clang-format off
    ImGui::GetPlatformIO().Platform_GetClipboardTextFn = [](ImGuiContext*) -> const char* { ++reads; return "pasted"; };
    // clang-format on
    m_harness.rightClick(cell(1, 0));
    ASSERT_EQ(GImGui->OpenPopupStack.Size, 1);

    for (int step = 0; step < 10; ++step) {
        m_harness.moveTo(ImVec2(cell(1, 0).x + static_cast<float>(step) * 4.0F, cell(1, 0).y + static_cast<float>(step) * 6.0F));
    }

    EXPECT_EQ(reads, 1);
}

// A secondary click offers the actions of the terminal while its program does not have the mouse, and the find keys step through an open search only.
TEST_F(TerminalTest, OffersItsMenuAndStepsThroughAnOpenSearch) {
    mount();
    print("beta\r\nbeta\r\n");
    m_harness.rightClick(cell(1, 0));
    EXPECT_EQ(GImGui->OpenPopupStack.Size, 1);
    m_harness.press(ImGuiKey_Escape);
    std::ignore = sent();

    const bool mac = ImGui::GetIO().ConfigMacOSXBehaviors;
    const ImGuiKeyChord next = mac ? (tests::InterfaceHarness::command() | ImGuiKey_G) : ImGuiKey_F3;
    m_harness.click(cell(1, 0));
    m_harness.press(next);
    EXPECT_EQ(sent().empty(), mac);

    ASSERT_TRUE(command("find", {{"text", "beta"}}).hasValue());
    EXPECT_EQ(event("find")->value["current"], 2);
    m_harness.frames(10);
    m_harness.click(cell(1, 3));
    m_harness.press(next);
    const auto stepped = event("find");
    ASSERT_TRUE(stepped.has_value());
    EXPECT_EQ(stepped->value["current"], 1);

    // A click on the terminal takes the keyboard back from the find field, so typing reaches the program again.
    std::ignore = sent();
    m_harness.type("x");
    EXPECT_EQ(sent(), "x");
}

// The scroll bar moves the history when its thumb is dragged, and a click on its track brings the thumb under the pointer.
TEST_F(TerminalTest, ScrollsItsHistoryFromItsScrollBar) {
    mount();
    std::string lines;

    for (int line = 0; line < 120; ++line) {
        lines += "line " + std::to_string(line) + "\r\n";
    }

    print(lines);
    const float edge = tests::InterfaceHarness::width - 3.0F * m_harness.context().scale();
    ASSERT_TRUE(cursorShown());

    m_harness.drag(ImVec2(edge, 395.0F), ImVec2(edge, 5.0F));
    EXPECT_FALSE(cursorShown());
    std::ignore = m_harness.takeEvents();

    m_harness.click(ImVec2(edge, 399.0F));
    m_harness.frame();
    EXPECT_TRUE(cursorShown());
    EXPECT_FALSE(ImGui::GetIO().MouseDown[ImGuiMouseButton_Left]);
}

// A program notifies through the ninth and the 777th operating system commands, and a progress report of another terminal is no notification.
TEST_F(TerminalTest, ReportsTheNotificationsOfItsProgram) {
    mount();
    print("\x1b]9;Build finished\x07");
    auto notified = event("notification");
    ASSERT_TRUE(notified.has_value());
    EXPECT_EQ(notified->value, (json{{"title", ""}, {"body", "Build finished"}}));

    print("\x1b]777;notify;Tests;All passed\x07");
    notified = event("notification");
    ASSERT_TRUE(notified.has_value());
    EXPECT_EQ(notified->value, (json{{"title", "Tests"}, {"body", "All passed"}}));

    print("\x1b]9;4;1;50\x07");
    EXPECT_FALSE(event("notification").has_value());
}

// Input the shell has not taken is reported once until a write goes through again, and a file address is a link like a web one.
TEST_F(TerminalTest, ReportsRefusedInputOnceAndOpensFileAddresses) {
    mount();
    m_harness.click(cell(4, 2));
    process().refusesInput = true;
    m_harness.type("a");
    m_harness.type("b");
    auto events = m_harness.takeEvents();
    // clang-format off
    const auto refused = [](const UiEvent& candidate) { return candidate.name == "input-refused"; };
    // clang-format on
    EXPECT_EQ(std::ranges::count_if(events, refused), 1);

    process().refusesInput = false;
    m_harness.type("c");
    process().refusesInput = true;
    m_harness.type("d");
    events = m_harness.takeEvents();
    EXPECT_EQ(std::ranges::count_if(events, refused), 1);

    print("\r\nfile:///tmp/report.txt\r\n");
    const auto modifier = static_cast<ImGuiKey>(tests::InterfaceHarness::command());
    ImGui::GetIO().AddKeyEvent(modifier, true);
    m_harness.click(cell(3, 1));
    ImGui::GetIO().AddKeyEvent(modifier, false);
    m_harness.frame();
    const auto linked = event("link");
    ASSERT_TRUE(linked.has_value());
    EXPECT_EQ(linked->value["url"], "file:///tmp/report.txt");
}

// Faint text is drawn halfway to the background, and SGR 22 ends it with bold.
TEST_F(TerminalTest, DrawsFaintTextDimmed) {
    mount();
    print("\x1b[2;38;2;200;100;0mdim\x1b[22m bright\r\n");
    const TerminalPalette palette = *TerminalPalette::named("balanced");
    const ImU32 dimmed = Color::blend(Color::rgb(200, 100, 0), palette.background(), 0.5F).packed();
    const ImU32 plain = IM_COL32(200, 100, 0, 255);
    bool dim = false;
    bool bright = false;

    for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists) {
        for (const ImDrawVert& vertex : list->VtxBuffer) {
            dim = dim || vertex.col == dimmed;
            bright = bright || vertex.col == plain;
        }
    }

    EXPECT_TRUE(dim);
    EXPECT_TRUE(bright);
}

// The cursor over a wide character covers both of its cells.
TEST_F(TerminalTest, CoversAWideCharacterWithTheCursor) {
    mount();
    m_harness.click(cell(4, 4));
    print("\xe4\xb8\xad\x1b[1G");
    const ImU32 ink = TerminalPalette::named("balanced")->cursor().packed();
    float left = FLT_MAX;
    float right = -FLT_MAX;

    for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists) {
        for (const ImDrawVert& vertex : list->VtxBuffer) {
            if (vertex.col == ink) {
                left = std::min(left, vertex.pos.x);
                right = std::max(right, vertex.pos.x);
            }
        }
    }

    ASSERT_LT(left, right);
    EXPECT_NEAR(right - left, (cell(1, 0).x - cell(0, 0).x) * 2.0F, 1.0F);
}

// A hyperlink written with OSC 8 opens its own address from any of its cells, text between links is plain, and an address the product cannot open is no link.
TEST_F(TerminalTest, OpensTheHyperlinksItsProgramWrites) {
    mount();
    print("\x1b]8;;https://workpane.dev/docs\x1b\\read the docs\x1b]8;;\x1b\\ then \x1b]8;id=7;mailto:someone@example.com\x1b\\mail\x1b]8;;\x1b\\\r\n");
    const auto modifier = static_cast<ImGuiKey>(tests::InterfaceHarness::command());

    // clang-format off
    const auto clickAt = [&](int column) { std::ignore = m_harness.takeEvents(); ImGui::GetIO().AddKeyEvent(modifier, true); m_harness.click(cell(column, 0)); ImGui::GetIO().AddKeyEvent(modifier, false); m_harness.frame(); return event("link"); };
    // clang-format on
    const auto first = clickAt(1);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->value["url"], "https://workpane.dev/docs");

    const auto space = clickAt(4);
    ASSERT_TRUE(space.has_value());
    EXPECT_EQ(space->value["url"], "https://workpane.dev/docs");

    EXPECT_FALSE(clickAt(15).has_value());
    EXPECT_FALSE(clickAt(20).has_value());
}

// The addresses of the hyperlinks one terminal remembers are bounded in bytes, and a link past the bound is written as plain text.
TEST_F(TerminalTest, BoundsTheBytesOfTheHyperlinksItRemembers) {
    mount();
    const int rows = process().launch.rows;
    std::string output;

    for (int link = 0; link < 70; ++link) {
        output += "\x1b]8;;https://example.com/" + std::string(60000, static_cast<char>('a' + link % 26)) + std::to_string(link) + "\x1b\\L\x1b]8;;\x1b\\\r\n";
    }

    print(output);
    m_harness.frames(80);
    const auto modifier = static_cast<ImGuiKey>(tests::InterfaceHarness::command());
    // clang-format off
    const auto clickAt = [&](int row) { std::ignore = m_harness.takeEvents(); ImGui::GetIO().AddKeyEvent(modifier, true); m_harness.click(cell(0, row)); ImGui::GetIO().AddKeyEvent(modifier, false); m_harness.frame(); return event("link"); };
    // clang-format on
    const auto kept = clickAt(rows - 3);

    ASSERT_TRUE(kept.has_value());
    EXPECT_EQ(kept->value["url"], "https://example.com/" + std::string(60000, static_cast<char>('a' + 68 % 26)) + "68");
    EXPECT_FALSE(clickAt(rows - 2).has_value());
}

// The decimal key of the keypad types one period, as every other key of the keypad types its character once.
TEST_F(TerminalTest, TypesOnePeriodForTheDecimalKeyOfTheKeypad) {
    mount();
    m_harness.click(cell(0, 0));
    std::ignore = sent();
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiKey_KeypadDecimal, true);
    io.AddInputCharacter('.');
    m_harness.frame();
    io.AddKeyEvent(ImGuiKey_KeypadDecimal, false);
    m_harness.frame();

    EXPECT_EQ(sent(), ".");
}

// A drag that leaves the window keeps its selection, since a pointer outside the window is bounded before it becomes a cell.
TEST_F(TerminalTest, KeepsASelectionWhoseDragLeavesTheWindow) {
    mount();
    print("alpha beta\r\n");
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(cell(4, 0).x, cell(4, 0).y);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    m_harness.frame();
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    m_harness.frames(2);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    m_harness.frame();

    ASSERT_TRUE(command("copy").hasValue());
    EXPECT_EQ(std::string(ImGui::GetClipboardText()), "alpha");
}

// A terminal that keeps no history still counts the lines that scroll away, so a selection stays on the text it covers.
TEST_F(TerminalTest, KeepsASelectionOnItsTextWithoutAHistory) {
    mount({{"history", 0}});
    print("first\r\nbeta\r\n");
    ASSERT_TRUE(command("find", {{"text", "beta"}}).hasValue());
    std::string filler;

    for (int line = 0; line < process().launch.rows - 2; ++line) {
        filler += "x\r\n";
    }

    print(filler);

    ASSERT_TRUE(command("copy").hasValue());
    EXPECT_EQ(std::string(ImGui::GetClipboardText()), "beta");
}

// A new palette reaches the lines of the history as it reaches the screen, so text scrolled back is drawn in the colors in use.
TEST_F(TerminalTest, DrawsTheHistoryInTheColorsOfANewPalette) {
    mount({{"palette", "balanced"}});
    std::string output = "history\r\n";

    for (int line = 0; line < process().launch.rows; ++line) {
        output += "\r\n";
    }

    print(output);
    m_harness.click(cell(0, 0));
    m_harness.press(ImGuiMod_Shift | ImGuiKey_Home);
    ASSERT_TRUE(m_harness.patch(2, {{"palette", "vivid"}}).hasValue());
    m_harness.frames(2);
    const ImU32 previous = TerminalPalette::named("balanced")->foreground().packed();
    const ImU32 current = TerminalPalette::named("vivid")->foreground().packed();
    ASSERT_NE(previous, current);
    bool stale = false;
    bool fresh = false;

    for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists) {
        for (const ImDrawVert& vertex : list->VtxBuffer) {
            stale = stale || vertex.col == previous;
            fresh = fresh || vertex.col == current;
        }
    }

    EXPECT_TRUE(fresh);
    EXPECT_FALSE(stale);
}

TEST_F(TerminalTest, ReportsAShellThatCouldNotStart) {
    m_harness.terminals().refusal = "terminal_directory_missing";
    mount();

    EXPECT_EQ(event("error")->value["code"], "terminal_directory_missing");
    EXPECT_TRUE(m_harness.terminals().processes.empty());
}

TEST_F(TerminalTest, WritesTheQuotedPathsOfFilesDroppedOnIt) {
    mount();
    m_harness.context().dropFiles({"/tmp/one file.txt", "/tmp/two"}, ImVec2(200.0F, 100.0F));
    m_harness.frames(1);

    EXPECT_EQ(sent(), "'/tmp/one file.txt' '/tmp/two' ");

    // A drop outside the terminal, or one naming a path with a line break, delivers nothing.
    m_harness.context().dropFiles({"/tmp/outside"}, ImVec2(2000.0F, 2000.0F));
    m_harness.frames(2);
    m_harness.context().dropFiles({"/tmp/one\nrm"}, ImVec2(200.0F, 100.0F));
    m_harness.frames(1);

    EXPECT_TRUE(sent().empty());
}

// The directory is read again once output pauses, so a change the shell made just before it went quiet is still reported.
TEST_F(TerminalTest, ReadsTheDirectoryAgainAfterTheLastOutput) {
    mount();
    {
        const std::lock_guard lock(process().mutex);
        process().directory = "/tmp/first";
    }

    m_harness.frames(40);
    print("one");
    EXPECT_EQ(event("directory")->value["path"], "/tmp/first");

    {
        const std::lock_guard lock(process().mutex);
        process().directory = "/tmp/second";
    }

    print("two");
    EXPECT_FALSE(event("directory").has_value());
    m_harness.frames(40);
    EXPECT_EQ(event("directory")->value["path"], "/tmp/second");
}

TEST_F(TerminalTest, KeepsItsShellReadingWhileItIsNotDrawn) {
    mount({{"visible", false}});

    ASSERT_EQ(m_harness.terminals().processes.size(), 1U);
    EXPECT_EQ(process().launch.columns, 80);
    EXPECT_EQ(process().launch.rows, 24);

    print("\x1b]0;hidden build\x07");
    EXPECT_EQ(event("title")->value["title"], "hidden build");

    // Shown again, it takes the size it is drawn at without starting another shell.
    ASSERT_TRUE(m_harness.patch(2, {{"visible", true}}).hasValue());
    m_harness.frames(2);
    EXPECT_EQ(m_harness.terminals().processes.size(), 1U);
    const std::lock_guard lock(process().mutex);
    ASSERT_FALSE(process().sizes.empty());
    EXPECT_GT(process().sizes.back().first, 80);
}

TEST_F(TerminalTest, ZoomsItsOwnTextWhileItHasTheKeyboard) {
    mount({{"fontSize", 10}});
    m_harness.click(ImVec2(200.0F, 100.0F));
    m_harness.press(tests::InterfaceHarness::command() | ImGuiKey_Equal);

    EXPECT_EQ(event("zoom")->value["fontSize"], 11.0);
    EXPECT_TRUE(sent().empty());
}

} // namespace workpane::ui
