#include "ui/components/terminal/Terminal.h"

#include "localization/Localization.h"
#include "ui/ButtonState.h"
#include "ui/FontFamilies.h"
#include "ui/FontScope.h"
#include "ui/Fonts.h"
#include "ui/MenuItem.h"
#include "ui/Menus.h"
#include "ui/PseudoTerminalHost.h"
#include "ui/TerminalLaunch.h"
#include "ui/WheelScale.h"
#include "ui/WidgetHelper.h"
#include "ui/components/terminal/TerminalPalette.h"
#include "ui/model/ContentFontSize.h"
#include "ui/model/TextValue.h"
#include "ui/theme/Theme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace workpane::ui {

Terminal::Terminal(NodeId id) : Component(id) {}

Terminal::~Terminal() = default;

std::string_view Terminal::kind() const {
    return "terminal";
}

Result<void> Terminal::command(RenderContext& context, std::string_view name, const json::Json& arguments) {
    if (name == "find") {
        std::string text = m_findBar.text();
        bool caseSensitive = m_findBar.caseSensitive();
        bool wholeWord = m_findBar.wholeWord();
        json::ObjectReader reader(arguments, "terminal.find");
        reader.read("text", text, json::Presence::Optional).read("caseSensitive", caseSensitive, json::Presence::Optional).read("wholeWord", wholeWord, json::Presence::Optional);

        if (const auto finished = reader.finish(); !finished.hasValue()) {
            return finished;
        }

        if (m_screen == nullptr) {
            return Result<void>::failure({"terminal_not_running", "The terminal has not been drawn yet", {}});
        }

        m_findBar.setQuery(std::move(text), caseSensitive, wholeWord);
        openFind(context);
        return Result<void>::success();
    }

    // Taking the keyboard and starting again need no screen, so they answer even before the terminal was first drawn.
    if (name == "focus" && arguments.empty()) {
        m_focusRequested = true;
        context.requestFrame();
        return Result<void>::success();
    }

    // A restart keeps the directory the shell stood in and begins again with an empty screen.
    if (name == "restart" && arguments.empty()) {
        m_process.reset();
        m_exited = false;
        m_failure.clear();
        m_offset = 0;
        m_seenHistory = 0;
        m_directory = m_reportedDirectory.empty() ? m_directory : m_reportedDirectory;

        if (m_screen != nullptr) {
            m_screen->reset();
        }

        context.requestFrame();
        return Result<void>::success();
    }

    if (!arguments.empty() || (name != "copy" && name != "paste" && name != "clear")) {
        return Component::command(context, name, arguments);
    }

    if (m_screen == nullptr) {
        return Result<void>::failure({"terminal_not_running", "The terminal has not been drawn yet", {}});
    }

    if (name == "copy") {
        copy();
        return Result<void>::success();
    }

    if (name == "paste") {
        paste(context);
        return Result<void>::success();
    }

    if (name == "clear") {
        perform(context, TerminalKeys::Action::Clear);
        return Result<void>::success();
    }

    return Component::command(context, name, arguments);
}

// Detaching ends the shell, because nothing is left to show what it writes.
void Terminal::detach(RenderContext&) {
    m_process.reset();
}

Alignment Terminal::defaultRowAlignment() const {
    return Alignment::Stretch;
}

void Terminal::readProperties(json::ObjectReader& reader) {
    std::string shell = m_shell.string();
    std::string historyFile = m_historyFile.string();
    reader.read("directory", m_directory, json::Presence::Optional).read("palette", m_palette, json::Presence::Optional).read("clipboardWrites", m_clipboardWrites, json::Presence::Optional);
    reader.read("shell", shell, json::Presence::Optional).read("historyFile", historyFile, json::Presence::Optional);
    m_shell = std::filesystem::path(std::u8string(shell.begin(), shell.end()));
    m_historyFile = std::filesystem::path(std::u8string(historyFile.begin(), historyFile.end()));
    reader.readNumber("fontSize", m_fontSize, ContentFontSize::minimum, ContentFontSize::maximum, json::Presence::Optional).read("fontFamily", m_fontFamily, json::Presence::Optional).readInteger("history", m_history, 0, maximumHistory, json::Presence::Optional).readInteger("cursorBlink", m_cursorBlink, 0, longestBlink, json::Presence::Optional);
}

Component::Restore Terminal::keep() {
    return kept(m_directory, m_palette, m_clipboardWrites, m_shell, m_historyFile, m_fontSize, m_fontFamily, m_history, m_cursorBlink);
}

// The history can shrink without output, so a reader scrolled back never stands above its first line.
void Terminal::applied() {
    if (m_screen == nullptr) {
        return;
    }

    m_screen->setHistoryLimit(static_cast<std::size_t>(m_history));
    m_offset = std::min(m_offset, m_screen->history());
}

