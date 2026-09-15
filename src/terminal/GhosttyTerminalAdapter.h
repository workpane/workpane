#pragma once

#include "domain/Result.h"
#include "domain/TerminalTheme.h"
#include "terminal/GhosttyHeaders.h"

#include <QByteArray>
#include <QColor>
#include <QKeyEvent>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QVector>

#include <cstddef>
#include <cstdint>

namespace workpane::terminalcore {

struct RenderCell final {
    int column{};
    int row{};
    QString text;
    QColor foreground;
    QColor background;
    bool hasBackground{false};
    bool bold{false};
    bool italic{false};
    bool faint{false};
    bool inverse{false};
    bool strikethrough{false};
    bool selected{false};
    int underline{};
};

// A program that asked for the mouse receives what the user did with it, and the widget reports the action rather than the Qt event that carried it.
enum class MouseAction { Press, Release, Motion };

enum class MouseButton { None, Left, Right, Middle, WheelUp, WheelDown, WheelLeft, WheelRight };

// A program chooses the shape of its cursor, and an editor changing mode is the reason that shape has to reach the screen.
enum class CursorStyle { Bar, Block, Underline, HollowBlock };

// A match is named by the row of the whole screen it sits on, so it is found again after the viewport has moved.
struct SearchMatch final {
    quint64 row{};
    int column{};
    int length{};

    [[nodiscard]] bool operator==(const SearchMatch& other) const = default;
};

// A drag that leaves the grid asks the viewport to follow it, which is how a selection reaches beyond the rows on screen.
enum class SelectionAutoscroll { None, Up, Down };

struct MouseReport final {
    MouseAction action{MouseAction::Motion};
    MouseButton button{MouseButton::None};
    Qt::KeyboardModifiers modifiers;
    // The position is measured in pixels from the first cell of the grid, so the emulator resolves the cell it names.
    QPointF position;
    bool anyButtonPressed{false};
};

struct TerminalRenderSnapshot final {
    int columns{1};
    int rows{1};
    QColor foreground{QStringLiteral("#d6d9e0")};
    QColor background{QStringLiteral("#111318")};
    QColor cursor{QStringLiteral("#d6d9e0")};
    QPoint cursorPosition;
    bool cursorVisible{true};
    bool cursorBlinking{false};
    bool cursorWide{false};
    CursorStyle cursorStyle{CursorStyle::Block};
    quint64 scrollTotal{};
    quint64 scrollOffset{};
    quint64 scrollViewport{};
    QVector<RenderCell> cells;
};

class GhosttyTerminalAdapter final : public QObject {
    Q_OBJECT

  public:
    explicit GhosttyTerminalAdapter(QObject* parent = nullptr);
    ~GhosttyTerminalAdapter() override;

    GhosttyTerminalAdapter(const GhosttyTerminalAdapter&) = delete;
    GhosttyTerminalAdapter& operator=(const GhosttyTerminalAdapter&) = delete;

    [[nodiscard]] Result<void> initialize(int columns, int rows, int cellWidth, int cellHeight, const domain::TerminalTheme& theme);
    [[nodiscard]] Result<void> setTheme(const domain::TerminalTheme& theme);
    void write(const QByteArray& bytes);
    [[nodiscard]] Result<void> resize(int columns, int rows, int cellWidth, int cellHeight);
    [[nodiscard]] Result<QByteArray> encodeKey(const QKeyEvent& event);
    // A shell runs every line a plain paste delivers, so text is handed over between the markers whenever the program asked to receive it that way.
    [[nodiscard]] Result<QByteArray> encodePaste(const QByteArray& text) const;
    [[nodiscard]] bool pasteExecutesOnArrival(const QByteArray& text) const;
    // A program asks for the mouse and for the focus through its own modes, and what the user does reaches it only while it did.
    [[nodiscard]] bool programWantsMouse() const;
    [[nodiscard]] bool programWantsFocus() const;
    [[nodiscard]] Result<QByteArray> encodeMouse(const MouseReport& report);
    [[nodiscard]] Result<QByteArray> encodeFocus(bool gained) const;
    // The selection belongs to the emulator, so it reaches the scrollback, follows a wrapped line and survives everything the program writes.
    [[nodiscard]] Result<void> beginSelection(const QPointF& position, quint64 timeNanoseconds, quint64 repeatIntervalNanoseconds, double repeatDistance, bool rectangle);
    [[nodiscard]] Result<void> extendSelection(const QPointF& position, bool rectangle);
    void endSelection(const QPointF& position);
    [[nodiscard]] SelectionAutoscroll selectionAutoscroll() const;
    [[nodiscard]] Result<void> advanceSelectionAutoscroll(const QPointF& position, bool rectangle);
    void selectAll();
    void clearSelection();
    // The library exposes no clearing door of its own, so the buffer is erased with the sequences a program would send.
    void clearScrollback();
    [[nodiscard]] bool hasSelection() const;
    [[nodiscard]] QString selectionText() const;
    // A program writing to the clipboard is refused until the user allows it, because anything printed to the terminal could ask for it.
    void setClipboardWriteAllowed(bool allowed);
    // Searching reads the whole screen, history included, and stops at the bound the caller declares.
    [[nodiscard]] QList<SearchMatch> search(const QString& query, bool caseSensitive, bool wholeWord, int maximum) const;
    [[nodiscard]] bool revealMatch(const SearchMatch& match);
    // A program marks its own links, and everything else is the address written inside the word under the pointer.
    [[nodiscard]] QString addressAt(const QPointF& position) const;
    void scrollViewport(qint64 rows);
    void scrollToRow(quint64 row);
    void scrollToTop();
    void scrollToBottom();
    [[nodiscard]] TerminalRenderSnapshot snapshot();
    [[nodiscard]] QString title() const;
    [[nodiscard]] QString workingDirectory() const;

