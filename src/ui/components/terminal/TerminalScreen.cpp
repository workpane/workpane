#include "ui/components/terminal/TerminalScreen.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace workpane::ui {

TerminalScreen::TerminalScreen(int rows, int columns, std::size_t historyLimit) : m_historyLimit(historyLimit) {
    m_terminal = vterm_new(rows, columns);
    vterm_set_utf8(m_terminal, 1);
    m_screen = vterm_obtain_screen(m_terminal);
    m_state = vterm_obtain_state(m_terminal);

    m_callbacks.movecursor = &TerminalScreen::cursorMoved;
    m_callbacks.settermprop = &TerminalScreen::propertyChanged;
    m_callbacks.bell = &TerminalScreen::rang;
    m_callbacks.sb_pushline = &TerminalScreen::pushed;
    m_callbacks.sb_popline = &TerminalScreen::popped;
    m_callbacks.sb_clear = &TerminalScreen::cleared;
    m_fallbacks.osc = &TerminalScreen::commanded;
    m_selection.set = &TerminalScreen::selected;

    // The screen reflows its lines when its width changes and keeps an alternate screen for full screen programs.
    vterm_screen_set_callbacks(m_screen, &m_callbacks, this);
    vterm_screen_set_unrecognised_fallbacks(m_screen, &m_fallbacks, this);
    vterm_state_set_selection_callbacks(m_state, &m_selection, this, m_selectionBuffer.data(), m_selectionBuffer.size());
    vterm_output_set_callback(m_terminal, &TerminalScreen::replied, this);
    vterm_screen_enable_altscreen(m_screen, 1);
    vterm_screen_enable_reflow(m_screen, true);
    vterm_screen_reset(m_screen, 1);
}

TerminalScreen::~TerminalScreen() {
    vterm_free(m_terminal);
}

void TerminalScreen::write(std::string_view bytes) {
    vterm_input_write(m_terminal, bytes.data(), bytes.size());
    vterm_screen_flush_damage(m_screen);
}

void TerminalScreen::resize(int rows, int columns) {
    if (rows == this->rows() && columns == this->columns()) {
        return;
    }

    vterm_set_size(m_terminal, rows, columns);
    vterm_screen_flush_damage(m_screen);
}

// Indexed colors resolve through the scheme, and the default colors of the cells already on screen change with it.
void TerminalScreen::setPalette(const TerminalPalette& palette) {
    VTermColor foreground{};
    VTermColor background{};
    const Color text = palette.foreground();
    const Color back = palette.background();
    vterm_color_rgb(&foreground, text.red, text.green, text.blue);
    vterm_color_rgb(&background, back.red, back.green, back.blue);
    vterm_screen_set_default_colors(m_screen, &foreground, &background);

    for (std::size_t index = 0; index < 16; ++index) {
        const Color ansi = palette.ansi(index);
        VTermColor color{};
        vterm_color_rgb(&color, ansi.red, ansi.green, ansi.blue);
        vterm_state_set_palette_color(m_state, static_cast<int>(index), &color);
    }
}

void TerminalScreen::setHistoryLimit(std::size_t limit) {
    m_historyLimit = limit;

    while (m_history.size() > m_historyLimit) {
        m_history.pop_front();
        ++m_dropped;
    }
}

void TerminalScreen::key(VTermKey key, VTermModifier modifiers) {
    vterm_keyboard_key(m_terminal, key, modifiers);
}

void TerminalScreen::character(std::uint32_t character, VTermModifier modifiers) {
    vterm_keyboard_unichar(m_terminal, character, modifiers);
}