Result<void> Terminal::validate() const {
    if (m_directory.empty() || !std::filesystem::path(m_directory).is_absolute()) {
        return Result<void>::failure({"terminal_directory_invalid", "A terminal opens in an absolute directory", m_directory});
    }

    if (m_fontFamily.empty() || m_fontFamily.size() > FontFamilies::longestName) {
        return Result<void>::failure({"terminal_font_family_invalid", "A terminal names a font family of a bounded length", m_fontFamily.substr(0, FontFamilies::longestName)});
    }

    if (!TerminalPalette::named(m_palette).has_value()) {
        return Result<void>::failure({"terminal_palette_unknown", "A terminal names a color scheme it does not have", m_palette});
    }

    if (m_cursorBlink != 0 && m_cursorBlink < shortestBlink) {
        return Result<void>::failure({"terminal_blink_invalid", "A terminal blinks its cursor from 100 to 2000 milliseconds or keeps it steady with 0", std::to_string(m_cursorBlink)});
    }

    if ((!m_shell.empty() && !m_shell.is_absolute()) || (!m_historyFile.empty() && !m_historyFile.is_absolute())) {
        return Result<void>::failure({"terminal_path_invalid", "The shell and the history file of a terminal are absolute paths", m_shell.empty() ? m_historyFile.string() : m_shell.string()});
    }

    return Result<void>::success();
}

ImVec2 Terminal::measureContent(RenderContext& context, float availableWidth) {
    return {availableWidth, minimumHeight * context.scale()};
}

void Terminal::render(RenderContext& context, const ImRect& bounds) {
    m_drawnFrame = context.frame();
    context.families().request(m_fontFamily);
    const FontScope font(context.fonts(), FontFace::Monospace, static_cast<float>(m_fontSize), m_fontFamily);
    // The grid keeps whole points, because ImGui snaps every text position to one, so each glyph lands on its own cell instead of drifting with the advance of its face.
    m_cell = ImVec2(std::round(ImGui::CalcTextSize("M").x), std::round(ImGui::GetFontSize()));
    const float padding = contentPadding * context.scale();
    const ImRect text(bounds.Min + ImVec2(padding, padding), bounds.Max - ImVec2(padding, padding));
    const int columns = std::max(2, static_cast<int>(text.GetWidth() / m_cell.x));
    const int rows = std::max(1, static_cast<int>(text.GetHeight() / m_cell.y));
    const auto palette = *TerminalPalette::named(m_palette);

    ImDrawList& list = *ImGui::GetWindowDrawList();
    list.AddRectFilled(bounds.Min, bounds.Max, WidgetHelper::ink(palette.background()));
    const ImGuiID item = ImGui::GetID("##terminal");
    ImGui::SetCursorScreenPos(bounds.Min);
    ImGui::ItemSize(bounds.GetSize());
    std::ignore = ImGui::ItemAdd(bounds, item);

    if (m_screen == nullptr) {
        m_screen = std::make_unique<TerminalScreen>(rows, columns, static_cast<std::size_t>(m_history));
    }

    if (m_appliedPalette != m_palette) {
        m_screen->setPalette(palette);
        m_appliedPalette = m_palette;
    }

    if (m_process == nullptr && m_failure.empty()) {
        start(context, columns, rows);
    }

    if (!m_failure.empty()) {
        WidgetHelper::paragraph(context, list, context.font(ThemeFont::Interface), text, context.color(ThemeColor::DangerText), context.translate(failureKey(m_failure)), TextAlign::Center);
        return;
    }

    // A taller screen takes lines back from the history, so a reader scrolled back never stands above its first line.
    if (columns != m_screen->columns() || rows != m_screen->rows()) {
        m_screen->resize(rows, columns);
        m_process->resize(columns, rows);
        m_offset = std::min(m_offset, m_screen->history());
    }

    drain(context);
    focus(context, item, bounds);
    receive(context, bounds);

    if (m_focused && !m_findBar.typing()) {
        keyboard(context);
    }

    const bool scrolling = scrollbar(context, bounds);
    mouse(context, text, scrolling);
    drawCells(context, text);
    drawCursor(context, text);
    menu(context);

    if (m_focused && !m_findBar.typing() && m_offset == 0 && !m_exited) {
        placeCaret(text);
    }

    if (m_finder.active()) {
        drawFind(context, bounds);
    }

    // The frame that turns a blinking cursor on or off is asked for, and so is the one where the pause of the keyboard leaves it lit.
    if (blinking(context)) {
        const double phase = static_cast<double>(m_cursorBlink) / 1000.0;
        const double turn = m_blinkStart + (std::floor((context.time() - m_blinkStart) / phase) + 1.0) * phase;
        context.requestFrameAt(std::min(turn, m_blinkStart + blinkPause));
    }
}

// A terminal kept mounted off screen, on a shelf or behind another destination, keeps its shell running and reading at the size it last had.
void Terminal::update(RenderContext& context) {
    if (m_drawnFrame == context.frame() || !m_failure.empty()) {
        return;
    }

    if (m_screen == nullptr) {
        m_screen = std::make_unique<TerminalScreen>(hiddenRows, hiddenColumns, static_cast<std::size_t>(m_history));
    }

    if (m_process == nullptr) {
        start(context, m_screen->columns(), m_screen->rows());
    }

    if (m_process != nullptr) {
        drain(context);
    }
}

