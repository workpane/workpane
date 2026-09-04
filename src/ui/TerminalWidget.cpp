#include "ui/TerminalWidget.h"

#include "terminal/TerminalShortcuts.h"
#include "ui/Components.h"
#include "ui/Theme.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHideEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QMutexLocker>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStyleHints>
#include <QWheelEvent>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace workpane::ui {

// A drag held outside the grid moves the viewport at a readable pace rather than as fast as the events arrive.
constexpr int autoscrollIntervalMs = 50;

// A wheel reports an eighth of a degree, and one notch of the market convention is fifteen degrees of them.
constexpr int wheelNotchAngle = 120;
constexpr int cellsPerWheelNotch = 3;

class TerminalWidgetHelper final {
  public:
    static int cellsScrolled(int pixelDelta, int cellSize, int& remainder);
    static int cellsNotched(int angleDelta, int& remainder);
};

int TerminalWidgetHelper::cellsScrolled(int pixelDelta, int cellSize, int& remainder) {
    remainder += pixelDelta;
    const int cells = remainder / cellSize;
    remainder %= cellSize;
    return cells;
}

int TerminalWidgetHelper::cellsNotched(int angleDelta, int& remainder) {
    remainder += angleDelta;
    const int notches = remainder / wheelNotchAngle;
    remainder %= wheelNotchAngle;
    return notches * cellsPerWheelNotch;
}

// A bar and an underline are drawn as the thin edge of the cell they mark.
constexpr qreal cursorEdgeThickness = 2.0;

// Marking every match of a query over a long history would cost more than the marks are worth, so the count says it stopped.
constexpr int searchMatchMaximum = 1000;

TerminalWidget::TerminalWidget(plugins::PluginHost& host, QWidget* parent) : QWidget(parent), m_host(host) {
    setAcceptDrops(true);
    setAttribute(Qt::WA_InputMethodEnabled);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::IBeamCursor);
    // A program tracking every mouse move is only served when the pointer is followed without a button held down.
    setMouseTracking(true);
    m_autoscrollTimer.setInterval(autoscrollIntervalMs);
    const FindBarLabels labels{m_host.translate(QStringLiteral("terminal.find.label")), m_host.translate(QStringLiteral("terminal.find.case-sensitive")), m_host.translate(QStringLiteral("terminal.find.whole-word")), m_host.translate(QStringLiteral("terminal.find.previous")), m_host.translate(QStringLiteral("terminal.find.next")), m_host.translate(QStringLiteral("terminal.find.close")), m_host.translate(QStringLiteral("terminal.find.not-found"))};
    m_findBar = new FindBar(m_host.theme(), labels, this);
    m_findBar->hide();
    // clang-format off
    connect(m_findBar, &FindBar::queryChanged, this, [this]() { refreshMatches(); });
    connect(m_findBar, &FindBar::searchRequested, this, [this](const QString&, bool forward) { stepMatch(forward); });
    connect(m_findBar, &FindBar::dismissed, this, [this]() { dismissFindBar(); });
    // clang-format on
    m_cursorTimer.setInterval(std::max(1, QGuiApplication::styleHints()->cursorFlashTime() / 2));
    // clang-format off
    connect(&m_cursorTimer, &QTimer::timeout, this, [this]() { m_cursorOn = !m_cursorOn; update(); });
    // clang-format on
    // clang-format off
    connect(&m_autoscrollTimer, &QTimer::timeout, this, [this]() { advanceAutoscroll(); });
    // clang-format on
    m_scrollBar = new QScrollBar(Qt::Vertical, this);
    m_scrollBar->setObjectName(QStringLiteral("terminalScrollBar"));
    m_scrollBar->setVisible(false);
    // clang-format off
    const auto scrollToRow = [this](int value) {
        if (!m_updatingScrollBar && m_session != nullptr) {
            m_session->scrollToRow(static_cast<quint64>(value));
        }
    };
    // clang-format on
    connect(m_scrollBar, &QScrollBar::valueChanged, this, scrollToRow);
    setTerminalFont({}, m_fontSize);

    m_resizeTimer.setSingleShot(true);
    m_resizeTimer.setInterval(40);
    connect(&m_resizeTimer, &QTimer::timeout, this, &TerminalWidget::updateTerminalSize);
    m_renderTimer.setSingleShot(true);
    m_renderTimer.setInterval(16);
    connect(&m_renderTimer, &QTimer::timeout, this, &TerminalWidget::refreshSnapshot);
}

