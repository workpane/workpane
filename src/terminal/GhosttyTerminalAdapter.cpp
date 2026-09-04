#include "terminal/GhosttyTerminalAdapter.h"

#include "terminal/TerminalDimensions.h"

#include <BuildInfo.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace workpane::terminalcore {

constexpr std::size_t scrollbackMaximumBytes = 64 * 1024 * 1024;
// The markers that open and close a bracketed paste are twelve bytes, and the reserve is generous enough that the encoder never runs out.
constexpr qsizetype pasteMarkerReserve = 64;
constexpr std::size_t scrollbackMaximumLines = 10'000;
// An address a program marked on a cell is bounded, because nothing is opened from a payload of any size.
constexpr std::size_t hyperlinkMaximumBytes = 2048;
// The schemes an address is opened with are the ones the application knows how to reach.
constexpr std::array<QLatin1StringView, 3> addressSchemes{QLatin1StringView("https://"), QLatin1StringView("http://"), QLatin1StringView("file://")};
// A sentence closes with punctuation, and that punctuation is not part of the address it followed.
constexpr QLatin1StringView addressTrailing(".,;:!?)]}'\"");

class GhosttyTerminalAdapterHelper final {
  public:
    static std::uint32_t unshiftedCodepointForKey(int key);
    static bool unsupportedKeyText(std::uint32_t codepoint);
    static QByteArray normalizedKeyText(const QKeyEvent& event);
    static std::uint32_t unshiftedCodepoint(const QKeyEvent& event, const QByteArray& normalizedText);
    static QColor toColor(const GhosttyColorRgb& color);
    static GhosttyColorRgb toGhosttyColor(const QColor& color);
    static Result<void> ghosttyFailure(const QString& operation, GhosttyResult result);
    static CursorStyle toCursorStyle(GhosttyRenderStateCursorVisualStyle style);
    static QString acceptedAddress(const QString& word);
    static bool standsAlone(const QString& text, qsizetype from, qsizetype length);
};

std::uint32_t GhosttyTerminalAdapterHelper::unshiftedCodepointForKey(int key) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return static_cast<std::uint32_t>('a' + (key - Qt::Key_A));
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return static_cast<std::uint32_t>('0' + (key - Qt::Key_0));
    }

    switch (key) {
    case Qt::Key_Space:
        return ' ';
    case Qt::Key_Exclam:
        return '1';
    case Qt::Key_QuoteDbl:
    case Qt::Key_Apostrophe:
        return '\'';
    case Qt::Key_NumberSign:
        return '3';
    case Qt::Key_Dollar:
        return '4';
    case Qt::Key_Percent:
        return '5';
    case Qt::Key_Ampersand:
        return '7';
    case Qt::Key_ParenLeft:
        return '9';
    case Qt::Key_ParenRight:
        return '0';
    case Qt::Key_Asterisk:
        return '8';
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        return '=';
    case Qt::Key_Comma:
    case Qt::Key_Less:
        return ',';
    case Qt::Key_Minus:
    case Qt::Key_Underscore:
        return '-';
    case Qt::Key_Period:
    case Qt::Key_Greater:
        return '.';
    case Qt::Key_Slash:
    case Qt::Key_Question:
        return '/';
    case Qt::Key_Colon:
    case Qt::Key_Semicolon:
        return ';';
    case Qt::Key_At:
        return '2';
    case Qt::Key_BracketLeft:
    case Qt::Key_BraceLeft:
        return '[';
    case Qt::Key_Backslash:
    case Qt::Key_Bar:
        return '\\';
    case Qt::Key_BracketRight:
    case Qt::Key_BraceRight:
        return ']';
    case Qt::Key_AsciiCircum:
        return '6';
    case Qt::Key_QuoteLeft:
    case Qt::Key_AsciiTilde:
        return '`';
    default:
        return 0;
    }
}

bool GhosttyTerminalAdapterHelper::unsupportedKeyText(std::uint32_t codepoint) {
    return codepoint < 0x20 || codepoint == 0x7F || (codepoint >= 0xF700 && codepoint <= 0xF8FF);
}

QByteArray GhosttyTerminalAdapterHelper::normalizedKeyText(const QKeyEvent& event) {
    const auto codepoints = event.text().toUcs4();

    if (std::ranges::any_of(codepoints, unsupportedKeyText)) {
        return {};
    }

    return event.text().toUtf8();
}

std::uint32_t GhosttyTerminalAdapterHelper::unshiftedCodepoint(const QKeyEvent& event, const QByteArray& normalizedText) {
    const std::uint32_t mapped = unshiftedCodepointForKey(event.key());

    if (mapped != 0) {
        return mapped;
    }

    if (normalizedText.isEmpty()) {
        return 0;
    }

    const auto codepoints = QString::fromUtf8(normalizedText).toUcs4();
    return codepoints.size() == 1 ? codepoints.first() : 0;
}

QColor GhosttyTerminalAdapterHelper::toColor(const GhosttyColorRgb& color) {
    return {color.r, color.g, color.b};
}

GhosttyColorRgb GhosttyTerminalAdapterHelper::toGhosttyColor(const QColor& color) {
    return {static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()), static_cast<std::uint8_t>(color.blue())};
}

// A whole word is a run nothing alphanumeric touches, which is what the option asks for.
bool GhosttyTerminalAdapterHelper::standsAlone(const QString& text, qsizetype from, qsizetype length) {
    const bool openStart = from == 0 || !text.at(from - 1).isLetterOrNumber();
    const bool openEnd = from + length >= text.size() || !text.at(from + length).isLetterOrNumber();
    return openStart && openEnd;
}

QString GhosttyTerminalAdapterHelper::acceptedAddress(const QString& word) {
    QString address = word.trimmed();

    while (!address.isEmpty() && addressTrailing.contains(address.back().toLatin1())) {
        address.chop(1);
    }

    // clang-format off
    const bool accepted = std::any_of(addressSchemes.begin(), addressSchemes.end(), [&address](QLatin1StringView scheme) { return address.startsWith(scheme); });
    // clang-format on
    return accepted ? address : QString();
}

CursorStyle GhosttyTerminalAdapterHelper::toCursorStyle(GhosttyRenderStateCursorVisualStyle style) {
    switch (style) {
    case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BAR:
        return CursorStyle::Bar;
    case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_UNDERLINE:
        return CursorStyle::Underline;
    case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK_HOLLOW:
        return CursorStyle::HollowBlock;
    default:
        return CursorStyle::Block;
    }
}

Result<void> GhosttyTerminalAdapterHelper::ghosttyFailure(const QString& operation, GhosttyResult result) {
    return Result<void>::failure({"ghostty_operation_failed", operation, QString::number(static_cast<int>(result))});
}

GhosttyTerminalAdapter::GhosttyTerminalAdapter(QObject* parent) : QObject(parent) {}

GhosttyTerminalAdapter::~GhosttyTerminalAdapter() {
    release();
}