FontFace Terminal::face(const VTermScreenCellAttrs& attrs) {
    if (attrs.bold != 0 && attrs.italic != 0) {
        return FontFace::MonospaceBoldItalic;
    }

    if (attrs.bold != 0) {
        return FontFace::MonospaceBold;
    }

    return attrs.italic != 0 ? FontFace::MonospaceItalic : FontFace::Monospace;
}

std::string_view Terminal::failureKey(std::string_view code) {
    if (code == "terminal_directory_missing") {
        return "workpane.terminal.directory-missing";
    }

    if (code == "terminal_shell_not_executable") {
        return "workpane.terminal.shell-not-executable";
    }

    return "workpane.terminal.spawn-failed";
}

void Terminal::start(RenderContext& context, int columns, int rows) {
    auto started = context.terminals().start(TerminalLaunch{m_directory, columns, rows, m_shell, m_historyFile});

    if (!started.hasValue()) {
        m_failure = started.error().code;
        context.emit(id(), "error", {{"code", started.error().code}, {"message", started.error().message}});
        return;
    }

    m_process = std::move(started.value());
    m_screen->focus(m_focused);
}

// What the program wrote reaches the screen before anything the screen answers goes back to it, and the end of the program is told last.
// Output is read in bounded slices, so a program that floods the terminal never holds a frame for long and the rest is read in the frames after it.
void Terminal::drain(RenderContext& context) {
    const std::string output = m_process->takeOutput(outputSlice);

    if (output.size() == outputSlice) {
        context.requestFrame();
    }

    if (!output.empty()) {
        m_screen->write(output);
        send(context, m_screen->takeReply());
        follow();
    }

    refreshFind(context);

    TerminalSignals signals = m_screen->takeSignals();

    if (signals.title.has_value()) {
        context.emit(id(), "title", {{"title", *signals.title}});
    }

    if (signals.bell) {
        context.emit(id(), "bell");
    }

    for (const auto& [title, body] : signals.notifications) {
        context.emit(id(), "notification", {{"title", title}, {"body", body}});
    }

    if (signals.clipboard.has_value() && m_clipboardWrites) {
        ImGui::SetClipboardText(signals.clipboard->c_str());
    }

    // Output since the last reading of the directory leads to one more reading once the pause has passed, so the last change a program made is never missed.
    const bool reported = signals.directory.has_value() && !signals.directory->empty();
    m_directoryStale = m_directoryStale || !output.empty();
    const bool due = m_directoryStale && context.time() - m_directoryCheck >= directorySeconds;
    const std::string directory = reported ? *signals.directory : due ? m_process->directory() : std::string();
    m_directoryCheck = due ? context.time() : m_directoryCheck;
    m_directoryStale = m_directoryStale && !due;

    if (m_directoryStale) {
        context.requestFrameAt(m_directoryCheck + directorySeconds);
    }

    if (!directory.empty() && directory != m_reportedDirectory) {
        m_reportedDirectory = directory;
        context.emit(id(), "directory", {{"path", directory}});
    }

    if (const auto code = m_process->exitCode(); code.has_value() && !m_exited) {
        m_exited = true;
        context.emit(id(), "exit", {{"code", *code}});
    }
}

// A reader looking back through the history keeps seeing the same lines while the program adds more below them.
void Terminal::follow() {
    const long history = m_screen->history();

    if (m_offset > 0) {
        m_offset = std::min(history, m_offset + std::max(0L, history - m_seenHistory));
    }

    m_seenHistory = history;
    m_findStale = true;
}

// A search still reading the history goes on at every frame, and new output is read again a few times a second.
void Terminal::refreshFind(RenderContext& context) {
    if (!m_finder.active() || (!m_finder.searching() && !m_findStale)) {
        return;
    }

    // Output that arrived since the last reading is read once its pause has passed, even when nothing else draws.
    if (!m_finder.searching() && context.time() - m_findRefresh < findRefreshSeconds) {
        context.requestFrameAt(m_findRefresh + findRefreshSeconds);
        return;
    }

    m_findStale = false;

    m_findRefresh = context.time();
    const std::size_t count = m_finder.count();
    const std::size_t position = m_finder.position();
    const bool bounded = m_finder.bounded();
    const bool chosen = m_finder.refresh(*m_screen);

    if (m_finder.searching()) {
        context.requestFrame();
    }

    // A new search shows the match it chose, while new output moves the marks and the count only, so the view and the selection stay where the reader left them.
    if (chosen) {
        revealFind(context);
        return;
    }

    if (m_finder.count() != count || m_finder.position() != position || m_finder.bounded() != bounded) {
        reportFind(context);
    }
}