void TerminalWidget::setSession(terminalcore::TerminalSession* newSession) {
    if (m_session == newSession) {
        return;
    }

    m_resizeTimer.stop();
    m_renderTimer.stop();

    if (m_renderConnection) {
        disconnect(m_renderConnection);
        m_renderConnection = {};
    }

    if (m_clipboardConnection) {
        disconnect(m_clipboardConnection);
        m_clipboardConnection = {};
    }

    m_session = newSession;
    m_wheelPixelRemainderX = 0;
    m_wheelPixelRemainderY = 0;
    m_wheelAngleRemainderX = 0;
    m_wheelAngleRemainderY = 0;

    if (m_session != nullptr) {
        m_renderConnection = connect(m_session, &terminalcore::TerminalSession::renderChanged, this, &TerminalWidget::scheduleSnapshotRefresh);
        // clang-format off
        m_clipboardConnection = connect(m_session, &terminalcore::TerminalSession::clipboardWriteRequested, this, [](const QString& text) { QGuiApplication::clipboard()->setText(text); });
        // clang-format on
        refreshSnapshot();
        scheduleTerminalResize();
    } else {
        {
            const QMutexLocker locker(&m_snapshotMutex);
            m_snapshot = {};
        }
        m_scrollBar->hide();
    }

    update();
}

void TerminalWidget::setTerminalFont(const QString& family, qreal pointSize) {
    m_font.setFamily(family.isEmpty() ? Components::defaultMonospacedFontFamily() : family);
    m_font.setStyleHint(QFont::Monospace);
    m_font.setFixedPitch(true);
    m_fontSize = std::clamp(pointSize, 8.0, 36.0);
    m_font.setPointSizeF(m_fontSize);
    updateCellMetrics();
    updateGeometry();
    scheduleTerminalResize();
    update();
}

QSize TerminalWidget::minimumSizeHint() const {
    return {m_host.theme().metric(ThemeMetric::TerminalHorizontalPadding) * 2 + m_host.theme().metric(ThemeMetric::TerminalMinimumColumns) * m_cellWidth, m_host.theme().metric(ThemeMetric::TerminalVerticalPadding) * 2 + m_host.theme().metric(ThemeMetric::TerminalMinimumRows) * m_cellHeight};
}

void TerminalWidget::setConfirmMultilinePaste(bool enabled) {
    m_confirmMultilinePaste = enabled;
}

bool TerminalWidget::event(QEvent* event) {
    if (event->type() == QEvent::ShortcutOverride) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (terminalcore::TerminalShortcuts::isTerminalOwned(*keyEvent)) {
            keyEvent->accept();
            return true;
        }
    } else if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab) {
            keyPressEvent(keyEvent);
            return true;
        }
    }

    return QWidget::event(event);
}

void TerminalWidget::paintEvent(QPaintEvent*) {
    terminalcore::TerminalRenderSnapshot renderSnapshot;
    {
        const QMutexLocker locker(&m_snapshotMutex);
        renderSnapshot = m_snapshot;
    }

    QPainter painter(this);
    painter.fillRect(rect(), renderSnapshot.background);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setFont(m_font);

    const QFontMetricsF fontMetrics(m_font);
    const qreal baseline = (static_cast<qreal>(m_cellHeight) - fontMetrics.height()) / 2.0 + fontMetrics.ascent();

    for (const auto& cell : renderSnapshot.cells) {
        const QRectF cellRect(m_host.theme().metric(ThemeMetric::TerminalHorizontalPadding) + static_cast<qreal>(cell.column * m_cellWidth), m_host.theme().metric(ThemeMetric::TerminalVerticalPadding) + static_cast<qreal>(cell.row * m_cellHeight), m_cellWidth, m_cellHeight);
        QColor foreground = cell.foreground;
        QColor background = cell.background;
        if (cell.inverse) {
            std::swap(foreground, background);
        }
        if (cell.hasBackground || cell.inverse) {
            painter.fillRect(cellRect, background);
        }
        if (cell.selected) {
            QColor selection = m_host.theme().color(ThemeColor::Accent);
            selection.setAlphaF(0.35F);
            painter.fillRect(cellRect, selection);
        }
        if (cell.text.isEmpty()) {
            continue;
        }

        QFont cellFont = m_font;
        cellFont.setBold(cell.bold);
        cellFont.setItalic(cell.italic);
        cellFont.setStrikeOut(cell.strikethrough);
        cellFont.setUnderline(cell.underline != 0);
        painter.setFont(cellFont);

        if (cell.faint) {
            foreground.setAlphaF(0.58F);
        }
        painter.setPen(foreground);
        painter.drawText(QPointF(cellRect.left(), cellRect.top() + baseline), cell.text);
    }

    paintMatches(painter, renderSnapshot);
    paintPreedit(painter, renderSnapshot);

    if (renderSnapshot.cursorVisible) {
        paintCursor(painter, renderSnapshot);
    }
}