Result<void> GhosttyTerminalAdapter::initialize(int columns, int rows, int cellWidth, int cellHeight, const domain::TerminalTheme& theme) {
    if (!TerminalDimensions::validTerminalGrid(columns, rows) || !TerminalDimensions::validTerminalCellSize(cellWidth, cellHeight)) {
        return Result<void>::failure({"ghostty_size_invalid", "The terminal dimensions are invalid", QStringLiteral("%1x%2 at %3x%4").arg(columns).arg(rows).arg(cellWidth).arg(cellHeight)});
    }

    release();

    GhosttyResult result = ghostty_terminal_new(nullptr, &m_terminal, static_cast<std::uint16_t>(columns), static_cast<std::uint16_t>(rows));

    if (result != GHOSTTY_SUCCESS) {
        return GhosttyTerminalAdapterHelper::ghosttyFailure(QStringLiteral("Terminal initialization failed"), result);
    }

    result = ghostty_render_state_new(nullptr, &m_renderState);

    if (result != GHOSTTY_SUCCESS) {
        release();
        return GhosttyTerminalAdapterHelper::ghosttyFailure(QStringLiteral("Render state initialization failed"), result);
    }

    result = ghostty_render_state_row_iterator_new(nullptr, &m_rowIterator);

    if (result != GHOSTTY_SUCCESS) {
        release();
        return GhosttyTerminalAdapterHelper::ghosttyFailure(QStringLiteral("Row iterator initialization failed"), result);
    }

    result = ghostty_render_state_row_cells_new(nullptr, &m_rowCells);

    if (result != GHOSTTY_SUCCESS) {
        release();
        return GhosttyTerminalAdapterHelper::ghosttyFailure(QStringLiteral("Cell iterator initialization failed"), result);
    }

    result = ghostty_key_encoder_new(nullptr, &m_keyEncoder);

    if (result != GHOSTTY_SUCCESS) {
        release();
        return GhosttyTerminalAdapterHelper::ghosttyFailure(QStringLiteral("Key encoder initialization failed"), result);
    }

    result = ghostty_key_event_new(nullptr, &m_keyEvent);

    if (result != GHOSTTY_SUCCESS) {
        release();
        return GhosttyTerminalAdapterHelper::ghosttyFailure(QStringLiteral("Key event initialization failed"), result);
    }

    result = ghostty_mouse_encoder_new(nullptr, &m_mouseEncoder);

    if (result != GHOSTTY_SUCCESS) {
        release();
        return GhosttyTerminalAdapterHelper::ghosttyFailure(QStringLiteral("Mouse encoder initialization failed"), result);
    }

    result = ghostty_mouse_event_new(nullptr, &m_mouseEvent);

    if (result != GHOSTTY_SUCCESS) {
        release();
        return GhosttyTerminalAdapterHelper::ghosttyFailure(QStringLiteral("Mouse event initialization failed"), result);
    }

    result = ghostty_selection_gesture_new(nullptr, &m_gesture);

    if (result != GHOSTTY_SUCCESS) {
        release();
        return GhosttyTerminalAdapterHelper::ghosttyFailure(QStringLiteral("Selection gesture initialization failed"), result);
    }

    const auto gestureEvents = std::array<std::pair<GhosttySelectionGestureEvent*, GhosttySelectionGestureEventType>, 4>{{{&m_pressEvent, GHOSTTY_SELECTION_GESTURE_EVENT_TYPE_PRESS}, {&m_dragEvent, GHOSTTY_SELECTION_GESTURE_EVENT_TYPE_DRAG}, {&m_releaseEvent, GHOSTTY_SELECTION_GESTURE_EVENT_TYPE_RELEASE}, {&m_autoscrollEvent, GHOSTTY_SELECTION_GESTURE_EVENT_TYPE_AUTOSCROLL_TICK}}};

    for (const auto& gestureEvent : gestureEvents) {
        result = ghostty_selection_gesture_event_new(nullptr, gestureEvent.first, gestureEvent.second);
        if (result != GHOSTTY_SUCCESS) {
            release();
            return GhosttyTerminalAdapterHelper::ghosttyFailure(QStringLiteral("Selection gesture event initialization failed"), result);
        }
    }

    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_USERDATA, this);
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_WRITE_PTY, reinterpret_cast<const void*>(writePtyCallback));
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_TITLE_CHANGED, reinterpret_cast<const void*>(titleChangedCallback));
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_PWD_CHANGED, reinterpret_cast<const void*>(pwdChangedCallback));
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_DEVICE_ATTRIBUTES, reinterpret_cast<const void*>(deviceAttributesCallback));
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_SIZE, reinterpret_cast<const void*>(sizeCallback));
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_COLOR_SCHEME, reinterpret_cast<const void*>(colorSchemeCallback));
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_XTVERSION, reinterpret_cast<const void*>(xtversionCallback));
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_BELL, reinterpret_cast<const void*>(bellCallback));
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_CLIPBOARD_WRITE, reinterpret_cast<const void*>(clipboardWriteCallback));
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_DESKTOP_NOTIFICATION, reinterpret_cast<const void*>(desktopNotificationCallback));

    result = ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_SCROLLBACK_MAX_BYTES, &scrollbackMaximumBytes);

    if (result != GHOSTTY_SUCCESS) {
        release();
        return GhosttyTerminalAdapterHelper::ghosttyFailure(QStringLiteral("Scrollback byte limit configuration failed"), result);
    }

    result = ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_SCROLLBACK_MAX_LINES, &scrollbackMaximumLines);

    if (result != GHOSTTY_SUCCESS) {
        release();
        return GhosttyTerminalAdapterHelper::ghosttyFailure(QStringLiteral("Scrollback line limit configuration failed"), result);
    }

    result = ghostty_terminal_resize(m_terminal, static_cast<std::uint16_t>(columns), static_cast<std::uint16_t>(rows), static_cast<std::uint32_t>(cellWidth), static_cast<std::uint32_t>(cellHeight));

    if (result != GHOSTTY_SUCCESS) {
        release();
        return GhosttyTerminalAdapterHelper::ghosttyFailure(QStringLiteral("Terminal cell geometry configuration failed"), result);
    }

    const auto themeResult = applyTheme(theme);

    if (!themeResult.hasValue()) {
        release();
        return themeResult;
    }

    ghostty_render_state_update(m_renderState, m_terminal);
    return Result<void>::success();
}

Result<void> GhosttyTerminalAdapter::setTheme(const domain::TerminalTheme& theme) {
    if (m_terminal == nullptr) {
        return Result<void>::failure({"ghostty_not_initialized", "The terminal emulator is not initialized", {}});
    }

    const auto result = applyTheme(theme);

    if (result.hasValue()) {
        ghostty_render_state_update(m_renderState, m_terminal);
    }

    return result;
}

void GhosttyTerminalAdapter::write(const QByteArray& bytes) {
    if (m_terminal == nullptr || bytes.isEmpty()) {
        return;
    }

    ghostty_terminal_vt_write(m_terminal, reinterpret_cast<const std::uint8_t*>(bytes.constData()), static_cast<std::size_t>(bytes.size()));
    ghostty_render_state_update(m_renderState, m_terminal);
}

Result<void> GhosttyTerminalAdapter::resize(int columns, int rows, int cellWidth, int cellHeight) {
    if (m_terminal == nullptr) {
        return Result<void>::failure({"ghostty_not_initialized", "The terminal emulator is not initialized", {}});
    }
    if (!TerminalDimensions::validTerminalGrid(columns, rows) || !TerminalDimensions::validTerminalCellSize(cellWidth, cellHeight)) {
        return Result<void>::failure({"ghostty_size_invalid", "The terminal dimensions are invalid", QStringLiteral("%1x%2 at %3x%4").arg(columns).arg(rows).arg(cellWidth).arg(cellHeight)});
    }

    const auto result = ghostty_terminal_resize(m_terminal, static_cast<std::uint16_t>(columns), static_cast<std::uint16_t>(rows), static_cast<std::uint32_t>(cellWidth), static_cast<std::uint32_t>(cellHeight));

    if (result != GHOSTTY_SUCCESS) {
        return GhosttyTerminalAdapterHelper::ghosttyFailure(QStringLiteral("Terminal resize failed"), result);
    }

    ghostty_render_state_update(m_renderState, m_terminal);
    return Result<void>::success();
}

Result<QByteArray> GhosttyTerminalAdapter::encodeKey(const QKeyEvent& event) {
    if (m_terminal == nullptr || m_keyEncoder == nullptr || m_keyEvent == nullptr) {
        return Result<QByteArray>::failure({"ghostty_not_initialized", "The terminal emulator is not initialized", {}});
    }

    const QByteArray text = GhosttyTerminalAdapterHelper::normalizedKeyText(event);
    ghostty_key_encoder_setopt_from_terminal(m_keyEncoder, m_terminal);
    ghostty_key_event_set_action(m_keyEvent, event.isAutoRepeat() ? GHOSTTY_KEY_ACTION_REPEAT : GHOSTTY_KEY_ACTION_PRESS);
    ghostty_key_event_set_key(m_keyEvent, mapKey(event));
    ghostty_key_event_set_mods(m_keyEvent, mapModifiers(event.modifiers()));
    ghostty_key_event_set_consumed_mods(m_keyEvent, 0);
    ghostty_key_event_set_composing(m_keyEvent, false);
    ghostty_key_event_set_utf8(m_keyEvent, text.isEmpty() ? nullptr : text.constData(), static_cast<std::size_t>(text.size()));

    ghostty_key_event_set_unshifted_codepoint(m_keyEvent, GhosttyTerminalAdapterHelper::unshiftedCodepoint(event, text));

    std::array<char, 128> stackBuffer{};
    std::size_t outputSize = 0;
    GhosttyResult result = ghostty_key_encoder_encode(m_keyEncoder, m_keyEvent, stackBuffer.data(), stackBuffer.size(), &outputSize);

    if (result == GHOSTTY_SUCCESS) {
        return Result<QByteArray>::success(QByteArray(stackBuffer.data(), static_cast<qsizetype>(outputSize)));
    }
    if (result != GHOSTTY_OUT_OF_SPACE) {
        return Result<QByteArray>::failure({"ghostty_key_encoding_failed", "The key event could not be encoded", QString::number(static_cast<int>(result))});
    }

    QByteArray output(static_cast<qsizetype>(outputSize), Qt::Uninitialized);
    result = ghostty_key_encoder_encode(m_keyEncoder, m_keyEvent, output.data(), static_cast<std::size_t>(output.size()), &outputSize);

    if (result != GHOSTTY_SUCCESS) {
        return Result<QByteArray>::failure({"ghostty_key_encoding_failed", "The key event could not be encoded", QString::number(static_cast<int>(result))});
    }

    output.resize(static_cast<qsizetype>(outputSize));
    return Result<QByteArray>::success(std::move(output));
}