// A program that asked for bracketed paste receives the text between the markers, so a shell never runs a pasted line by itself.
// Line breaks reach the program as the carriage return the Enter key sends.
// Control characters other than tabs and line breaks are left out, so pasted text can never end the paste early or run a sequence of its own.
void TerminalScreen::paste(std::string_view text) {
    vterm_keyboard_start_paste(m_terminal);

    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto byte = static_cast<unsigned char>(text[index]);
        const bool crlf = byte == '\r' && index + 1 < text.size() && text[index + 1] == '\n';
        const bool control = (byte < 0x20U && byte != '\t' && byte != '\n' && byte != '\r') || byte == 0x7FU;
        const bool eightBitControl = byte == 0xC2U && index + 1 < text.size() && static_cast<unsigned char>(text[index + 1]) >= 0x80U && static_cast<unsigned char>(text[index + 1]) <= 0x9FU;

        if (eightBitControl) {
            ++index;
            continue;
        }

        if (crlf || control) {
            continue;
        }

        m_reply += byte == '\n' ? '\r' : text[index];
    }

    vterm_keyboard_end_paste(m_terminal);
}

void TerminalScreen::mouseMove(int row, int column, VTermModifier modifiers) {
    vterm_mouse_move(m_terminal, row, column, modifiers);
}

void TerminalScreen::mouseButton(int button, bool pressed, VTermModifier modifiers) {
    vterm_mouse_button(m_terminal, button, pressed, modifiers);
}

void TerminalScreen::focus(bool focused) {
    if (focused) {
        vterm_state_focus_in(m_state);
        return;
    }

    vterm_state_focus_out(m_state);
}

void TerminalScreen::clearHistory() {
    m_dropped += m_history.size();
    m_history.clear();
}

void TerminalScreen::reset() {
    clearHistory();
    vterm_screen_reset(m_screen, 1);
    m_linkNumbers.clear();
    m_links.clear();
    m_linkBytes = 0;
    m_title = {};
    m_cursor = {};
    m_mouseMode = 0;
    m_alternate = false;
}

std::string TerminalScreen::takeReply() {
    return std::exchange(m_reply, {});
}

TerminalSignals TerminalScreen::takeSignals() {
    return std::exchange(m_signals, {});
}

int TerminalScreen::rows() const {
    int rows = 0;
    int columns = 0;
    vterm_get_size(m_terminal, &rows, &columns);

    return rows;
}

int TerminalScreen::columns() const {
    int rows = 0;
    int columns = 0;
    vterm_get_size(m_terminal, &rows, &columns);

    return columns;
}

long TerminalScreen::history() const {
    return static_cast<long>(m_history.size());
}

long TerminalScreen::lines() const {
    return history() + rows();
}

std::uint64_t TerminalScreen::dropped() const {
    return m_dropped;
}

VTermScreenCell TerminalScreen::cell(long line, int column) const {
    if (line < history()) {
        const auto& cells = m_history[static_cast<std::size_t>(line)].cells;
        return static_cast<std::size_t>(column) < cells.size() ? cells[static_cast<std::size_t>(column)] : blank();
    }

    VTermScreenCell cell{};
    vterm_screen_get_cell(m_screen, VTermPos{static_cast<int>(line - history()), column}, &cell);

    return cell;
}

// A line of the history remembers whether the next one continues it, and a row on screen asks whether the row below it continues it.
bool TerminalScreen::wraps(long line) const {
    if (line < history()) {
        return m_history[static_cast<std::size_t>(line)].wraps;
    }

    const long row = line - history();

    if (row + 1 >= rows()) {
        return false;
    }

    const VTermLineInfo* next = vterm_state_get_lineinfo(m_state, static_cast<int>(row + 1));
    return next != nullptr && next->continuation != 0;
}

std::string TerminalScreen::text(long line, int from, int to) const {
    std::string written;

    for (int column = std::max(0, from); column < std::min(to, columns()); ++column) {
        written += utf8(cell(line, column));
    }

    return written;
}

// A cell written inside an OSC 8 hyperlink answers its address, and every other cell answers nothing.
std::string TerminalScreen::link(const VTermScreenCell& cell) const {
    if (cell.attrs.link == 0 || cell.chars[0] == 0 || cell.attrs.link > m_links.size()) {
        return {};
    }

    return m_links[cell.attrs.link - 1];
}