// Every match is marked so the reader sees how the results are spread, and the one being read carries the selection instead.
void TerminalWidget::paintMatches(QPainter& painter, const terminalcore::TerminalRenderSnapshot& renderSnapshot) {
    if (m_matches.isEmpty()) {
        return;
    }

    QColor marker = m_host.theme().color(ThemeColor::Warning);
    marker.setAlphaF(0.35F);
    const auto& theme = m_host.theme();

    for (const auto& match : m_matches) {
        const qint64 row = static_cast<qint64>(match.row) - static_cast<qint64>(renderSnapshot.scrollOffset);
        if (row < 0 || row >= renderSnapshot.rows) {
            continue;
        }

        const QRectF markerRect(theme.metric(ThemeMetric::TerminalHorizontalPadding) + static_cast<qreal>(match.column * m_cellWidth), theme.metric(ThemeMetric::TerminalVerticalPadding) + static_cast<qreal>(row * m_cellHeight), static_cast<qreal>(match.length * m_cellWidth), m_cellHeight);
        painter.fillRect(markerRect, marker);
    }
}

// The composed text sits where the cursor is, underlined, so it reads as text that has not been sent yet.
void TerminalWidget::paintPreedit(QPainter& painter, const terminalcore::TerminalRenderSnapshot& renderSnapshot) {
    if (m_preedit.isEmpty()) {
        return;
    }

    const auto& theme = m_host.theme();
    const QRectF area(theme.metric(ThemeMetric::TerminalHorizontalPadding) + static_cast<qreal>(renderSnapshot.cursorPosition.x() * m_cellWidth), theme.metric(ThemeMetric::TerminalVerticalPadding) + static_cast<qreal>(renderSnapshot.cursorPosition.y() * m_cellHeight), static_cast<qreal>(width()), m_cellHeight);
    painter.fillRect(area, renderSnapshot.background);

    QFont composingFont = m_font;
    composingFont.setUnderline(true);
    painter.setFont(composingFont);
    painter.setPen(renderSnapshot.foreground);
    painter.drawText(area, Qt::AlignLeft | Qt::AlignVCenter, m_preedit);
}