void GhosttyTerminalAdapter::scrollViewport(qint64 rows) {
    if (m_terminal == nullptr || rows == 0) {
        return;
    }

    GhosttyTerminalScrollViewport behavior{};
    behavior.tag = GHOSTTY_SCROLL_VIEWPORT_DELTA;
    behavior.value.delta = static_cast<intptr_t>(rows);
    ghostty_terminal_scroll_viewport(m_terminal, behavior);
    ghostty_render_state_update(m_renderState, m_terminal);
}

void GhosttyTerminalAdapter::scrollToRow(quint64 row) {
    if (m_terminal == nullptr) {
        return;
    }

    GhosttyTerminalScrollViewport behavior{};
    behavior.tag = GHOSTTY_SCROLL_VIEWPORT_ROW;
    behavior.value.row = static_cast<std::size_t>(row);
    ghostty_terminal_scroll_viewport(m_terminal, behavior);
    ghostty_render_state_update(m_renderState, m_terminal);
}

void GhosttyTerminalAdapter::scrollToTop() {
    if (m_terminal == nullptr) {
        return;
    }

    GhosttyTerminalScrollViewport behavior{};
    behavior.tag = GHOSTTY_SCROLL_VIEWPORT_TOP;
    ghostty_terminal_scroll_viewport(m_terminal, behavior);
    ghostty_render_state_update(m_renderState, m_terminal);
}

void GhosttyTerminalAdapter::scrollToBottom() {
    if (m_terminal == nullptr) {
        return;
    }

    GhosttyTerminalScrollViewport behavior{};
    behavior.tag = GHOSTTY_SCROLL_VIEWPORT_BOTTOM;
    ghostty_terminal_scroll_viewport(m_terminal, behavior);
    ghostty_render_state_update(m_renderState, m_terminal);
}

TerminalRenderSnapshot GhosttyTerminalAdapter::snapshot() {
    TerminalRenderSnapshot output;

    if (m_renderState == nullptr) {
        return output;
    }

    std::uint16_t columns = 1;
    std::uint16_t rows = 1;
    ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_COLS, &columns);
    ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_ROWS, &rows);
    output.columns = columns;
    output.rows = rows;

    GhosttyTerminalScrollbar scrollbar{};

    if (m_terminal != nullptr && ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_SCROLLBAR, &scrollbar) == GHOSTTY_SUCCESS) {
        output.scrollTotal = scrollbar.total;
        output.scrollOffset = scrollbar.offset;
        output.scrollViewport = scrollbar.len;
    }

    GhosttyRenderStateColors colors{};
    colors.size = sizeof(GhosttyRenderStateColors);

    if (ghostty_render_state_colors_get(m_renderState, &colors) == GHOSTTY_SUCCESS) {
        output.foreground = GhosttyTerminalAdapterHelper::toColor(colors.foreground);
        output.background = GhosttyTerminalAdapterHelper::toColor(colors.background);
        output.cursor = colors.cursor_has_value ? GhosttyTerminalAdapterHelper::toColor(colors.cursor) : output.foreground;
    }

    bool cursorInViewport = false;
    ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VISIBLE, &output.cursorVisible);
    ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_BLINKING, &output.cursorBlinking);
    ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_HAS_VALUE, &cursorInViewport);

    if (cursorInViewport) {
        std::uint16_t cursorColumn = 0;
        std::uint16_t cursorRow = 0;
        GhosttyRenderStateCursorVisualStyle cursorStyle = GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK;
        ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X, &cursorColumn);
        ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y, &cursorRow);
        ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_WIDE_TAIL, &output.cursorWide);
        ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VISUAL_STYLE, &cursorStyle);
        output.cursorPosition = {cursorColumn, cursorRow};
        output.cursorStyle = GhosttyTerminalAdapterHelper::toCursorStyle(cursorStyle);
    } else {
        output.cursorVisible = false;
    }

    if (ghostty_render_state_get(m_renderState, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &m_rowIterator) != GHOSTTY_SUCCESS) {
        return output;
    }

    int row = 0;
    output.cells.reserve(output.columns * output.rows);

    while (ghostty_render_state_row_iterator_next(m_rowIterator)) {
        if (ghostty_render_state_row_get(m_rowIterator, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &m_rowCells) != GHOSTTY_SUCCESS) {
            ++row;
            continue;
        }

        GhosttyRenderStateRowSelection rowSelection{};
        rowSelection.size = sizeof(GhosttyRenderStateRowSelection);
        const bool rowHasSelection = ghostty_render_state_row_get(m_rowIterator, GHOSTTY_RENDER_STATE_ROW_DATA_SELECTION, &rowSelection) == GHOSTTY_SUCCESS;

        int column = 0;
        while (ghostty_render_state_row_cells_next(m_rowCells)) {
            RenderCell cell;
            cell.column = column;
            cell.row = row;
            cell.selected = rowHasSelection && column >= rowSelection.start_x && column <= rowSelection.end_x;
            cell.foreground = output.foreground;
            cell.background = output.background;

            GhosttyBuffer textBuffer{};
            std::array<std::uint8_t, 64> textStorage{};
            textBuffer.ptr = textStorage.data();
            textBuffer.cap = textStorage.size();
            ghostty_render_state_row_cells_get(m_rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_UTF8, &textBuffer);
            if (textBuffer.len > 0 && textBuffer.len <= textBuffer.cap) {
                cell.text = QString::fromUtf8(reinterpret_cast<const char*>(textBuffer.ptr), static_cast<qsizetype>(textBuffer.len));
            }

            GhosttyColorRgb foreground{};
            if (ghostty_render_state_row_cells_get(m_rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR, &foreground) == GHOSTTY_SUCCESS) {
                cell.foreground = GhosttyTerminalAdapterHelper::toColor(foreground);
            }
            GhosttyColorRgb background{};
            if (ghostty_render_state_row_cells_get(m_rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR, &background) == GHOSTTY_SUCCESS) {
                cell.background = GhosttyTerminalAdapterHelper::toColor(background);
                cell.hasBackground = true;
            }

            bool hasStyle = false;
            ghostty_render_state_row_cells_get(m_rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_HAS_STYLING, &hasStyle);
            if (hasStyle) {
                GhosttyStyle style{};
                style.size = sizeof(GhosttyStyle);
                if (ghostty_render_state_row_cells_get(m_rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &style) == GHOSTTY_SUCCESS) {
                    cell.bold = style.bold;
                    cell.italic = style.italic;
                    cell.faint = style.faint;
                    cell.inverse = style.inverse;
                    cell.strikethrough = style.strikethrough;
                    cell.underline = style.underline;
                }
            }

            if (!cell.text.isEmpty() || cell.hasBackground || cell.selected) {
                output.cells.append(std::move(cell));
            }
            ++column;
        }
        ++row;
    }

    return output;
}

QString GhosttyTerminalAdapter::title() const {
    if (m_terminal == nullptr) {
        return {};
    }

    GhosttyString value{};

    if (ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_TITLE, &value) != GHOSTTY_SUCCESS) {
        return {};
    }

    return QString::fromUtf8(reinterpret_cast<const char*>(value.ptr), static_cast<qsizetype>(value.len));
}

QString GhosttyTerminalAdapter::workingDirectory() const {
    if (m_terminal == nullptr) {
        return {};
    }

    GhosttyString value{};

    if (ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_PWD, &value) != GHOSTTY_SUCCESS) {
        return {};
    }

    return QString::fromUtf8(reinterpret_cast<const char*>(value.ptr), static_cast<qsizetype>(value.len));
}

// A program asking what this terminal is receives the answer the emulator conforms to, which is a colour VT220.
bool GhosttyTerminalAdapter::deviceAttributesCallback(GhosttyTerminal, void*, GhosttyDeviceAttributes* attributes) {
    if (attributes == nullptr) {
        return false;
    }

    attributes->primary.conformance_level = GHOSTTY_DA_CONFORMANCE_VT220;
    attributes->primary.features[0] = GHOSTTY_DA_FEATURE_ANSI_COLOR;
    attributes->primary.num_features = 1;
    attributes->secondary.device_type = GHOSTTY_DA_DEVICE_TYPE_VT220;
    attributes->secondary.firmware_version = 0;
    attributes->secondary.rom_cartridge = 0;
    attributes->tertiary.unit_id = 0;
    return true;
}

// A program asking how large the window is receives the geometry the widget declared rather than silence.
bool GhosttyTerminalAdapter::sizeCallback(GhosttyTerminal terminal, void*, GhosttySizeReportSize* size) {
    std::uint16_t columns = 0;
    std::uint16_t rows = 0;
    std::uint32_t widthPixels = 0;
    std::uint32_t heightPixels = 0;

    if (size == nullptr || ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_COLS, &columns) != GHOSTTY_SUCCESS || ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_ROWS, &rows) != GHOSTTY_SUCCESS) {
        return false;
    }
    if (columns == 0 || rows == 0 || ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_WIDTH_PX, &widthPixels) != GHOSTTY_SUCCESS || ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_HEIGHT_PX, &heightPixels) != GHOSTTY_SUCCESS) {
        return false;
    }

    size->rows = rows;
    size->columns = columns;
    size->cell_width = widthPixels / columns;
    size->cell_height = heightPixels / rows;
    return true;
}