// A click inside the terminal gives it the keyboard, and it keeps every key while it has it, so the product never moves its focus away on a key the shell needs.
void Terminal::focus(RenderContext& context, ImGuiID item, const ImRect& bounds) {
    const bool hovered = ImGui::IsMouseHoveringRect(bounds.Min, bounds.Max) && ImGui::IsWindowHovered();
    const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);

    if (m_focusRequested || (hovered && clicked)) {
        ImGui::SetActiveID(item, ImGui::GetCurrentWindow());
        ImGui::SetFocusID(item, ImGui::GetCurrentWindow());
        m_focusRequested = false;
        m_findBar.release();
    }

    if (!hovered && clicked && ImGui::GetActiveID() == item) {
        ImGui::ClearActiveID();
    }

    const bool focused = ImGui::GetActiveID() == item;

    // The terminal owns every key while it has the keyboard except the product combinations, which reach the shortcuts of the product and its plugins.
    if (focused && TerminalKeys::product()) {
        GImGui->ActiveIdUsingAllKeyboardKeys = false;
    } else if (focused) {
        ImGui::SetActiveIdUsingAllKeyboardKeys();
    }

    // A control drawn before the terminal takes the first click while the terminal has the keyboard, because the terminal only lets the keyboard go once it is drawn.
    if (focused) {
        GImGui->ActiveIdAllowOverlap = true;
    }

    if (focused != m_focused) {
        m_focused = focused;
        m_blinkStart = context.time();
        m_screen->focus(focused);
        send(context, m_screen->takeReply());
        context.emit(id(), "focus", {{"focused", focused}});
    }
}

// The zoom keys change the size of this terminal only, the product actions answer before the program, and everything else reaches the program.
void Terminal::keyboard(RenderContext& context) {
    if (const auto step = ContentFontSize::shortcutStep(); step.has_value()) {
        m_fontSize = ContentFontSize::stepped(m_fontSize, *step);
        context.emit(id(), "zoom", {{"fontSize", m_fontSize}}, {{"fontSize", "fontSize"}});
        ImGui::GetIO().InputQueueCharacters.resize(0);
        return;
    }

    if (const auto action = TerminalKeys::action(!m_selection.empty(), m_finder.active()); action != TerminalKeys::Action::None) {
        perform(context, action);
        ImGui::GetIO().InputQueueCharacters.resize(0);
        return;
    }

    const long before = m_screen->history();
    TerminalKeys::send(*m_screen);
    const std::string reply = m_screen->takeReply();

    // Typing returns the view to the live screen and ends the selection, the way every terminal does.
    if (!reply.empty()) {
        m_offset = 0;
        m_seenHistory = before;
        m_blinkStart = context.time();
        m_selection.clear();
        send(context, reply);
    }
}

// A link opens with its modifier and a click even while the program has the mouse, so an address shown by a full screen program still opens.
// A program that asked for the mouse receives presses, releases, movement and the wheel in the protocol it chose, and shift keeps the gesture for the selection.
void Terminal::mouse(RenderContext& context, const ImRect& text, bool scrolling) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool hovered = ImGui::IsMouseHoveringRect(text.Min, text.Max) && ImGui::IsWindowHovered() && !scrolling;
    const TerminalPoint pointed = point(io.MousePos, text);
    const bool reporting = m_screen->mouseMode() != VTERM_PROP_MOUSE_NONE && !io.KeyShift && m_offset == 0;
    const int row = static_cast<int>(static_cast<long>(pointed.line - m_screen->dropped()) - firstLine());

    if (hovered && TerminalKeys::linkModifier() && link(pointed).has_value()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            context.emit(id(), "link", {{"url", *link(pointed)}});
            return;
        }
    }

    if (reporting && hovered) {
        m_screen->mouseMove(row, pointed.column, TerminalKeys::modifiers());

        for (std::size_t index = 0; index < reportedButtons.size(); ++index) {
            const auto button = static_cast<ImGuiMouseButton>(index);

            if (ImGui::IsMouseClicked(button)) {
                m_screen->mouseButton(reportedButtons[index], true, TerminalKeys::modifiers());
            }

            if (ImGui::IsMouseReleased(button)) {
                m_screen->mouseButton(reportedButtons[index], false, TerminalKeys::modifiers());
            }
        }

        report(WheelScale::distance());
        send(context, m_screen->takeReply());
        return;
    }

    // A secondary click offers what the terminal does with the pointer, since the program did not ask for it.
    // The clipboard is read once as the menu opens, since reading it on X11 waits for the program that owns it.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && context.claimMenuClick()) {
        const char* clipboard = ImGui::GetClipboardText();
        m_pasteable = clipboard != nullptr && *clipboard != '\0';
        ImGui::OpenPopup(menuName);
    }

    // The wheel moves the history as far as it moves any view and keeps what a trackpad moved less than a line, so a slow gesture still scrolls once it adds up to one.
    const float moved = WheelScale::distance().y;

    if (hovered && moved != 0.0F) {
        m_scrolled += moved / m_cell.y;
        const int lines = static_cast<int>(std::trunc(m_scrolled));
        m_scrolled -= static_cast<float>(lines);

        // A full screen program without the mouse scrolls with the arrows, which is what the wheel means to it.
        if (m_screen->alternateScreen()) {
            for (int step = 0; step < std::abs(lines); ++step) {
                m_screen->key(lines > 0 ? VTERM_KEY_UP : VTERM_KEY_DOWN, VTERM_MOD_NONE);
            }

            send(context, m_screen->takeReply());
        } else {
            m_offset = std::clamp(m_offset + lines, 0L, m_screen->history());
            m_seenHistory = m_screen->history();
        }
    }

    // A second click selects a word, a third the whole line, and alt held from the first click selects a rectangle.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && io.MouseClickedCount[ImGuiMouseButton_Left] >= 3) {
        m_selection.line(*m_screen, pointed);
        m_selecting = false;
        return;
    }

    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        m_selection.word(*m_screen, pointed);
        m_selecting = false;
        return;
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_selection.begin(pointed, io.KeyAlt);
        m_selecting = true;
    }

    // A drag held above or below the terminal moves the history under it.
    if (m_selecting && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (io.MousePos.y < text.Min.y) {
            m_offset = std::min(m_screen->history(), m_offset + 1);
            context.requestFrame();
        }

        if (io.MousePos.y > text.Max.y) {
            m_offset = std::max(0L, m_offset - 1);
            context.requestFrame();
        }

        m_selection.extend(point(io.MousePos, text));
    }

    if (m_selecting && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_selecting = false;
    }
}