// The shape the program asked for is the one that is drawn, and a terminal nobody is typing into shows the outline instead of the filled cell.
void TerminalWidget::paintCursor(QPainter& painter, const terminalcore::TerminalRenderSnapshot& renderSnapshot) {
    const qreal width = renderSnapshot.cursorWide ? m_cellWidth * 2.0 : m_cellWidth;
    const QRectF cell(m_host.theme().metric(ThemeMetric::TerminalHorizontalPadding) + static_cast<qreal>(renderSnapshot.cursorPosition.x() * m_cellWidth), m_host.theme().metric(ThemeMetric::TerminalVerticalPadding) + static_cast<qreal>(renderSnapshot.cursorPosition.y() * m_cellHeight), width, m_cellHeight);
    QColor cursorColor = renderSnapshot.cursor;

    if (!hasFocus()) {
        painter.setPen(QPen(cursorColor, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(cell.adjusted(0.5, 0.5, -0.5, -0.5));
        return;
    }

    if (renderSnapshot.cursorBlinking && !m_cursorOn) {
        return;
    }

    cursorColor.setAlphaF(0.72F);

    switch (renderSnapshot.cursorStyle) {
    case terminalcore::CursorStyle::Bar:
        painter.fillRect(QRectF(cell.left(), cell.top(), cursorEdgeThickness, cell.height()), cursorColor);
        return;
    case terminalcore::CursorStyle::Underline:
        painter.fillRect(QRectF(cell.left(), cell.bottom() - cursorEdgeThickness, cell.width(), cursorEdgeThickness), cursorColor);
        return;
    case terminalcore::CursorStyle::HollowBlock:
        painter.setPen(QPen(cursorColor, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(cell.adjusted(0.5, 0.5, -0.5, -0.5));
        return;
    case terminalcore::CursorStyle::Block:
        painter.fillRect(cell, cursorColor);
        return;
    }
}

void TerminalWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    const int horizontalPadding = m_host.theme().metric(ThemeMetric::TerminalHorizontalPadding);
    m_scrollBar->setGeometry(width() - horizontalPadding, 0, horizontalPadding, height());
    m_scrollBar->raise();

    if (m_findBar->isVisible()) {
        layoutFindBar();
        m_findBar->raise();
    }

    scheduleTerminalResize();
}

void TerminalWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    scheduleTerminalResize();
    scheduleSnapshotRefresh();
}

void TerminalWidget::hideEvent(QHideEvent* event) {
    m_resizeTimer.stop();
    m_renderTimer.stop();
    m_cursorTimer.stop();
    QWidget::hideEvent(event);
}

void TerminalWidget::focusInEvent(QFocusEvent* event) {
    QWidget::focusInEvent(event);
    emit focused();
    reportFocus(true);
    m_cursorOn = true;
    m_cursorTimer.start();
    update();
}

void TerminalWidget::focusOutEvent(QFocusEvent* event) {
    QWidget::focusOutEvent(event);
    reportFocus(false);
    m_cursorTimer.stop();
    update();
}

// A program that asked to know when the window is being looked at is told, which is what stops an editor from redrawing a cursor nobody sees.
void TerminalWidget::reportFocus(bool gained) {
    if (m_session == nullptr || !m_session->programWantsFocus()) {
        return;
    }

    const auto result = m_session->sendFocus(gained);

    if (!result.hasValue()) {
        emit interactionError(result.error());
    }
}

void TerminalWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (m_session == nullptr || TerminalDrops::localPathsFromDrop(*event->mimeData()).isEmpty()) {
        event->ignore();
        return;
    }

    event->acceptProposedAction();
}

void TerminalWidget::dragMoveEvent(QDragMoveEvent* event) {
    if (m_session == nullptr || TerminalDrops::localPathsFromDrop(*event->mimeData()).isEmpty()) {
        event->ignore();
        return;
    }

    event->acceptProposedAction();
}

void TerminalWidget::dropEvent(QDropEvent* event) {
    if (m_session == nullptr) {
        event->ignore();
        return;
    }

    const QStringList paths = TerminalDrops::localPathsFromDrop(*event->mimeData());

    if (paths.isEmpty()) {
        event->ignore();
        return;
    }

    m_session->scrollToBottom();
    const auto result = m_session->writeLocalPaths(paths);

    if (!result.hasValue()) {
        emit interactionError(result.error());
        event->ignore();
        return;
    }

    setFocus(Qt::MouseFocusReason);
    event->acceptProposedAction();
}

void TerminalWidget::keyPressEvent(QKeyEvent* event) {
    if (m_session == nullptr) {
        event->ignore();
        return;
    }

    // Copying answers only while something is selected, so the combination the shell interrupts with still reaches it.
    if (terminalcore::TerminalShortcuts::isCopy(*event) && hasSelection()) {
        copySelection();
        event->accept();
        return;
    }

    if (terminalcore::TerminalShortcuts::isPaste(*event)) {
        pasteClipboard();
        event->accept();
        return;
    }

    if (terminalcore::TerminalShortcuts::isFind(*event)) {
        showFindBar();
        event->accept();
        return;
    }

    if (terminalcore::TerminalShortcuts::isFindNext(*event) || terminalcore::TerminalShortcuts::isFindPrevious(*event)) {
        stepMatch(terminalcore::TerminalShortcuts::isFindNext(*event));
        event->accept();
        return;
    }

    if (terminalcore::TerminalShortcuts::isSelectAll(*event)) {
        selectAll();
        event->accept();
        return;
    }

    if (terminalcore::TerminalShortcuts::isClearBuffer(*event)) {
        clearScrollback();
        event->accept();
        return;
    }

    if (event->modifiers() == Qt::ShiftModifier) {
        if (event->key() == Qt::Key_PageUp) {
            m_session->scrollViewport(-viewportRows());
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_PageDown) {
            m_session->scrollViewport(viewportRows());
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Home) {
            m_session->scrollToTop();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_End) {
            m_session->scrollToBottom();
            event->accept();
            return;
        }
    }

    // A combination the terminal does not own belongs to the application, so it is never written to the shell as if the shell had been typed at.
    if (!terminalcore::TerminalShortcuts::isTerminalOwned(*event) && terminalcore::TerminalShortcuts::isReservedForApplication(*event)) {
        event->ignore();
        return;
    }

    m_session->scrollToBottom();
    const auto result = m_session->sendKey(*event);

    if (!result.hasValue()) {
        emit interactionError(result.error());
    }

    event->accept();
}

void TerminalWidget::pasteClipboard() {
    const QString text = QApplication::clipboard()->text();
    const QByteArray payload = text.toUtf8();
    const int lineCount = static_cast<int>(text.count(QLatin1Char('\n'))) + (text.endsWith(QLatin1Char('\n')) ? 0 : 1);

    if (m_confirmMultilinePaste && m_session->pasteExecutesOnArrival(payload)) {
        const bool confirmed = m_host.confirm(this, m_host.translate(QStringLiteral("terminal.paste.confirm-title")), m_host.translate(QStringLiteral("terminal.paste.confirm-message")).arg(lineCount), m_host.translate(QStringLiteral("terminal.paste.confirm-detail")), m_host.translate(QStringLiteral("terminal.paste.confirm-action")), false);
        if (!confirmed) {
            return;
        }
    }

    m_session->scrollToBottom();
    const auto result = m_session->paste(payload);

    if (!result.hasValue()) {
        emit interactionError(result.error());
    }
}

QStringList TerminalDrops::localPathsFromDrop(const QMimeData& mimeData) {
    if (!mimeData.hasUrls()) {
        return {};
    }

    QStringList paths;
    paths.reserve(mimeData.urls().size());

    for (const auto& url : mimeData.urls()) {
        if (!url.isLocalFile()) {
            return {};
        }

        const QString localPath = url.toLocalFile();
        const QFileInfo fileInfo(localPath);
        if (localPath.contains(QChar::Null) || localPath.contains(QLatin1Char('\n')) || localPath.contains(QLatin1Char('\r')) || !fileInfo.isAbsolute()) {
            return {};
        }

        paths.append(fileInfo.absoluteFilePath());
    }

    return paths;
}

// Text still being composed belongs on screen where it is being typed, because a writer who cannot see it is typing blind.
void TerminalWidget::inputMethodEvent(QInputMethodEvent* event) {
    if (m_session == nullptr) {
        event->ignore();
        return;
    }

    m_preedit = event->preeditString();

    if (!event->commitString().isEmpty()) {
        m_session->scrollToBottom();
        const auto result = m_session->write(event->commitString().toUtf8());
        if (!result.hasValue()) {
            emit interactionError(result.error());
        }
    }

    update();
    event->accept();
}

QVariant TerminalWidget::inputMethodQuery(Qt::InputMethodQuery query) const {
    if (query == Qt::ImEnabled) {
        return true;
    }

    if (query == Qt::ImCursorRectangle) {
        const QMutexLocker locker(&m_snapshotMutex);
        return QRectF(m_host.theme().metric(ThemeMetric::TerminalHorizontalPadding) + static_cast<qreal>(m_snapshot.cursorPosition.x() * m_cellWidth), m_host.theme().metric(ThemeMetric::TerminalVerticalPadding) + static_cast<qreal>(m_snapshot.cursorPosition.y() * m_cellHeight), m_cellWidth, m_cellHeight);
    }

    return QWidget::inputMethodQuery(query);
}

int TerminalWidget::viewportRows() const {
    const QMutexLocker locker(&m_snapshotMutex);
    return m_snapshot.rows;
}

bool TerminalWidget::hasSelection() const {
    return m_session != nullptr && m_session->hasSelection();
}

QString TerminalWidget::selectedText() const {
    return m_session == nullptr ? QString() : m_session->selectionText();
}

void TerminalWidget::copySelection() {
    const QString text = selectedText();

    if (text.isEmpty()) {
        return;
    }

    QGuiApplication::clipboard()->setText(text);
}

void TerminalWidget::clearSelection() {
    if (m_session == nullptr) {
        return;
    }

    m_autoscrollTimer.stop();
    m_session->clearSelection();
}

void TerminalWidget::selectAll() {
    if (m_session == nullptr) {
        return;
    }

    m_session->selectAll();
}

// The reader looks for something the terminal already printed, and the bar floats over the content so the shell is never resized to hold it.
void TerminalWidget::showFindBar() {
    layoutFindBar();
    m_findBar->raise();
    m_findBar->activate(selectedText());
}

void TerminalWidget::refreshMatches() {
    if (m_session == nullptr) {
        return;
    }

    const QString query = m_findBar->query();
    m_matches = m_session->search(query, m_findBar->caseSensitive(), m_findBar->wholeWord(), searchMatchMaximum);
    m_currentMatch = m_matches.isEmpty() ? -1 : 0;

    if (m_currentMatch >= 0) {
        m_session->revealMatch(m_matches.at(m_currentMatch));
    }

    m_findBar->reportMatches(m_currentMatch + 1, static_cast<int>(m_matches.size()), m_matches.size() == searchMatchMaximum);
    update();
}

// The next match after the last one is the first again, because a reader looking through history expects to come back around.
void TerminalWidget::stepMatch(bool forward) {
    if (m_session == nullptr || m_matches.isEmpty()) {
        return;
    }

    const int count = static_cast<int>(m_matches.size());
    m_currentMatch = ((m_currentMatch + (forward ? 1 : -1)) % count + count) % count;
    m_session->revealMatch(m_matches.at(m_currentMatch));
    m_findBar->reportMatches(m_currentMatch + 1, count, m_matches.size() == searchMatchMaximum);
    update();
}

void TerminalWidget::dismissFindBar() {
    m_findBar->hide();
    m_matches.clear();
    m_currentMatch = -1;
    clearSelection();
    setFocus(Qt::OtherFocusReason);
    update();
}

void TerminalWidget::layoutFindBar() {
    const int barHeight = m_findBar->sizeHint().height();
    const int reserved = m_host.theme().metric(ThemeMetric::TerminalHorizontalPadding);
    m_findBar->setGeometry(0, height() - barHeight, width() - reserved, barHeight);
}

void TerminalWidget::clearScrollback() {
    if (m_session == nullptr) {
        return;
    }

    m_session->clearScrollback();
}

// A right click offers what the terminal can do with the pointer, unless the program under it asked for the mouse itself.
void TerminalWidget::contextMenuEvent(QContextMenuEvent* event) {
    if (m_session == nullptr || mouseBelongsToProgram(Qt::NoModifier)) {
        event->ignore();
        return;
    }

    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    QAction* copy = menu->addAction(m_host.translate(QStringLiteral("terminal.menu.copy")));
    copy->setEnabled(hasSelection());
    QAction* paste = menu->addAction(m_host.translate(QStringLiteral("terminal.menu.paste")));
    paste->setEnabled(!QGuiApplication::clipboard()->text().isEmpty());
    menu->addSeparator();
    QAction* selectEverything = menu->addAction(m_host.translate(QStringLiteral("terminal.menu.select-all")));
    QAction* clear = menu->addAction(m_host.translate(QStringLiteral("terminal.menu.clear")));

    // clang-format off
    connect(copy, &QAction::triggered, this, [this]() { copySelection(); });
    connect(paste, &QAction::triggered, this, [this]() { pasteClipboard(); });
    connect(selectEverything, &QAction::triggered, this, [this]() { selectAll(); });
    connect(clear, &QAction::triggered, this, [this]() { clearScrollback(); });
    // clang-format on

    menu->popup(event->globalPos());
    event->accept();
}

bool TerminalWidget::mouseBelongsToProgram(Qt::KeyboardModifiers modifiers) const {
    if (m_session == nullptr || modifiers.testFlag(Qt::ShiftModifier)) {
        return false;
    }

    return m_session->programWantsMouse();
}

QPointF TerminalWidget::gridPosition(const QPointF& position) const {
    const auto& theme = m_host.theme();
    return {position.x() - theme.metric(ThemeMetric::TerminalHorizontalPadding), position.y() - theme.metric(ThemeMetric::TerminalVerticalPadding)};
}

terminalcore::MouseButton TerminalWidget::mouseButtonOf(Qt::MouseButton button) {
    switch (button) {
    case Qt::LeftButton:
        return terminalcore::MouseButton::Left;
    case Qt::RightButton:
        return terminalcore::MouseButton::Right;
    case Qt::MiddleButton:
        return terminalcore::MouseButton::Middle;
    default:
        return terminalcore::MouseButton::None;
    }
}

// A motion is reported as the drag of the button that is holding it, and as a bare move while none is.
terminalcore::MouseButton TerminalWidget::draggedButton() const {
    if (m_reportedButtons.testFlag(Qt::LeftButton)) {
        return terminalcore::MouseButton::Left;
    }
    if (m_reportedButtons.testFlag(Qt::MiddleButton)) {
        return terminalcore::MouseButton::Middle;
    }
    if (m_reportedButtons.testFlag(Qt::RightButton)) {
        return terminalcore::MouseButton::Right;
    }

    return terminalcore::MouseButton::None;
}

void TerminalWidget::reportMouse(terminalcore::MouseAction action, terminalcore::MouseButton button, const QPointF& position, Qt::KeyboardModifiers modifiers, Qt::MouseButtons pressedButtons) {
    if (m_session == nullptr) {
        return;
    }

    terminalcore::MouseReport report;
    report.action = action;
    report.button = button;
    report.modifiers = modifiers;
    report.position = gridPosition(position);
    report.anyButtonPressed = pressedButtons != Qt::NoButton;

    const auto result = m_session->sendMouse(report);

    if (!result.hasValue()) {
        emit interactionError(result.error());
    }
}

// A press starts, extends or repeats a gesture the emulator interprets, so a word, a line and a drag through the scrollback are all its own.
void TerminalWidget::startSelection(QMouseEvent& event) {
    const quint64 timeNanoseconds = static_cast<quint64>(event.timestamp()) * 1'000'000;
    const quint64 repeatInterval = static_cast<quint64>(QApplication::doubleClickInterval()) * 1'000'000;
    const auto result = m_session->beginSelection(gridPosition(event.position()), timeNanoseconds, repeatInterval, QApplication::startDragDistance(), event.modifiers().testFlag(Qt::AltModifier));

    if (!result.hasValue()) {
        emit interactionError(result.error());
    }
}

void TerminalWidget::mousePressEvent(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
    emit focused();

    if (event->button() == Qt::LeftButton && event->modifiers().testFlag(terminalcore::applicationModifier)) {
        const QString address = m_session == nullptr ? QString() : m_session->addressAt(gridPosition(event->position()));
        if (!address.isEmpty()) {
            emit linkActivated(address);
            event->accept();
            return;
        }
    }

    const terminalcore::MouseButton button = mouseButtonOf(event->button());

    if (button != terminalcore::MouseButton::None && mouseBelongsToProgram(event->modifiers())) {
        clearSelection();
        m_reportedButtons |= event->button();
        reportMouse(terminalcore::MouseAction::Press, button, event->position(), event->modifiers(), event->buttons());
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_session != nullptr) {
        m_selecting = true;
        startSelection(*event);
    }

    event->accept();
}

// Qt reports the second click of a sequence as its own event, and the gesture counts it as the press it really is.
void TerminalWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || m_session == nullptr || mouseBelongsToProgram(event->modifiers())) {
        event->ignore();
        return;
    }

    m_selecting = true;
    startSelection(*event);
    event->accept();
}

void TerminalWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_selecting && m_session != nullptr) {
        m_autoscrollPosition = event->position();
        m_autoscrollRectangle = event->modifiers().testFlag(Qt::AltModifier);
        const auto result = m_session->extendSelection(gridPosition(event->position()), m_autoscrollRectangle);
        if (!result.hasValue()) {
            emit interactionError(result.error());
        }
        updateAutoscroll();
        event->accept();
        return;
    }

    const bool overLink = m_session != nullptr && event->modifiers().testFlag(terminalcore::applicationModifier) && !m_session->addressAt(gridPosition(event->position())).isEmpty();
    setCursor(overLink ? Qt::PointingHandCursor : Qt::IBeamCursor);

    if (!mouseBelongsToProgram(event->modifiers())) {
        event->ignore();
        return;
    }

    reportMouse(terminalcore::MouseAction::Motion, draggedButton(), event->position(), event->modifiers(), event->buttons());
    event->accept();
}

void TerminalWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (m_selecting && m_session != nullptr) {
        m_autoscrollTimer.stop();
        m_session->endSelection(gridPosition(event->position()));
    }

    m_selecting = false;

    if (m_reportedButtons.testFlag(event->button())) {
        m_reportedButtons &= ~event->button();
        reportMouse(terminalcore::MouseAction::Release, mouseButtonOf(event->button()), event->position(), event->modifiers(), event->buttons());
    }

    event->accept();
}