// A default color answers the default of the palette in use, since the cells of the history keep the default of the palette they were written with.
Color TerminalScreen::color(VTermColor color) const {
    if (VTERM_COLOR_IS_DEFAULT_FG(&color) || VTERM_COLOR_IS_DEFAULT_BG(&color)) {
        VTermColor foreground{};
        VTermColor background{};
        vterm_state_get_default_colors(m_state, &foreground, &background);
        color = VTERM_COLOR_IS_DEFAULT_FG(&color) ? foreground : background;
    }

    vterm_screen_convert_color_to_rgb(m_screen, &color);

    return Color::rgb(color.rgb.red, color.rgb.green, color.rgb.blue);
}

const TerminalCursor& TerminalScreen::cursor() const {
    return m_cursor;
}

int TerminalScreen::mouseMode() const {
    return m_mouseMode;
}

bool TerminalScreen::alternateScreen() const {
    return m_alternate;
}

int TerminalScreen::cursorMoved(VTermPos position, VTermPos, int visible, void* user) {
    auto& screen = *static_cast<TerminalScreen*>(user);
    screen.m_cursor.row = position.row;
    screen.m_cursor.column = position.col;
    screen.m_cursor.visible = visible != 0;

    return 1;
}

// A title arrives in fragments, and only the last fragment makes it the title of the terminal.
int TerminalScreen::propertyChanged(VTermProp property, VTermValue* value, void* user) {
    auto& screen = *static_cast<TerminalScreen*>(user);

    switch (property) {
    case VTERM_PROP_CURSORVISIBLE:
        screen.m_cursor.visible = value->boolean != 0;
        return 1;
    case VTERM_PROP_CURSORBLINK:
        screen.m_cursor.blinking = value->boolean != 0;
        return 1;
    case VTERM_PROP_CURSORSHAPE:
        screen.m_cursor.shape = value->number == VTERM_PROP_CURSORSHAPE_UNDERLINE ? CursorShape::Underline : value->number == VTERM_PROP_CURSORSHAPE_BAR_LEFT ? CursorShape::Bar : CursorShape::Block;
        return 1;
    case VTERM_PROP_MOUSE:
        screen.m_mouseMode = value->number;
        return 1;
    case VTERM_PROP_ALTSCREEN:
        screen.m_alternate = value->boolean != 0;
        return 1;
    case VTERM_PROP_TITLE:
        if (gather(screen.m_title, value->string, largestTitle)) {
            screen.m_signals.title = screen.m_title.text;
        }

        return 1;
    default:
        return 1;
    }
}

int TerminalScreen::rang(void* user) {
    static_cast<TerminalScreen*>(user)->m_signals.bell = true;

    return 1;
}

// A line leaving the top of the screen keeps only its written cells, and it wraps when the line now at the top continues it.
int TerminalScreen::pushed(int columns, const VTermScreenCell* cells, void* user) {
    auto& screen = *static_cast<TerminalScreen*>(user);

    // A line the history cannot keep is still counted, so the lines on screen keep their numbers.
    if (screen.m_historyLimit == 0) {
        ++screen.m_dropped;
        return 1;
    }

    int written = columns;

    while (written > 0 && cells[written - 1].chars[0] == 0 && VTERM_COLOR_IS_DEFAULT_BG(&cells[written - 1].bg)) {
        --written;
    }

    const VTermLineInfo* next = vterm_state_get_lineinfo(screen.m_state, 0);
    screen.m_history.push_back({std::vector<VTermScreenCell>(cells, cells + written), next != nullptr && next->continuation != 0});

    if (screen.m_history.size() > screen.m_historyLimit) {
        screen.m_history.pop_front();
        ++screen.m_dropped;
    }

    return 1;
}