// A program that adapts its colours to the terminal is told which side the background is on.
bool GhosttyTerminalAdapter::colorSchemeCallback(GhosttyTerminal terminal, void*, GhosttyColorScheme* scheme) {
    GhosttyColorRgb background{};

    if (scheme == nullptr || ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_COLOR_BACKGROUND, &background) != GHOSTTY_SUCCESS) {
        return false;
    }

    *scheme = GhosttyTerminalAdapterHelper::toColor(background).lightnessF() < 0.5F ? GHOSTTY_COLOR_SCHEME_DARK : GHOSTTY_COLOR_SCHEME_LIGHT;
    return true;
}

// A program asking which terminal it is talking to receives the name of this product rather than the name of the library it embeds.
GhosttyString GhosttyTerminalAdapter::xtversionCallback(GhosttyTerminal, void*) {
    static constexpr QLatin1StringView version(WORKPANE_PRODUCT_NAME " " WORKPANE_APP_VERSION);
    return {reinterpret_cast<const std::uint8_t*>(version.data()), static_cast<std::size_t>(version.size())};
}

void GhosttyTerminalAdapter::writePtyCallback(GhosttyTerminal, void* userData, const std::uint8_t* data, std::size_t size) {
    auto* adapter = static_cast<GhosttyTerminalAdapter*>(userData);
    emit adapter->responseReady(QByteArray(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size)));
}

void GhosttyTerminalAdapter::titleChangedCallback(GhosttyTerminal, void* userData) {
    emit static_cast<GhosttyTerminalAdapter*>(userData)->titleChanged();
}

void GhosttyTerminalAdapter::pwdChangedCallback(GhosttyTerminal, void* userData) {
    emit static_cast<GhosttyTerminalAdapter*>(userData)->workingDirectoryChanged();
}

void GhosttyTerminalAdapter::bellCallback(GhosttyTerminal, void* userData) {
    emit static_cast<GhosttyTerminalAdapter*>(userData)->bellRang();
}

// The clipboard belongs to the user, so a program only reaches it while the user allows it and only with text.
GhosttyClipboardWriteResult GhosttyTerminalAdapter::clipboardWriteCallback(GhosttyTerminal, void* userData, const GhosttyClipboardWrite* write) {
    auto* adapter = static_cast<GhosttyTerminalAdapter*>(userData);

    if (!adapter->m_clipboardWriteAllowed) {
        return GHOSTTY_CLIPBOARD_WRITE_RESULT_DENIED;
    }
    if (write == nullptr || write->location != GHOSTTY_CLIPBOARD_LOCATION_STANDARD) {
        return GHOSTTY_CLIPBOARD_WRITE_RESULT_UNSUPPORTED;
    }

    if (write->contents_len == 0) {
        emit adapter->clipboardWriteRequested(QString());
        return GHOSTTY_CLIPBOARD_WRITE_RESULT_SUCCESS;
    }

    for (std::size_t entry = 0; entry < write->contents_len; ++entry) {
        const GhosttyClipboardContent& content = write->contents[entry];
        const QLatin1StringView mime(reinterpret_cast<const char*>(content.mime.ptr), static_cast<qsizetype>(content.mime.len));
        if (mime != QLatin1StringView("text/plain")) {
            continue;
        }

        emit adapter->clipboardWriteRequested(QString::fromUtf8(reinterpret_cast<const char*>(content.data.ptr), static_cast<qsizetype>(content.data.len)));
        return GHOSTTY_CLIPBOARD_WRITE_RESULT_SUCCESS;
    }

    return GHOSTTY_CLIPBOARD_WRITE_RESULT_UNSUPPORTED;
}

// A program that finished something long says so, and the application shows it where every other message is shown.
void GhosttyTerminalAdapter::desktopNotificationCallback(GhosttyTerminal, void* userData, const GhosttyTerminalDesktopNotification* notification) {
    if (notification == nullptr) {
        return;
    }

    const QString title = QString::fromUtf8(reinterpret_cast<const char*>(notification->title.ptr), static_cast<qsizetype>(notification->title.len));
    const QString body = QString::fromUtf8(reinterpret_cast<const char*>(notification->body.ptr), static_cast<qsizetype>(notification->body.len));
    emit static_cast<GhosttyTerminalAdapter*>(userData)->notificationPosted(title, body);
}

void GhosttyTerminalAdapter::setClipboardWriteAllowed(bool allowed) {
    m_clipboardWriteAllowed = allowed;
}