// Every three lines the wheel moves on either axis are one report, which is a notch on Windows and Linux, and what a trackpad moved less than that waits for the next frame.
void Terminal::report(ImVec2 moved) {
    m_wheel += moved / (static_cast<float>(wheelLines) * m_cell.y);

    while (std::abs(m_wheel.y) >= 1.0F) {
        m_screen->mouseButton(m_wheel.y > 0.0F ? 4 : 5, true, TerminalKeys::modifiers());
        m_wheel.y -= std::copysign(1.0F, m_wheel.y);
    }

    while (std::abs(m_wheel.x) >= 1.0F) {
        m_screen->mouseButton(m_wheel.x > 0.0F ? 6 : 7, true, TerminalKeys::modifiers());
        m_wheel.x -= std::copysign(1.0F, m_wheel.x);
    }
}

// The menu offers copying while text is selected and pasting while the clipboard held text as the menu opened.
void Terminal::menu(RenderContext& context) {
    if (!ImGui::IsPopupOpen(menuName)) {
        return;
    }

    const std::vector<MenuItem> items{{"copy", TextValue::translated("workpane.terminal.copy"), std::nullopt, {}, !m_selection.empty(), false, false}, {"paste", TextValue::translated("workpane.terminal.paste"), std::nullopt, {}, m_pasteable && !m_exited, false, false}, {{}, {}, std::nullopt, {}, true, true, false}, {"select-all", TextValue::translated("workpane.terminal.select-all"), std::nullopt, {}, true, false, false}, {"clear", TextValue::translated("workpane.terminal.clear"), std::nullopt, {}, true, false, false}};
    const auto picked = Menus::popup(context, menuName, items);

    if (!picked.has_value()) {
        return;
    }

    const std::array<std::pair<std::string_view, TerminalKeys::Action>, 4> actions{{{"copy", TerminalKeys::Action::Copy}, {"paste", TerminalKeys::Action::Paste}, {"select-all", TerminalKeys::Action::SelectAll}, {"clear", TerminalKeys::Action::Clear}}};

    for (const auto& [name, action] : actions) {
        if (*picked == name) {
            perform(context, action);
        }
    }
}

// A dropped file reaches the shell as its quoted path, and a drop naming anything the shell would read as more than a path delivers nothing.
void Terminal::receive(RenderContext& context, const ImRect& bounds) {
    const auto dropped = context.takeDroppedFiles(bounds);

    if (!dropped.has_value() || m_exited) {
        return;
    }

    for (const auto& path : *dropped) {
        const std::string text = path.string();

        // clang-format off
        if (text.empty() || std::ranges::any_of(text, [](char character) { return static_cast<unsigned char>(character) < 0x20U || character == 0x7F; })) {
            return;
        }
        // clang-format on
    }

    send(context, context.terminals().quotePaths(*dropped, m_shell));
    m_focusRequested = true;
}

void Terminal::perform(RenderContext& context, TerminalKeys::Action action) {
    switch (action) {
    case TerminalKeys::Action::Copy:
        copy();
        return;
    case TerminalKeys::Action::Paste:
        paste(context);
        return;
    case TerminalKeys::Action::SelectAll:
        m_selection.all(*m_screen);
        return;
    case TerminalKeys::Action::Clear:
        m_screen->clearHistory();
        m_screen->write("\x1b[H\x1b[2J");
        m_offset = 0;
        m_seenHistory = 0;
        m_selection.clear();
        send(context, "\x0c");
        return;
    case TerminalKeys::Action::Find:
        // The reader opening find starts from the selected text when it stays on one line, the way searching from a selection works everywhere.
        if (const std::string selected = m_selection.text(*m_screen); !selected.empty() && selected.find('\n') == std::string::npos) {
            m_findBar.setQuery(selected, m_findBar.caseSensitive(), m_findBar.wholeWord());
        }

        openFind(context);
        return;
    case TerminalKeys::Action::FindNext:
    case TerminalKeys::Action::FindPrevious:
        m_finder.step(action == TerminalKeys::Action::FindNext ? 1 : -1);
        revealFind(context);
        return;
    case TerminalKeys::Action::LineUp:
    case TerminalKeys::Action::LineDown:
        m_offset = std::clamp(m_offset + (action == TerminalKeys::Action::LineUp ? 1L : -1L), 0L, m_screen->history());
        m_seenHistory = m_screen->history();
        return;
    case TerminalKeys::Action::PageUp:
    case TerminalKeys::Action::PageDown:
        m_offset = std::clamp(m_offset + (action == TerminalKeys::Action::PageUp ? 1L : -1L) * m_screen->rows(), 0L, m_screen->history());
        m_seenHistory = m_screen->history();
        return;
    case TerminalKeys::Action::Top:
    case TerminalKeys::Action::Bottom:
        m_offset = action == TerminalKeys::Action::Top ? m_screen->history() : 0L;
        m_seenHistory = m_screen->history();
        return;
    case TerminalKeys::Action::None:
        return;
    }
}