// A drag held outside the grid keeps moving the viewport under it, which is what selects more than the rows on screen.
void TerminalWidget::updateAutoscroll() {
    if (m_session == nullptr || m_session->selectionAutoscroll() == terminalcore::SelectionAutoscroll::None) {
        m_autoscrollTimer.stop();
        return;
    }

    if (!m_autoscrollTimer.isActive()) {
        m_autoscrollTimer.start();
    }
}

void TerminalWidget::advanceAutoscroll() {
    if (m_session == nullptr || !m_selecting) {
        m_autoscrollTimer.stop();
        return;
    }

    const auto result = m_session->advanceSelectionAutoscroll(gridPosition(m_autoscrollPosition), m_autoscrollRectangle);

    if (!result.hasValue()) {
        emit interactionError(result.error());
    }

    updateAutoscroll();
}

void TerminalWidget::wheelEvent(QWheelEvent* event) {
    if (m_session == nullptr) {
        event->ignore();
        return;
    }

    const bool byPixel = !event->pixelDelta().isNull();
    int rows = 0;
    int columns = 0;

    if (byPixel) {
        rows = TerminalWidgetHelper::cellsScrolled(event->pixelDelta().y(), m_cellHeight, m_wheelPixelRemainderY);
        columns = TerminalWidgetHelper::cellsScrolled(event->pixelDelta().x(), m_cellWidth, m_wheelPixelRemainderX);
    } else if (!event->angleDelta().isNull()) {
        rows = TerminalWidgetHelper::cellsNotched(event->angleDelta().y(), m_wheelAngleRemainderY);
        columns = TerminalWidgetHelper::cellsNotched(event->angleDelta().x(), m_wheelAngleRemainderX);
    }

    if (rows == 0 && columns == 0) {
        event->accept();
        return;
    }

    if (mouseBelongsToProgram(event->modifiers())) {
        reportWheel(rows, terminalcore::MouseButton::WheelUp, terminalcore::MouseButton::WheelDown, *event);
        reportWheel(columns, terminalcore::MouseButton::WheelLeft, terminalcore::MouseButton::WheelRight, *event);
        event->accept();
        return;
    }

    // The grid is exactly as wide as the terminal, so only the history scrolls when the program is not reading the wheel.
    if (rows != 0) {
        m_session->scrollViewport(-rows);
    }

    event->accept();
}