GhosttyKey GhosttyTerminalAdapter::mapKey(const QKeyEvent& event) {
    const int key = event.key();

    if (event.modifiers().testFlag(Qt::KeypadModifier)) {
        if (key >= Qt::Key_0 && key <= Qt::Key_9) {
            return static_cast<GhosttyKey>(GHOSTTY_KEY_NUMPAD_0 + (key - Qt::Key_0));
        }

        switch (key) {
        case Qt::Key_Plus:
            return GHOSTTY_KEY_NUMPAD_ADD;
        case Qt::Key_Backspace:
            return GHOSTTY_KEY_NUMPAD_BACKSPACE;
        case Qt::Key_Clear:
            return GHOSTTY_KEY_NUMPAD_CLEAR;
        case Qt::Key_Comma:
            return GHOSTTY_KEY_NUMPAD_COMMA;
        case Qt::Key_Delete:
            return GHOSTTY_KEY_NUMPAD_DELETE;
        case Qt::Key_Period:
            return GHOSTTY_KEY_NUMPAD_DECIMAL;
        case Qt::Key_Slash:
            return GHOSTTY_KEY_NUMPAD_DIVIDE;
        case Qt::Key_Down:
            return GHOSTTY_KEY_NUMPAD_DOWN;
        case Qt::Key_End:
            return GHOSTTY_KEY_NUMPAD_END;
        case Qt::Key_Enter:
        case Qt::Key_Return:
            return GHOSTTY_KEY_NUMPAD_ENTER;
        case Qt::Key_Equal:
            return GHOSTTY_KEY_NUMPAD_EQUAL;
        case Qt::Key_Home:
            return GHOSTTY_KEY_NUMPAD_HOME;
        case Qt::Key_Insert:
            return GHOSTTY_KEY_NUMPAD_INSERT;
        case Qt::Key_Left:
            return GHOSTTY_KEY_NUMPAD_LEFT;
        case Qt::Key_Asterisk:
            return GHOSTTY_KEY_NUMPAD_MULTIPLY;
        case Qt::Key_PageDown:
            return GHOSTTY_KEY_NUMPAD_PAGE_DOWN;
        case Qt::Key_PageUp:
            return GHOSTTY_KEY_NUMPAD_PAGE_UP;
        case Qt::Key_Right:
            return GHOSTTY_KEY_NUMPAD_RIGHT;
        case Qt::Key_Minus:
            return GHOSTTY_KEY_NUMPAD_SUBTRACT;
        case Qt::Key_Up:
            return GHOSTTY_KEY_NUMPAD_UP;
        default:
            break;
        }
    }

    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return static_cast<GhosttyKey>(GHOSTTY_KEY_A + (key - Qt::Key_A));
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return static_cast<GhosttyKey>(GHOSTTY_KEY_DIGIT_0 + (key - Qt::Key_0));
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F25) {
        return static_cast<GhosttyKey>(GHOSTTY_KEY_F1 + (key - Qt::Key_F1));
    }

    switch (key) {
    case Qt::Key_QuoteLeft:
    case Qt::Key_AsciiTilde:
        return GHOSTTY_KEY_BACKQUOTE;
    case Qt::Key_Backslash:
    case Qt::Key_Bar:
        return GHOSTTY_KEY_BACKSLASH;
    case Qt::Key_BracketLeft:
    case Qt::Key_BraceLeft:
        return GHOSTTY_KEY_BRACKET_LEFT;
    case Qt::Key_BracketRight:
    case Qt::Key_BraceRight:
        return GHOSTTY_KEY_BRACKET_RIGHT;
    case Qt::Key_Comma:
    case Qt::Key_Less:
        return GHOSTTY_KEY_COMMA;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        return GHOSTTY_KEY_EQUAL;
    case Qt::Key_Minus:
    case Qt::Key_Underscore:
        return GHOSTTY_KEY_MINUS;
    case Qt::Key_Period:
    case Qt::Key_Greater:
        return GHOSTTY_KEY_PERIOD;
    case Qt::Key_Apostrophe:
    case Qt::Key_QuoteDbl:
        return GHOSTTY_KEY_QUOTE;
    case Qt::Key_Colon:
    case Qt::Key_Semicolon:
        return GHOSTTY_KEY_SEMICOLON;
    case Qt::Key_Question:
    case Qt::Key_Slash:
        return GHOSTTY_KEY_SLASH;
    case Qt::Key_Exclam:
        return GHOSTTY_KEY_DIGIT_1;
    case Qt::Key_At:
        return GHOSTTY_KEY_DIGIT_2;
    case Qt::Key_NumberSign:
        return GHOSTTY_KEY_DIGIT_3;
    case Qt::Key_Dollar:
        return GHOSTTY_KEY_DIGIT_4;
    case Qt::Key_Percent:
        return GHOSTTY_KEY_DIGIT_5;
    case Qt::Key_AsciiCircum:
        return GHOSTTY_KEY_DIGIT_6;
    case Qt::Key_Ampersand:
        return GHOSTTY_KEY_DIGIT_7;
    case Qt::Key_Asterisk:
        return GHOSTTY_KEY_DIGIT_8;
    case Qt::Key_ParenLeft:
        return GHOSTTY_KEY_DIGIT_9;
    case Qt::Key_ParenRight:
        return GHOSTTY_KEY_DIGIT_0;
    case Qt::Key_Alt:
        return GHOSTTY_KEY_ALT_LEFT;
    case Qt::Key_AltGr:
        return GHOSTTY_KEY_ALT_RIGHT;
    case Qt::Key_Backspace:
        return GHOSTTY_KEY_BACKSPACE;
    case Qt::Key_CapsLock:
        return GHOSTTY_KEY_CAPS_LOCK;
    case Qt::Key_Control:
        return GHOSTTY_KEY_CONTROL_LEFT;
    case Qt::Key_Delete:
        return GHOSTTY_KEY_DELETE;
    case Qt::Key_Down:
        return GHOSTTY_KEY_ARROW_DOWN;
    case Qt::Key_End:
        return GHOSTTY_KEY_END;
    case Qt::Key_Enter:
    case Qt::Key_Return:
        return GHOSTTY_KEY_ENTER;
    case Qt::Key_Escape:
        return GHOSTTY_KEY_ESCAPE;
    case Qt::Key_Help:
        return GHOSTTY_KEY_HELP;
    case Qt::Key_Home:
        return GHOSTTY_KEY_HOME;
    case Qt::Key_Insert:
        return GHOSTTY_KEY_INSERT;
    case Qt::Key_Left:
        return GHOSTTY_KEY_ARROW_LEFT;
    case Qt::Key_PageDown:
        return GHOSTTY_KEY_PAGE_DOWN;
    case Qt::Key_PageUp:
        return GHOSTTY_KEY_PAGE_UP;
    case Qt::Key_Pause:
        return GHOSTTY_KEY_PAUSE;
    case Qt::Key_Print:
        return GHOSTTY_KEY_PRINT_SCREEN;
    case Qt::Key_Right:
        return GHOSTTY_KEY_ARROW_RIGHT;
    case Qt::Key_Space:
        return GHOSTTY_KEY_SPACE;
    case Qt::Key_Meta:
    case Qt::Key_Super_L:
        return GHOSTTY_KEY_META_LEFT;
    case Qt::Key_Super_R:
        return GHOSTTY_KEY_META_RIGHT;
    case Qt::Key_Shift:
        return GHOSTTY_KEY_SHIFT_LEFT;
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        return GHOSTTY_KEY_TAB;
    case Qt::Key_Up:
        return GHOSTTY_KEY_ARROW_UP;
    case Qt::Key_NumLock:
        return GHOSTTY_KEY_NUM_LOCK;
    case Qt::Key_ScrollLock:
        return GHOSTTY_KEY_SCROLL_LOCK;
    case Qt::Key_Menu:
        return GHOSTTY_KEY_CONTEXT_MENU;
    case Qt::Key_Back:
        return GHOSTTY_KEY_BROWSER_BACK;
    case Qt::Key_Favorites:
        return GHOSTTY_KEY_BROWSER_FAVORITES;
    case Qt::Key_Forward:
        return GHOSTTY_KEY_BROWSER_FORWARD;
    case Qt::Key_HomePage:
        return GHOSTTY_KEY_BROWSER_HOME;
    case Qt::Key_Refresh:
        return GHOSTTY_KEY_BROWSER_REFRESH;
    case Qt::Key_Search:
        return GHOSTTY_KEY_BROWSER_SEARCH;
    case Qt::Key_Stop:
        return GHOSTTY_KEY_BROWSER_STOP;
    case Qt::Key_Eject:
        return GHOSTTY_KEY_EJECT;
    case Qt::Key_Launch0:
        return GHOSTTY_KEY_LAUNCH_APP_1;
    case Qt::Key_Launch1:
        return GHOSTTY_KEY_LAUNCH_APP_2;
    case Qt::Key_LaunchMail:
        return GHOSTTY_KEY_LAUNCH_MAIL;
    case Qt::Key_MediaPlay:
    case Qt::Key_MediaPause:
    case Qt::Key_MediaTogglePlayPause:
        return GHOSTTY_KEY_MEDIA_PLAY_PAUSE;
    case Qt::Key_MediaStop:
        return GHOSTTY_KEY_MEDIA_STOP;
    case Qt::Key_MediaNext:
        return GHOSTTY_KEY_MEDIA_TRACK_NEXT;
    case Qt::Key_MediaPrevious:
        return GHOSTTY_KEY_MEDIA_TRACK_PREVIOUS;
    case Qt::Key_PowerOff:
        return GHOSTTY_KEY_POWER;
    case Qt::Key_Sleep:
        return GHOSTTY_KEY_SLEEP;
    case Qt::Key_VolumeDown:
        return GHOSTTY_KEY_AUDIO_VOLUME_DOWN;
    case Qt::Key_VolumeMute:
        return GHOSTTY_KEY_AUDIO_VOLUME_MUTE;
    case Qt::Key_VolumeUp:
        return GHOSTTY_KEY_AUDIO_VOLUME_UP;
    case Qt::Key_WakeUp:
        return GHOSTTY_KEY_WAKE_UP;
    case Qt::Key_Copy:
        return GHOSTTY_KEY_COPY;
    case Qt::Key_Cut:
        return GHOSTTY_KEY_CUT;
    case Qt::Key_Paste:
        return GHOSTTY_KEY_PASTE;
    default:
        return GHOSTTY_KEY_UNIDENTIFIED;
    }
}

// A shell interrupts on the physical control key, and Qt reports that key as the meta modifier on macOS while reporting Command as the control one.
Result<QByteArray> GhosttyTerminalAdapter::encodePaste(const QByteArray& text) const {
    if (m_terminal == nullptr) {
        return Result<QByteArray>::failure({"ghostty_not_initialized", "The terminal emulator is not initialized", {}});
    }

    GhosttyTerminalModeConfig bracketed{GHOSTTY_MODE_BRACKETED_PASTE, false};

    if (ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_MODE, &bracketed) != GHOSTTY_SUCCESS) {
        return Result<QByteArray>::failure({"ghostty_mode_unavailable", "The terminal paste mode could not be read", {}});
    }

    QByteArray payload = text;
    std::size_t written = 0;
    QByteArray encoded(payload.size() + pasteMarkerReserve, '\0');
    const GhosttyResult result = ghostty_paste_encode(payload.data(), static_cast<std::size_t>(payload.size()), bracketed.value, encoded.data(), static_cast<std::size_t>(encoded.size()), &written);

    if (result != GHOSTTY_SUCCESS) {
        return Result<QByteArray>::failure(GhosttyTerminalAdapterHelper::ghosttyFailure(QStringLiteral("encode paste"), result).error());
    }

    encoded.truncate(static_cast<qsizetype>(written));
    return Result<QByteArray>::success(encoded);
}

// Text carrying a line break reaches the shell as a command it runs, which is what the confirmation exists to warn about.
bool GhosttyTerminalAdapter::pasteExecutesOnArrival(const QByteArray& text) const {
    return !ghostty_paste_is_safe(text.constData(), static_cast<std::size_t>(text.size()));
}

// A mode nobody could read belongs to a terminal that is not running, and no program is waiting on it.
bool GhosttyTerminalAdapter::modeEnabled(GhosttyMode mode) const {
    if (m_terminal == nullptr) {
        return false;
    }

    GhosttyTerminalModeConfig config{mode, false};

    if (ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_MODE, &config) != GHOSTTY_SUCCESS) {
        return false;
    }

    return config.value;
}

bool GhosttyTerminalAdapter::programWantsMouse() const {
    if (m_terminal == nullptr) {
        return false;
    }

    bool tracking = false;

    if (ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_MOUSE_TRACKING, &tracking) != GHOSTTY_SUCCESS) {
        return false;
    }

    return tracking;
}

bool GhosttyTerminalAdapter::programWantsFocus() const {
    return modeEnabled(GHOSTTY_MODE_FOCUS_EVENT);
}