// A screen that grows taller takes its lines back from the history, padded to its width.
int TerminalScreen::popped(int columns, VTermScreenCell* cells, void* user) {
    auto& screen = *static_cast<TerminalScreen*>(user);

    if (screen.m_history.empty()) {
        return 0;
    }

    const auto& line = screen.m_history.back().cells;

    for (int column = 0; column < columns; ++column) {
        cells[column] = static_cast<std::size_t>(column) < line.size() ? line[static_cast<std::size_t>(column)] : screen.blank();
    }

    screen.m_history.pop_back();

    return 1;
}

int TerminalScreen::cleared(void* user) {
    static_cast<TerminalScreen*>(user)->clearHistory();

    return 1;
}

// The directory a shell reports with the seventh operating system command arrives in fragments as a file address, and the ninth and the 777th carry notifications.
int TerminalScreen::commanded(int command, VTermStringFragment fragment, void* user) {
    if (command != directoryCommand && command != linkCommand && command != messageCommand && command != notifyCommand) {
        return 0;
    }

    auto& screen = *static_cast<TerminalScreen*>(user);

    if (!gather(screen.m_command, fragment, largestCommand)) {
        return 1;
    }

    if (command == directoryCommand) {
        screen.m_signals.directory = directory(screen.m_command.text);
        return 1;
    }

    if (command == linkCommand) {
        screen.startLink(screen.m_command.text);
        return 1;
    }

    screen.notify(command, screen.m_command.text);

    return 1;
}

// The eighth command carries its parameters and an address, and every cell written until an empty address follows carries the number of that address.
// A link past the bound of addresses or of their bytes one terminal remembers is written as plain text.
// The addresses stay where they were stored, so the index of their numbers reads them in place.
void TerminalScreen::startLink(std::string_view command) {
    const std::size_t separator = command.find(';');
    const std::string_view address = separator == std::string_view::npos ? std::string_view() : command.substr(separator + 1);

    if (address.empty()) {
        vterm_state_set_link(m_state, 0);
        return;
    }

    if (const auto known = m_linkNumbers.find(address); known != m_linkNumbers.end()) {
        vterm_state_set_link(m_state, static_cast<int>(known->second));
        return;
    }

    if (m_links.size() >= maximumLinks || m_linkBytes + address.size() > maximumLinkBytes) {
        vterm_state_set_link(m_state, 0);
        return;
    }

    m_links.emplace_back(address);
    m_linkBytes += address.size();
    m_linkNumbers.emplace(m_links.back(), m_links.size());
    vterm_state_set_link(m_state, static_cast<int>(m_links.size()));
}

// The ninth command carries a message alone and the 777th a title and a body after the word notify.
// A ninth command made of numbers and semicolons is a progress report of another terminal, which is no message.
void TerminalScreen::notify(int command, const std::string& text) {
    if (command == messageCommand) {
        const bool report = !text.empty() && text.find_first_not_of("0123456789;") == std::string::npos;

        if (!text.empty() && !report) {
            m_signals.notifications.emplace_back(std::string(), text);
        }

        return;
    }

    const std::size_t kind = text.find(';');
    const std::size_t title = kind == std::string::npos ? std::string::npos : text.find(';', kind + 1);

    if (kind == std::string::npos || text.substr(0, kind) != "notify") {
        return;
    }

    if (title == std::string::npos) {
        m_signals.notifications.emplace_back(text.substr(kind + 1), std::string());
        return;
    }

    m_signals.notifications.emplace_back(text.substr(kind + 1, title - kind - 1), text.substr(title + 1));
}

int TerminalScreen::selected(VTermSelectionMask, VTermStringFragment fragment, void* user) {
    auto& screen = *static_cast<TerminalScreen*>(user);

    if (gather(screen.m_clipboard, fragment, largestClipboard)) {
        screen.m_signals.clipboard = screen.m_clipboard.text;
    }

    return 1;
}