void TerminalWidget::reportWheel(int cells, terminalcore::MouseButton forward, terminalcore::MouseButton backward, const QWheelEvent& event) {
    const terminalcore::MouseButton button = cells > 0 ? forward : backward;

    for (int step = 0; step < std::abs(cells); ++step) {
        reportMouse(terminalcore::MouseAction::Press, button, event.position(), event.modifiers(), event.buttons());
    }
}

void TerminalWidget::refreshSnapshot() {
    if (m_session == nullptr) {
        return;
    }

    terminalcore::TerminalRenderSnapshot snapshot;
    {
        const QMutexLocker locker(&m_snapshotMutex);
        m_snapshot = m_session->snapshot();
        snapshot = m_snapshot;
    }
    updateScrollBar(snapshot);
    update();
}

void TerminalWidget::scheduleSnapshotRefresh() {
    if (isVisible() && !m_renderTimer.isActive()) {
        m_renderTimer.start();
    }
}

void TerminalWidget::scheduleTerminalResize() {
    if (isVisible()) {
        m_resizeTimer.start();
    }
}

void TerminalWidget::updateTerminalSize() {
    if (m_session == nullptr || !isVisible()) {
        return;
    }

    const int contentWidth = width() - m_host.theme().metric(ThemeMetric::TerminalHorizontalPadding) * 2;
    const int contentHeight = height() - m_host.theme().metric(ThemeMetric::TerminalVerticalPadding) * 2;
    const int columns = std::max(1, contentWidth / m_cellWidth);
    const int rows = std::max(1, contentHeight / m_cellHeight);
    const auto result = m_session->resize(columns, rows, m_cellWidth, m_cellHeight);

    if (!result.hasValue()) {
        emit interactionError(result.error());
    }
}