// Input the shell could not take, because too much was already waiting for it, is reported once until a write succeeds again.
void Terminal::send(RenderContext& context, std::string_view bytes) {
    if (bytes.empty() || m_process == nullptr || m_exited) {
        return;
    }

    const bool written = m_process->write(bytes);

    if (!written && !m_writeRefused) {
        context.emit(id(), "input-refused");
    }

    m_writeRefused = !written;
}

void Terminal::paste(RenderContext& context) {
    const char* clipboard = ImGui::GetClipboardText();

    if (clipboard == nullptr || *clipboard == '\0' || m_exited) {
        return;
    }

    m_screen->paste(clipboard);
    send(context, m_screen->takeReply());
    m_offset = 0;
    m_blinkStart = context.time();
}

void Terminal::copy() {
    const std::string text = m_selection.text(*m_screen);

    if (!text.empty()) {
        ImGui::SetClipboardText(text.c_str());
    }
}

void Terminal::openFind(RenderContext& context) {
    m_findBar.focus();
    m_finder.search(*m_screen, m_findBar.text(), m_findBar.caseSensitive(), m_findBar.wholeWord());
    revealFind(context);
}

// The match being read carries the selection, so copying it takes exactly the text that was found.
void Terminal::revealFind(RenderContext& context) {
    if (const auto current = m_finder.current(); current.has_value()) {
        m_selection.select(current->from, current->to);
        const long line = static_cast<long>(current->from.line - m_screen->dropped());
        const long first = firstLine();

        if (line < first || line >= first + m_screen->rows()) {
            m_offset = std::clamp(m_screen->history() - line + m_screen->rows() / 2, 0L, m_screen->history());
        }
    }

    reportFind(context);
}

void Terminal::reportFind(RenderContext& context) {
    context.emit(id(), "find", {{"count", m_finder.count()}, {"current", m_finder.position()}, {"bounded", m_finder.bounded()}});
}

// Cells are drawn in runs that share their face and colors, the background first, then the marks of the search and the selection, then the text and its lines.
void Terminal::drawCells(RenderContext& context, const ImRect& text) {
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const long first = firstLine();
    const Color selection = context.color(ThemeColor::Selection);
    const Color match = context.color(ThemeColor::Highlight);
    const float underline = std::max(1.0F, std::round(context.scale()));

    for (int row = 0; row < m_screen->rows(); ++row) {
        const long line = first + row;

        if (line >= m_screen->lines()) {
            break;
        }

        const float top = text.Min.y + static_cast<float>(row) * m_cell.y;
        const std::uint64_t stable = m_screen->dropped() + static_cast<std::uint64_t>(line);
        Run run{FontFace::Monospace, Color{}, {}};

        for (int column = 0; column < m_screen->columns(); ++column) {
            const VTermScreenCell cell = m_screen->cell(line, column);

            if (cell.chars[0] == 0xFFFFFFFFU) {
                continue;
            }

            const float left = text.Min.x + static_cast<float>(column) * m_cell.x;
            const ImRect area(left, top, left + m_cell.x * static_cast<float>(std::max<int>(1, cell.width)), top + m_cell.y);
            Color foreground = m_screen->color(cell.fg);
            Color background = m_screen->color(cell.bg);

            if (cell.attrs.reverse != 0) {
                std::swap(foreground, background);
            }

            if (cell.attrs.faint != 0) {
                foreground = Color::blend(foreground, background, faintShare);
            }

            if (cell.attrs.reverse != 0 || !VTERM_COLOR_IS_DEFAULT_BG(&cell.bg)) {
                list.AddRectFilled(area.Min, area.Max, WidgetHelper::ink(background));
            }

            const TerminalPoint point{stable, column};

            if (m_finder.active() && m_finder.marks(point)) {
                list.AddRectFilled(area.Min, area.Max, WidgetHelper::ink(match));
            }

            if (m_selection.contains(point)) {
                list.AddRectFilled(area.Min, area.Max, WidgetHelper::ink(selection));
            }

            // Glyphs of one face and color share one run, so the face is pushed once for all of them.
            const bool visible = cell.chars[0] != 0 && cell.chars[0] != ' ' && cell.attrs.conceal == 0;
            const FontFace glyphFace = face(cell.attrs);

            if (visible && (glyphFace != run.face || !(foreground == run.color))) {
                flush(context, text, top, run);
                run = {glyphFace, foreground, {}};
            }

            if (visible) {
                run.glyphs.push_back({column, m_screen->text(line, column, column + 1)});
            }

            if (cell.attrs.underline != 0) {
                list.AddRectFilled(ImVec2(area.Min.x, area.Max.y - underline * 2.0F), ImVec2(area.Max.x, area.Max.y - underline), WidgetHelper::ink(foreground));
            }

            if (cell.attrs.strike != 0) {
                list.AddRectFilled(ImVec2(area.Min.x, area.GetCenter().y), ImVec2(area.Max.x, area.GetCenter().y + underline), WidgetHelper::ink(foreground));
            }
        }

        flush(context, text, top, run);
    }
}