// The wheel travels as the buttons the protocol reserves for it, which is what a program reading the mouse expects to receive.
// The pixel the pointer is over names a cell of the viewport, which is the reference every gesture is expressed in.
bool GhosttyTerminalAdapter::gridReferenceAt(const QPointF& position, GhosttyGridRef& reference) const {
    std::uint16_t columns = 0;
    std::uint16_t rows = 0;
    std::uint32_t widthPixels = 0;
    std::uint32_t heightPixels = 0;

    if (m_terminal == nullptr || ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_COLS, &columns) != GHOSTTY_SUCCESS || ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_ROWS, &rows) != GHOSTTY_SUCCESS) {
        return false;
    }
    if (columns == 0 || rows == 0 || ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_WIDTH_PX, &widthPixels) != GHOSTTY_SUCCESS || ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_HEIGHT_PX, &heightPixels) != GHOSTTY_SUCCESS) {
        return false;
    }

    const int column = std::clamp(static_cast<int>(position.x() / (static_cast<double>(widthPixels) / columns)), 0, columns - 1);
    const int row = std::clamp(static_cast<int>(position.y() / (static_cast<double>(heightPixels) / rows)), 0, rows - 1);

    GhosttyPoint point{};
    point.tag = GHOSTTY_POINT_TAG_VIEWPORT;
    point.value.coordinate.x = static_cast<std::uint16_t>(column);
    point.value.coordinate.y = static_cast<std::uint32_t>(row);
    reference.size = sizeof(GhosttyGridRef);
    return ghostty_terminal_grid_ref(m_terminal, point, &reference) == GHOSTTY_SUCCESS;
}

bool GhosttyTerminalAdapter::gestureGeometry(GhosttySelectionGestureGeometry& geometry) const {
    std::uint16_t columns = 0;
    std::uint32_t widthPixels = 0;
    std::uint32_t heightPixels = 0;

    if (m_terminal == nullptr || ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_COLS, &columns) != GHOSTTY_SUCCESS || ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_WIDTH_PX, &widthPixels) != GHOSTTY_SUCCESS) {
        return false;
    }
    if (columns == 0 || ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_HEIGHT_PX, &heightPixels) != GHOSTTY_SUCCESS || heightPixels == 0) {
        return false;
    }

    geometry.columns = columns;
    geometry.cell_width = widthPixels / columns;
    geometry.padding_left = 0;
    geometry.screen_height = heightPixels;
    return geometry.cell_width != 0;
}

// The terminal owns the selection once it is installed, so it stays correct while the program keeps writing and the viewport keeps moving.
void GhosttyTerminalAdapter::installSelection(const GhosttySelection* selection) {
    if (m_terminal == nullptr) {
        return;
    }

    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_SELECTION, selection);

    if (m_renderState != nullptr) {
        ghostty_render_state_update(m_renderState, m_terminal);
    }
}

Result<void> GhosttyTerminalAdapter::applyGesture(GhosttySelectionGestureEvent event) {
    GhosttySelection selection{};
    selection.size = sizeof(GhosttySelection);
    const GhosttyResult result = ghostty_selection_gesture_event(m_gesture, m_terminal, event, &selection);

    if (result == GHOSTTY_SUCCESS) {
        installSelection(&selection);
        return Result<void>::success();
    }

    if (result == GHOSTTY_NO_VALUE) {
        return Result<void>::success();
    }

    return GhosttyTerminalAdapterHelper::ghosttyFailure(QStringLiteral("Selection gesture failed"), result);
}

Result<void> GhosttyTerminalAdapter::beginSelection(const QPointF& position, quint64 timeNanoseconds, quint64 repeatIntervalNanoseconds, double repeatDistance, bool rectangle) {
    if (m_terminal == nullptr || m_gesture == nullptr) {
        return Result<void>::failure({"ghostty_not_initialized", "The terminal emulator is not initialized", {}});
    }

    GhosttyGridRef reference{};

    if (!gridReferenceAt(position, reference)) {
        return Result<void>::failure({"ghostty_position_invalid", "The pointer is outside the terminal grid", {}});
    }

    const GhosttySurfacePosition surfacePosition{position.x(), position.y()};
    ghostty_selection_gesture_event_set(m_pressEvent, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_REF, &reference);
    ghostty_selection_gesture_event_set(m_pressEvent, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_POSITION, &surfacePosition);
    ghostty_selection_gesture_event_set(m_pressEvent, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_TIME_NS, &timeNanoseconds);
    ghostty_selection_gesture_event_set(m_pressEvent, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_REPEAT_INTERVAL_NS, &repeatIntervalNanoseconds);
    ghostty_selection_gesture_event_set(m_pressEvent, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_REPEAT_DISTANCE, &repeatDistance);
    ghostty_selection_gesture_event_set(m_pressEvent, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_RECTANGLE, &rectangle);

    // A press that selects nothing is the click that starts one, so whatever was selected before is dropped.
    installSelection(nullptr);
    return applyGesture(m_pressEvent);
}

Result<void> GhosttyTerminalAdapter::extendSelection(const QPointF& position, bool rectangle) {
    if (m_terminal == nullptr || m_gesture == nullptr) {
        return Result<void>::failure({"ghostty_not_initialized", "The terminal emulator is not initialized", {}});
    }

    GhosttyGridRef reference{};
    GhosttySelectionGestureGeometry geometry{};

    if (!gridReferenceAt(position, reference) || !gestureGeometry(geometry)) {
        return Result<void>::failure({"ghostty_position_invalid", "The pointer is outside the terminal grid", {}});
    }

    const GhosttySurfacePosition surfacePosition{position.x(), position.y()};
    ghostty_selection_gesture_event_set(m_dragEvent, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_REF, &reference);
    ghostty_selection_gesture_event_set(m_dragEvent, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_POSITION, &surfacePosition);
    ghostty_selection_gesture_event_set(m_dragEvent, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_GEOMETRY, &geometry);
    ghostty_selection_gesture_event_set(m_dragEvent, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_RECTANGLE, &rectangle);
    return applyGesture(m_dragEvent);
}

void GhosttyTerminalAdapter::endSelection(const QPointF& position) {
    if (m_terminal == nullptr || m_gesture == nullptr) {
        return;
    }

    GhosttyGridRef reference{};
    const bool resolved = gridReferenceAt(position, reference);
    ghostty_selection_gesture_event_set(m_releaseEvent, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_REF, resolved ? &reference : nullptr);
    ghostty_selection_gesture_event(m_gesture, m_terminal, m_releaseEvent, nullptr);
}

SelectionAutoscroll GhosttyTerminalAdapter::selectionAutoscroll() const {
    if (m_terminal == nullptr || m_gesture == nullptr) {
        return SelectionAutoscroll::None;
    }

    GhosttySelectionGestureAutoscroll autoscroll = GHOSTTY_SELECTION_GESTURE_AUTOSCROLL_NONE;

    if (ghostty_selection_gesture_get(m_gesture, m_terminal, GHOSTTY_SELECTION_GESTURE_DATA_AUTOSCROLL, &autoscroll) != GHOSTTY_SUCCESS) {
        return SelectionAutoscroll::None;
    }

    switch (autoscroll) {
    case GHOSTTY_SELECTION_GESTURE_AUTOSCROLL_UP:
        return SelectionAutoscroll::Up;
    case GHOSTTY_SELECTION_GESTURE_AUTOSCROLL_DOWN:
        return SelectionAutoscroll::Down;
    default:
        return SelectionAutoscroll::None;
    }
}

Result<void> GhosttyTerminalAdapter::advanceSelectionAutoscroll(const QPointF& position, bool rectangle) {
    const SelectionAutoscroll direction = selectionAutoscroll();

    if (direction == SelectionAutoscroll::None) {
        return Result<void>::success();
    }

    scrollViewport(direction == SelectionAutoscroll::Up ? -1 : 1);

    GhosttySelectionGestureGeometry geometry{};

    if (!gestureGeometry(geometry)) {
        return Result<void>::failure({"ghostty_geometry_unavailable", "The terminal geometry could not be read", {}});
    }

    std::uint16_t columns = 0;
    std::uint16_t rows = 0;
    ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_COLS, &columns);
    ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_ROWS, &rows);

    if (columns == 0 || rows == 0) {
        return Result<void>::failure({"ghostty_geometry_unavailable", "The terminal geometry could not be read", {}});
    }

    GhosttyPointCoordinate viewport{};
    viewport.x = static_cast<std::uint16_t>(std::clamp(static_cast<int>(position.x() / geometry.cell_width), 0, columns - 1));
    viewport.y = direction == SelectionAutoscroll::Up ? 0 : static_cast<std::uint32_t>(rows - 1);

    const GhosttySurfacePosition surfacePosition{position.x(), position.y()};
    ghostty_selection_gesture_event_set(m_autoscrollEvent, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_VIEWPORT, &viewport);
    ghostty_selection_gesture_event_set(m_autoscrollEvent, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_GEOMETRY, &geometry);
    ghostty_selection_gesture_event_set(m_autoscrollEvent, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_POSITION, &surfacePosition);
    ghostty_selection_gesture_event_set(m_autoscrollEvent, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_RECTANGLE, &rectangle);
    return applyGesture(m_autoscrollEvent);
}