// A string a program sends in fragments is kept up to its bound, and one that passes the bound is dropped whole when it ends, so a string that never ends cannot grow without limit.
bool TerminalScreen::gather(Gathered& gathered, VTermStringFragment fragment, std::size_t bound) {
    if (fragment.initial) {
        gathered = {};
    }

    gathered.overflowed = gathered.overflowed || gathered.text.size() + fragment.len > bound;

    if (!gathered.overflowed) {
        gathered.text.append(fragment.str, fragment.len);
    }

    return fragment.final && !gathered.overflowed;
}

void TerminalScreen::replied(const char* bytes, std::size_t length, void* user) {
    static_cast<TerminalScreen*>(user)->m_reply.append(bytes, length);
}

// A cell holds a base character and its combining marks, the second half of a wide character holds nothing and an empty cell reads as a space.
std::string TerminalScreen::utf8(const VTermScreenCell& cell) {
    if (cell.chars[0] == 0xFFFFFFFFU) {
        return {};
    }

    if (cell.chars[0] == 0) {
        return " ";
    }

    std::string encoded;

    for (std::size_t index = 0; index < VTERM_MAX_CHARS_PER_CELL && cell.chars[index] != 0; ++index) {
        const std::uint32_t code = cell.chars[index];

        if (code < 0x80U) {
            encoded += static_cast<char>(code);
        } else if (code < 0x800U) {
            encoded += static_cast<char>(0xC0U | (code >> 6U));
            encoded += static_cast<char>(0x80U | (code & 0x3FU));
        } else if (code < 0x10000U) {
            encoded += static_cast<char>(0xE0U | (code >> 12U));
            encoded += static_cast<char>(0x80U | ((code >> 6U) & 0x3FU));
            encoded += static_cast<char>(0x80U | (code & 0x3FU));
        } else {
            encoded += static_cast<char>(0xF0U | (code >> 18U));
            encoded += static_cast<char>(0x80U | ((code >> 12U) & 0x3FU));
            encoded += static_cast<char>(0x80U | ((code >> 6U) & 0x3FU));
            encoded += static_cast<char>(0x80U | (code & 0x3FU));
        }
    }

    return encoded;
}

// A file address names a host before its path and escapes bytes as percent pairs, and a drive letter after the first slash is a Windows path.
std::string TerminalScreen::directory(std::string_view address) {
    constexpr std::string_view scheme = "file://";

    if (!address.starts_with(scheme)) {
        return {};
    }

    const std::string_view rest = address.substr(scheme.size());
    const std::size_t slash = rest.find('/');

    if (slash == std::string_view::npos) {
        return {};
    }

    std::string path;
    const std::string_view encoded = rest.substr(slash);

    for (std::size_t index = 0; index < encoded.size(); ++index) {
        unsigned int byte = 0;
        const bool marked = encoded[index] == '%' && index + 2 < encoded.size();
        const auto read = marked ? std::from_chars(encoded.data() + index + 1, encoded.data() + index + 3, byte, 16) : std::from_chars_result{nullptr, std::errc::invalid_argument};
        const bool escaped = read.ec == std::errc() && read.ptr == encoded.data() + index + 3;

        if (escaped) {
            path += static_cast<char>(byte);
            index += 2;
            continue;
        }

        path += encoded[index];
    }

    if (path.size() > 2 && path[2] == ':') {
        path.erase(0, 1);
    }

    return path;
}

VTermScreenCell TerminalScreen::blank() const {
    VTermScreenCell cell{};
    vterm_state_get_default_colors(m_state, &cell.fg, &cell.bg);
    cell.fg.type = static_cast<std::uint8_t>(cell.fg.type | VTERM_COLOR_DEFAULT_FG);
    cell.bg.type = static_cast<std::uint8_t>(cell.bg.type | VTERM_COLOR_DEFAULT_BG);
    cell.width = 1;

    return cell;
}

} // namespace workpane::ui