void Terminal::flush(RenderContext& context, const ImRect& text, float top, Run& run) const {
    if (run.glyphs.empty()) {
        return;
    }

    const FontScope font(context.fonts(), run.face, static_cast<float>(m_fontSize), m_fontFamily);
    const float y = top + (m_cell.y - ImGui::GetFontSize()) / 2.0F;
    const ImU32 ink = WidgetHelper::ink(run.color);
    ImDrawList& list = *ImGui::GetWindowDrawList();

    for (const Glyph& glyph : run.glyphs) {
        list.AddText(ImVec2(text.Min.x + static_cast<float>(glyph.column) * m_cell.x, y), ink, glyph.text.c_str(), glyph.text.c_str() + glyph.text.size());
    }

    run.glyphs.clear();
}

// The cursor blinks while the terminal has the keyboard, its plugin gave it an interval and its program did not ask for a steady one, until the keyboard rests for fifteen seconds.
bool Terminal::blinking(const RenderContext& context) const {
    return m_focused && m_cursorBlink > 0 && m_screen->cursor().blinking && context.time() - m_blinkStart < blinkPause;
}

// The cursor shows only on the live screen, filled while the terminal has the keyboard and outlined otherwise, and a blinking one is lit from the last input on.
void Terminal::drawCursor(RenderContext& context, const ImRect& text) {
    const TerminalCursor& cursor = m_screen->cursor();
    const double phase = static_cast<double>(m_cursorBlink) / 1000.0;
    const bool lit = !blinking(context) || std::fmod(context.time() - m_blinkStart, phase * 2.0) < phase;

    if (m_offset != 0 || !cursor.visible || !lit || m_exited) {
        return;
    }

    ImDrawList& list = *ImGui::GetWindowDrawList();
    const auto palette = *TerminalPalette::named(m_palette);
    const float left = text.Min.x + static_cast<float>(cursor.column) * m_cell.x;
    const float top = text.Min.y + static_cast<float>(cursor.row) * m_cell.y;
    const long line = m_screen->history() + cursor.row;
    const int width = std::max(1, static_cast<int>(m_screen->cell(line, cursor.column).width));
    const ImRect area(left, top, left + m_cell.x * static_cast<float>(width), top + m_cell.y);
    const float thickness = context.metric(ThemeMetric::CaretWidth);
    const ImU32 ink = WidgetHelper::ink(palette.cursor());

    if (cursor.shape == CursorShape::Underline) {
        list.AddRectFilled(ImVec2(area.Min.x, area.Max.y - thickness), area.Max, ink);
        return;
    }

    if (cursor.shape == CursorShape::Bar) {
        list.AddRectFilled(area.Min, ImVec2(area.Min.x + thickness, area.Max.y), ink);
        return;
    }

    if (!m_focused) {
        list.AddRect(area.Min, area.Max, ink, 0.0F, std::max(1.0F, std::round(context.scale())));
        return;
    }

    const std::string glyph = m_screen->text(line, cursor.column, cursor.column + width);
    list.AddRectFilled(area.Min, area.Max, ink);
    list.AddText(ImVec2(left, top + (m_cell.y - ImGui::GetFontSize()) / 2.0F), WidgetHelper::ink(palette.background()), glyph.c_str());
}

// The input method of the system opens its candidates at the cell of the cursor while the terminal has the keyboard, blinking or not.
void Terminal::placeCaret(const ImRect& text) const {
    const TerminalCursor& cursor = m_screen->cursor();
    ImGuiPlatformImeData& caret = GImGui->PlatformImeData;
    caret.WantVisible = true;
    caret.WantTextInput = true;
    caret.InputPos = ImVec2(text.Min.x + static_cast<float>(cursor.column) * m_cell.x, text.Min.y + static_cast<float>(cursor.row) * m_cell.y);
    caret.InputLineHeight = m_cell.y;
    caret.ViewportId = ImGui::GetWindowViewport()->ID;
}