void TerminalWidget::updateCellMetrics() {
    const QFontMetricsF fontMetrics(m_font);
    // The cell is the advance rounded to the nearest pixel, because every cell is positioned explicitly and one wider than the advance reads as a gap after every glyph.
    m_cellWidth = std::max(1, static_cast<int>(std::llround(fontMetrics.horizontalAdvance(QLatin1Char('M')))));
    // The leading is what a font answers for the distance between two of its lines.
    m_cellHeight = std::max(1, static_cast<int>(std::llround(fontMetrics.lineSpacing())));
}

void TerminalWidget::updateScrollBar(const terminalcore::TerminalRenderSnapshot& snapshot) {
    const quint64 maximumOffset = snapshot.scrollTotal > snapshot.scrollViewport ? snapshot.scrollTotal - snapshot.scrollViewport : 0;
    const int maximum = static_cast<int>(std::min<quint64>(maximumOffset, INT_MAX));
    const int value = static_cast<int>(std::min<quint64>(snapshot.scrollOffset, static_cast<quint64>(maximum)));
    const int pageStep = static_cast<int>(std::min<quint64>(std::max<quint64>(snapshot.scrollViewport, 1), INT_MAX));

    m_updatingScrollBar = true;
    const QSignalBlocker blocker(m_scrollBar);
    m_scrollBar->setRange(0, maximum);
    m_scrollBar->setPageStep(pageStep);
    m_scrollBar->setValue(value);
    m_scrollBar->setVisible(maximum > 0);
    m_scrollBar->raise();
    m_updatingScrollBar = false;
}

} // namespace workpane::ui