// Every row of the screen is read once, so a query is answered against the history as well as against what is on view.
QString GhosttyTerminalAdapter::screenText() const {
    if (m_terminal == nullptr) {
        return {};
    }

    GhosttySelection selection{};
    selection.size = sizeof(GhosttySelection);

    if (ghostty_terminal_select_all(m_terminal, &selection) != GHOSTTY_SUCCESS) {
        return {};
    }

    return formatSelection(&selection, false);
}

QList<SearchMatch> GhosttyTerminalAdapter::search(const QString& query, bool caseSensitive, bool wholeWord, int maximum) const {
    QList<SearchMatch> matches;

    if (query.isEmpty() || maximum <= 0) {
        return matches;
    }

    const Qt::CaseSensitivity sensitivity = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    const QStringList rows = screenText().split(QLatin1Char('\n'));

    for (qsizetype row = 0; row < rows.size() && matches.size() < maximum; ++row) {
        const QString& text = rows.at(row);
        qsizetype from = text.indexOf(query, 0, sensitivity);
        while (from >= 0 && matches.size() < maximum) {
            if (!wholeWord || GhosttyTerminalAdapterHelper::standsAlone(text, from, query.size())) {
                matches.append({static_cast<quint64>(row), static_cast<int>(from), static_cast<int>(query.size())});
            }
            from = text.indexOf(query, from + 1, sensitivity);
        }
    }

    return matches;
}

// The match becomes the selection and the viewport moves to where it is, which is what reading a result means.
bool GhosttyTerminalAdapter::revealMatch(const SearchMatch& match) {
    if (m_terminal == nullptr || match.length <= 0) {
        return false;
    }

    GhosttyGridRef start{};
    GhosttyGridRef end{};

    if (!screenReference(match.row, match.column, start) || !screenReference(match.row, match.column + match.length - 1, end)) {
        return false;
    }

    GhosttySelection selection{};
    selection.size = sizeof(GhosttySelection);
    selection.start = start;
    selection.end = end;
    selection.rectangle = false;
    installSelection(&selection);

    GhosttyTerminalScrollbar scrollbar{};

    if (ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_SCROLLBAR, &scrollbar) != GHOSTTY_SUCCESS || scrollbar.len == 0) {
        return true;
    }

    const quint64 highest = scrollbar.total > scrollbar.len ? scrollbar.total - scrollbar.len : 0;
    const quint64 centered = match.row > scrollbar.len / 2 ? match.row - (scrollbar.len / 2) : 0;
    scrollToRow(std::min(centered, highest));
    return true;
}

bool GhosttyTerminalAdapter::screenReference(quint64 row, int column, GhosttyGridRef& reference) const {
    GhosttyPoint point{};
    point.tag = GHOSTTY_POINT_TAG_SCREEN;
    point.value.coordinate.x = static_cast<std::uint16_t>(column);
    point.value.coordinate.y = static_cast<std::uint32_t>(row);
    reference.size = sizeof(GhosttyGridRef);
    return ghostty_terminal_grid_ref(m_terminal, point, &reference) == GHOSTTY_SUCCESS;
}

void GhosttyTerminalAdapter::selectAll() {
    if (m_terminal == nullptr) {
        return;
    }

    GhosttySelection selection{};
    selection.size = sizeof(GhosttySelection);

    if (ghostty_terminal_select_all(m_terminal, &selection) != GHOSTTY_SUCCESS) {
        return;
    }

    installSelection(&selection);
}

QString GhosttyTerminalAdapter::addressAt(const QPointF& position) const {
    GhosttyGridRef reference{};

    if (!gridReferenceAt(position, reference)) {
        return {};
    }

    std::array<std::uint8_t, hyperlinkMaximumBytes> buffer{};
    std::size_t written = 0;

    if (ghostty_grid_ref_hyperlink_uri(&reference, buffer.data(), buffer.size(), &written) == GHOSTTY_SUCCESS && written > 0) {
        return QString::fromUtf8(reinterpret_cast<const char*>(buffer.data()), static_cast<qsizetype>(written));
    }

    return GhosttyTerminalAdapterHelper::acceptedAddress(wordAt(reference));
}

// The word under the pointer is the run the blanks around it delimit, which is exactly how far an address written in plain text reaches.
QString GhosttyTerminalAdapter::wordAt(const GhosttyGridRef& reference) const {
    constexpr std::array<std::uint32_t, 2> blanks{' ', '\t'};

    GhosttyTerminalSelectWordOptions options{};
    options.size = sizeof(GhosttyTerminalSelectWordOptions);
    options.ref = reference;
    options.boundary_codepoints = blanks.data();
    options.boundary_codepoints_len = blanks.size();

    GhosttySelection selection{};
    selection.size = sizeof(GhosttySelection);

    if (ghostty_terminal_select_word(m_terminal, &options, &selection) != GHOSTTY_SUCCESS) {
        return {};
    }

    return formatSelection(&selection, true);
}

void GhosttyTerminalAdapter::clearScrollback() {
    clearSelection();
    write(QByteArrayLiteral("\x1b[H\x1b[2J\x1b[3J"));
}

void GhosttyTerminalAdapter::clearSelection() {
    if (m_gesture != nullptr && m_terminal != nullptr) {
        ghostty_selection_gesture_reset(m_gesture, m_terminal);
    }

    installSelection(nullptr);
}

bool GhosttyTerminalAdapter::hasSelection() const {
    if (m_terminal == nullptr) {
        return false;
    }

    GhosttySelection selection{};
    selection.size = sizeof(GhosttySelection);
    return ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_SELECTION, &selection) == GHOSTTY_SUCCESS;
}

// The format options declare a field named emit, which collides with the Qt keyword macro.
#if defined(emit)
#undef emit
#define WORKPANE_TERMINAL_RESTORE_EMIT
#endif

// A wrapped line is copied as the single line it really is, and a trailing run of blanks is not part of what was read.
QString GhosttyTerminalAdapter::selectionText() const {
    return formatSelection(nullptr, true);
}

QString GhosttyTerminalAdapter::formatSelection(const GhosttySelection* selection, bool unwrap) const {
    if (m_terminal == nullptr) {
        return {};
    }

    GhosttyTerminalSelectionFormatOptions options{};
    options.size = sizeof(GhosttyTerminalSelectionFormatOptions);
    options.emit = GHOSTTY_FORMATTER_FORMAT_PLAIN;
    options.unwrap = unwrap;
    options.trim = true;
    options.selection = selection;

    std::uint8_t* buffer = nullptr;
    std::size_t length = 0;

    if (ghostty_terminal_selection_format_alloc(m_terminal, nullptr, options, &buffer, &length) != GHOSTTY_SUCCESS) {
        return {};
    }

    const QString text = QString::fromUtf8(reinterpret_cast<const char*>(buffer), static_cast<qsizetype>(length));
    ghostty_free(nullptr, buffer, length);
    return text;
}

#if defined(WORKPANE_TERMINAL_RESTORE_EMIT)
#undef WORKPANE_TERMINAL_RESTORE_EMIT
#define emit
#endif

GhosttyMouseAction GhosttyTerminalAdapter::mapMouseAction(MouseAction action) {
    switch (action) {
    case MouseAction::Press:
        return GHOSTTY_MOUSE_ACTION_PRESS;
    case MouseAction::Release:
        return GHOSTTY_MOUSE_ACTION_RELEASE;
    case MouseAction::Motion:
        return GHOSTTY_MOUSE_ACTION_MOTION;
    }

    return GHOSTTY_MOUSE_ACTION_MOTION;
}

GhosttyMouseButton GhosttyTerminalAdapter::mapMouseButton(MouseButton button) {
    switch (button) {
    case MouseButton::None:
        return GHOSTTY_MOUSE_BUTTON_UNKNOWN;
    case MouseButton::Left:
        return GHOSTTY_MOUSE_BUTTON_LEFT;
    case MouseButton::Right:
        return GHOSTTY_MOUSE_BUTTON_RIGHT;
    case MouseButton::Middle:
        return GHOSTTY_MOUSE_BUTTON_MIDDLE;
    case MouseButton::WheelUp:
        return GHOSTTY_MOUSE_BUTTON_FOUR;
    case MouseButton::WheelDown:
        return GHOSTTY_MOUSE_BUTTON_FIVE;
    case MouseButton::WheelLeft:
        return GHOSTTY_MOUSE_BUTTON_SIX;
    case MouseButton::WheelRight:
        return GHOSTTY_MOUSE_BUTTON_SEVEN;
    }

    return GHOSTTY_MOUSE_BUTTON_UNKNOWN;
}