// The scroll bar is a thin thumb along the right edge that the pointer drags, and a click on its track brings the thumb under the pointer.
// It answers whether the pointer is using it, so the same press never starts a selection underneath.
bool Terminal::scrollbar(RenderContext& context, const ImRect& bounds) {
    const long history = m_screen->history();

    if (history == 0) {
        m_scrollGrab.reset();
        return false;
    }

    const float width = scrollbarWidth * context.scale();
    const float height = bounds.GetHeight();
    const float thumb = std::max(height * static_cast<float>(m_screen->rows()) / static_cast<float>(m_screen->lines()), width * 4.0F);
    const float travel = std::max(1.0F, height - thumb);
    const float position = travel * static_cast<float>(firstLine()) / static_cast<float>(history);
    const ImRect track(bounds.Max.x - scrollbarReach * context.scale(), bounds.Min.y, bounds.Max.x, bounds.Max.y);
    const ButtonState state = WidgetHelper::interact(ImGui::GetID("##terminalScrollbar"), track);
    const float pointer = ImGui::GetIO().MousePos.y - bounds.Min.y;

    if (state.held && !m_scrollGrab.has_value()) {
        m_scrollGrab = pointer >= position && pointer <= position + thumb ? pointer - position : thumb / 2.0F;
    }

    if (!state.held) {
        m_scrollGrab.reset();
    }

    if (m_scrollGrab.has_value()) {
        const float fraction = std::clamp((pointer - *m_scrollGrab) / travel, 0.0F, 1.0F);
        m_offset = history - std::lround(fraction * static_cast<float>(history));
        m_seenHistory = history;
    }

    const ThemeColor shade = state.held ? ThemeColor::ScrollbarActive : state.hovered ? ThemeColor::ScrollbarHover : ThemeColor::Scrollbar;
    const float shown = travel * static_cast<float>(firstLine()) / static_cast<float>(history);
    const ImVec2 origin(bounds.Max.x - width - 2.0F * context.scale(), bounds.Min.y + shown);
    ImGui::GetWindowDrawList()->AddRectFilled(origin, origin + ImVec2(width, thumb), WidgetHelper::ink(context.color(shade)), width / 2.0F);

    return state.hovered || state.held;
}

// The find bar floats over the top of the content in a window of its own, so searching never takes rows from the program and a click on the bar never reaches it.
// The find bar searches again as its query changes, steps through the matches and gives the keyboard back to the terminal when it closes.
void Terminal::drawFind(RenderContext& context, const ImRect& bounds) {
    const ImRect area = FindBar::area(context, bounds, false);
    ImGui::SetCursorScreenPos(area.Min);
    FindBar::Outcome outcome;

    if (ImGui::BeginChild("##find", area.GetSize(), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground)) {
        outcome = m_findBar.draw(context, bounds, {m_finder.count(), m_finder.position(), m_finder.bounded()}, false);
    }

    ImGui::EndChild();

    if (outcome.searched) {
        m_finder.search(*m_screen, m_findBar.text(), m_findBar.caseSensitive(), m_findBar.wholeWord());
        revealFind(context);
    }

    if (outcome.step != 0) {
        m_finder.step(outcome.step);
        revealFind(context);
    }

    if (outcome.closed) {
        m_finder.close();
        m_focusRequested = true;
    }
}

TerminalPoint Terminal::point(ImVec2 position, const ImRect& text) const {
    // The position is bounded before it becomes a cell, since a pointer outside the window stands at a float no integer holds.
    const int row = static_cast<int>(std::clamp(std::floor((position.y - text.Min.y) / m_cell.y), 0.0F, static_cast<float>(m_screen->rows() - 1)));
    const int column = static_cast<int>(std::clamp(std::floor((position.x - text.Min.x) / m_cell.x), 0.0F, static_cast<float>(m_screen->columns() - 1)));
    const long line = std::min(firstLine() + row, m_screen->lines() - 1);
    return {m_screen->dropped() + static_cast<std::uint64_t>(line), column};
}

long Terminal::firstLine() const {
    return m_screen->history() - m_offset;
}

// An address is the word the blanks around it delimit, offered for web addresses and file addresses.
std::optional<std::string> Terminal::link(TerminalPoint point) const {
    const long line = static_cast<long>(point.line - m_screen->dropped());
    const std::string address = m_screen->link(m_screen->cell(line, point.column));

    // A hyperlink the program wrote with OSC 8 names its address itself, and only an address the product opens is a link.
    if (!address.empty()) {
        // clang-format off
        const bool opened = std::ranges::any_of(linkSchemes, [&address](std::string_view scheme) { return address.starts_with(scheme); });
        // clang-format on
        return opened ? std::optional<std::string>(address) : std::nullopt;
    }

    int from = point.column;
    int to = from;

    if (m_screen->text(line, point.column, point.column + 1) == " ") {
        return std::nullopt;
    }

    while (from > 0 && m_screen->text(line, from - 1, from) != " ") {
        --from;
    }

    while (to + 1 < m_screen->columns() && m_screen->text(line, to + 1, to + 2) != " ") {
        ++to;
    }

    std::string word = m_screen->text(line, from, to + 1);
    std::size_t start = std::string::npos;

    for (const std::string_view scheme : linkSchemes) {
        start = std::min(start, word.find(scheme));
    }

    if (start == std::string::npos) {
        return std::nullopt;
    }

    word.erase(0, start);
    word.erase(word.find_last_not_of(".,;:!?)]}>'\"") + 1);

    return word;
}

} // namespace workpane::ui