  signals:
    void responseReady(const QByteArray& bytes);
    void titleChanged();
    void workingDirectoryChanged();
    void bellRang();
    void clipboardWriteRequested(const QString& text);
    void notificationPosted(const QString& title, const QString& body);

  private:
    static void writePtyCallback(GhosttyTerminal terminal, void* userData, const std::uint8_t* data, std::size_t size);
    static void titleChangedCallback(GhosttyTerminal terminal, void* userData);
    static void pwdChangedCallback(GhosttyTerminal terminal, void* userData);
    static void bellCallback(GhosttyTerminal terminal, void* userData);
    static GhosttyClipboardWriteResult clipboardWriteCallback(GhosttyTerminal terminal, void* userData, const GhosttyClipboardWrite* write);
    static void desktopNotificationCallback(GhosttyTerminal terminal, void* userData, const GhosttyTerminalDesktopNotification* notification);
    static bool deviceAttributesCallback(GhosttyTerminal terminal, void* userData, GhosttyDeviceAttributes* attributes);
    static bool sizeCallback(GhosttyTerminal terminal, void* userData, GhosttySizeReportSize* size);
    static bool colorSchemeCallback(GhosttyTerminal terminal, void* userData, GhosttyColorScheme* scheme);
    static GhosttyString xtversionCallback(GhosttyTerminal terminal, void* userData);
    [[nodiscard]] static GhosttyKey mapKey(const QKeyEvent& event);
    [[nodiscard]] static GhosttyMods mapModifiers(Qt::KeyboardModifiers modifiers);
    [[nodiscard]] static GhosttyMouseAction mapMouseAction(MouseAction action);
    [[nodiscard]] static GhosttyMouseButton mapMouseButton(MouseButton button);
    [[nodiscard]] bool modeEnabled(GhosttyMode mode) const;
    [[nodiscard]] bool gridReferenceAt(const QPointF& position, GhosttyGridRef& reference) const;
    [[nodiscard]] bool gestureGeometry(GhosttySelectionGestureGeometry& geometry) const;
    [[nodiscard]] Result<void> applyGesture(GhosttySelectionGestureEvent event);
    void installSelection(const GhosttySelection* selection);
    [[nodiscard]] QString formatSelection(const GhosttySelection* selection, bool unwrap) const;
    [[nodiscard]] QString screenText() const;
    [[nodiscard]] bool screenReference(quint64 row, int column, GhosttyGridRef& reference) const;
    [[nodiscard]] QString wordAt(const GhosttyGridRef& reference) const;
    [[nodiscard]] Result<void> applyTheme(const domain::TerminalTheme& theme);
    void release();

    GhosttyTerminal m_terminal{nullptr};
    GhosttyRenderState m_renderState{nullptr};
    GhosttyRenderStateRowIterator m_rowIterator{nullptr};
    GhosttyRenderStateRowCells m_rowCells{nullptr};
    GhosttyKeyEncoder m_keyEncoder{nullptr};
    GhosttyKeyEvent m_keyEvent{nullptr};
    GhosttyMouseEncoder m_mouseEncoder{nullptr};
    GhosttyMouseEvent m_mouseEvent{nullptr};
    GhosttySelectionGesture m_gesture{nullptr};
    GhosttySelectionGestureEvent m_pressEvent{nullptr};
    GhosttySelectionGestureEvent m_dragEvent{nullptr};
    GhosttySelectionGestureEvent m_releaseEvent{nullptr};
    GhosttySelectionGestureEvent m_autoscrollEvent{nullptr};
    bool m_clipboardWriteAllowed{false};
};

} // namespace workpane::terminalcore