Result<QByteArray> GhosttyTerminalAdapter::encodeMouse(const MouseReport& report) {
    if (m_terminal == nullptr || m_mouseEncoder == nullptr || m_mouseEvent == nullptr) {
        return Result<QByteArray>::failure({"ghostty_not_initialized", "The terminal emulator is not initialized", {}});
    }

    std::uint16_t columns = 0;
    std::uint16_t rows = 0;
    std::uint32_t widthPixels = 0;
    std::uint32_t heightPixels = 0;
    const bool geometryRead = ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_COLS, &columns) == GHOSTTY_SUCCESS && ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_ROWS, &rows) == GHOSTTY_SUCCESS && ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_WIDTH_PX, &widthPixels) == GHOSTTY_SUCCESS && ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_HEIGHT_PX, &heightPixels) == GHOSTTY_SUCCESS;

    if (!geometryRead || columns == 0 || rows == 0) {
        return Result<QByteArray>::failure({"ghostty_geometry_unavailable", "The terminal geometry could not be read", {}});
    }

    GhosttyMouseEncoderSize size{};
    size.size = sizeof(GhosttyMouseEncoderSize);
    size.screen_width = widthPixels;
    size.screen_height = heightPixels;
    size.cell_width = widthPixels / columns;
    size.cell_height = heightPixels / rows;

    ghostty_mouse_encoder_setopt_from_terminal(m_mouseEncoder, m_terminal);
    ghostty_mouse_encoder_setopt(m_mouseEncoder, GHOSTTY_MOUSE_ENCODER_OPT_SIZE, &size);
    ghostty_mouse_encoder_setopt(m_mouseEncoder, GHOSTTY_MOUSE_ENCODER_OPT_ANY_BUTTON_PRESSED, &report.anyButtonPressed);
    const bool trackLastCell = true;
    ghostty_mouse_encoder_setopt(m_mouseEncoder, GHOSTTY_MOUSE_ENCODER_OPT_TRACK_LAST_CELL, &trackLastCell);

    ghostty_mouse_event_set_action(m_mouseEvent, mapMouseAction(report.action));

    if (report.button == MouseButton::None) {
        ghostty_mouse_event_clear_button(m_mouseEvent);
    } else {
        ghostty_mouse_event_set_button(m_mouseEvent, mapMouseButton(report.button));
    }

    ghostty_mouse_event_set_mods(m_mouseEvent, mapModifiers(report.modifiers));
    const GhosttyMousePosition position{static_cast<float>(report.position.x()), static_cast<float>(report.position.y())};
    ghostty_mouse_event_set_position(m_mouseEvent, position);

    std::array<char, 64> stackBuffer{};
    std::size_t written = 0;
    GhosttyResult result = ghostty_mouse_encoder_encode(m_mouseEncoder, m_mouseEvent, stackBuffer.data(), stackBuffer.size(), &written);

    if (result == GHOSTTY_SUCCESS) {
        return Result<QByteArray>::success(QByteArray(stackBuffer.data(), static_cast<qsizetype>(written)));
    }
    if (result != GHOSTTY_OUT_OF_SPACE) {
        return Result<QByteArray>::failure({"ghostty_mouse_encoding_failed", "The mouse event could not be encoded", QString::number(static_cast<int>(result))});
    }

    QByteArray output(static_cast<qsizetype>(written), Qt::Uninitialized);
    result = ghostty_mouse_encoder_encode(m_mouseEncoder, m_mouseEvent, output.data(), static_cast<std::size_t>(output.size()), &written);

    if (result != GHOSTTY_SUCCESS) {
        return Result<QByteArray>::failure({"ghostty_mouse_encoding_failed", "The mouse event could not be encoded", QString::number(static_cast<int>(result))});
    }

    output.resize(static_cast<qsizetype>(written));
    return Result<QByteArray>::success(std::move(output));
}

Result<QByteArray> GhosttyTerminalAdapter::encodeFocus(bool gained) const {
    std::array<char, 16> buffer{};
    std::size_t written = 0;
    const GhosttyResult result = ghostty_focus_encode(gained ? GHOSTTY_FOCUS_GAINED : GHOSTTY_FOCUS_LOST, buffer.data(), buffer.size(), &written);

    if (result != GHOSTTY_SUCCESS) {
        return Result<QByteArray>::failure({"ghostty_focus_encoding_failed", "The focus event could not be encoded", QString::number(static_cast<int>(result))});
    }

    return Result<QByteArray>::success(QByteArray(buffer.data(), static_cast<qsizetype>(written)));
}

GhosttyMods GhosttyTerminalAdapter::mapModifiers(Qt::KeyboardModifiers modifiers) {
    GhosttyMods output = 0;

    if (modifiers.testFlag(Qt::ShiftModifier)) {
        output |= GHOSTTY_MODS_SHIFT;
    }

    if (modifiers.testFlag(Qt::AltModifier)) {
        output |= GHOSTTY_MODS_ALT;
    }

#ifdef Q_OS_MACOS

    if (modifiers.testFlag(Qt::MetaModifier)) {
        output |= GHOSTTY_MODS_CTRL;
    }

    if (modifiers.testFlag(Qt::ControlModifier)) {
        output |= GHOSTTY_MODS_SUPER;
    }

#else

    if (modifiers.testFlag(Qt::ControlModifier)) {
        output |= GHOSTTY_MODS_CTRL;
    }

    if (modifiers.testFlag(Qt::MetaModifier)) {
        output |= GHOSTTY_MODS_SUPER;
    }

#endif
    return output;
}

Result<void> GhosttyTerminalAdapter::applyTheme(const domain::TerminalTheme& theme) {
    const GhosttyColorRgb foreground = GhosttyTerminalAdapterHelper::toGhosttyColor(theme.foreground);
    const GhosttyColorRgb background = GhosttyTerminalAdapterHelper::toGhosttyColor(theme.background);
    const GhosttyColorRgb cursor = GhosttyTerminalAdapterHelper::toGhosttyColor(theme.cursor);

    std::array<GhosttyColorRgb, 256> palette{};
    ghostty_color_palette_default(palette.data());

    for (std::size_t index = 0; index < theme.ansiPalette.size(); ++index) {
        palette[index] = GhosttyTerminalAdapterHelper::toGhosttyColor(theme.ansiPalette[index]);
    }

    ghostty_color_palette_generate(palette.data(), nullptr, &background, &foreground, true, palette.data());

    const std::array<std::pair<GhosttyTerminalOption, const void*>, 4> options = {std::pair{GHOSTTY_TERMINAL_OPT_COLOR_FOREGROUND, static_cast<const void*>(&foreground)}, std::pair{GHOSTTY_TERMINAL_OPT_COLOR_BACKGROUND, static_cast<const void*>(&background)}, std::pair{GHOSTTY_TERMINAL_OPT_COLOR_CURSOR, static_cast<const void*>(&cursor)}, std::pair{GHOSTTY_TERMINAL_OPT_COLOR_PALETTE, static_cast<const void*>(palette.data())}};

    for (const auto& [option, value] : options) {
        const auto result = ghostty_terminal_set(m_terminal, option, value);
        if (result != GHOSTTY_SUCCESS) {
            return GhosttyTerminalAdapterHelper::ghosttyFailure(QStringLiteral("Terminal theme update failed"), result);
        }
    }

    return Result<void>::success();
}

void GhosttyTerminalAdapter::release() {
    for (auto* gestureEvent : {&m_autoscrollEvent, &m_releaseEvent, &m_dragEvent, &m_pressEvent}) {
        if (*gestureEvent != nullptr) {
            ghostty_selection_gesture_event_free(*gestureEvent);
            *gestureEvent = nullptr;
        }
    }

    if (m_gesture != nullptr) {
        ghostty_selection_gesture_free(m_gesture, m_terminal);
        m_gesture = nullptr;
    }

    if (m_mouseEvent != nullptr) {
        ghostty_mouse_event_free(m_mouseEvent);
        m_mouseEvent = nullptr;
    }

    if (m_mouseEncoder != nullptr) {
        ghostty_mouse_encoder_free(m_mouseEncoder);
        m_mouseEncoder = nullptr;
    }

    if (m_keyEvent != nullptr) {
        ghostty_key_event_free(m_keyEvent);
        m_keyEvent = nullptr;
    }

    if (m_keyEncoder != nullptr) {
        ghostty_key_encoder_free(m_keyEncoder);
        m_keyEncoder = nullptr;
    }

    if (m_rowCells != nullptr) {
        ghostty_render_state_row_cells_free(m_rowCells);
        m_rowCells = nullptr;
    }

    if (m_rowIterator != nullptr) {
        ghostty_render_state_row_iterator_free(m_rowIterator);
        m_rowIterator = nullptr;
    }

    if (m_renderState != nullptr) {
        ghostty_render_state_free(m_renderState);
        m_renderState = nullptr;
    }

    if (m_terminal != nullptr) {
        ghostty_terminal_free(m_terminal);
        m_terminal = nullptr;
    }
}

} // namespace workpane::terminalcore
