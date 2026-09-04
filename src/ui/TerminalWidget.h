#pragma once

#include "plugins/PluginInterface.h"
#include "terminal/TerminalSession.h"
#include "ui/FindBar.h"

#include <QFont>
#include <QMetaObject>
#include <QMutex>
#include <QPointF>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QWidget>

class QPainter;
class QScrollBar;
class QContextMenuEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QHideEvent;
class QMimeData;
class QShowEvent;
class QWheelEvent;

namespace workpane::ui {

class TerminalWidget final : public QWidget {
    Q_OBJECT

  public:
    explicit TerminalWidget(plugins::PluginHost& host, QWidget* parent = nullptr);

    void setSession(terminalcore::TerminalSession* newSession);
    void setTerminalFont(const QString& family, qreal pointSize);
    void setConfirmMultilinePaste(bool enabled);
    [[nodiscard]] QSize minimumSizeHint() const override;
    [[nodiscard]] bool hasSelection() const;
    [[nodiscard]] QString selectedText() const;
    void copySelection();
    void clearSelection();
    void selectAll();
    void clearScrollback();

  signals:
    void focused();
    void interactionError(const Error& error);
    void linkActivated(const QString& address);

  protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    [[nodiscard]] QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

  private slots:
    void scheduleSnapshotRefresh();
    void refreshSnapshot();

  private:
    void scheduleTerminalResize();
    void updateTerminalSize();
    void updateCellMetrics();
    void updateScrollBar(const terminalcore::TerminalRenderSnapshot& snapshot);
    void pasteClipboard();
    // A program that asked for the mouse receives what the user did with it, unless the shift modifier claims that gesture for the selection.
    [[nodiscard]] bool mouseBelongsToProgram(Qt::KeyboardModifiers modifiers) const;
    [[nodiscard]] QPointF gridPosition(const QPointF& position) const;
    void reportMouse(terminalcore::MouseAction action, terminalcore::MouseButton button, const QPointF& position, Qt::KeyboardModifiers modifiers, Qt::MouseButtons pressedButtons);
    void reportWheel(int cells, terminalcore::MouseButton forward, terminalcore::MouseButton backward, const QWheelEvent& event);
    void paintCursor(QPainter& painter, const terminalcore::TerminalRenderSnapshot& renderSnapshot);
    void paintMatches(QPainter& painter, const terminalcore::TerminalRenderSnapshot& renderSnapshot);
    void paintPreedit(QPainter& painter, const terminalcore::TerminalRenderSnapshot& renderSnapshot);
    void reportFocus(bool gained);
    void showFindBar();
    void startSelection(QMouseEvent& event);
    void refreshMatches();
    void stepMatch(bool forward);
    void dismissFindBar();
    void layoutFindBar();
    void updateAutoscroll();
    void advanceAutoscroll();
    [[nodiscard]] terminalcore::MouseButton draggedButton() const;
    [[nodiscard]] static terminalcore::MouseButton mouseButtonOf(Qt::MouseButton button);
    [[nodiscard]] int viewportRows() const;

    QPointer<terminalcore::TerminalSession> m_session;
    plugins::PluginHost& m_host;
    QMetaObject::Connection m_renderConnection;
    QMetaObject::Connection m_clipboardConnection;
    QFont m_font;
    qreal m_fontSize{13.0};
    bool m_confirmMultilinePaste{true};
    int m_cellWidth{8};
    int m_cellHeight{17};
    int m_wheelPixelRemainderX{};
    int m_wheelPixelRemainderY{};
    int m_wheelAngleRemainderX{};
    int m_wheelAngleRemainderY{};
    bool m_updatingScrollBar{false};
    QScrollBar* m_scrollBar{nullptr};
    QTimer m_resizeTimer;
    QTimer m_renderTimer;
    bool m_selecting{false};
    QPointF m_autoscrollPosition;
    bool m_autoscrollRectangle{false};
    QTimer m_autoscrollTimer;
    QTimer m_cursorTimer;
    FindBar* m_findBar{nullptr};
    QList<terminalcore::SearchMatch> m_matches;
    int m_currentMatch{-1};
    bool m_cursorOn{true};
    QString m_preedit;
    Qt::MouseButtons m_reportedButtons{Qt::NoButton};
    mutable QMutex m_snapshotMutex;
    terminalcore::TerminalRenderSnapshot m_snapshot;
};

// A path a drop delivers is written to the shell, so a drop carrying anything the shell would act on delivers nothing at all.
class TerminalDrops final {
  public:
    [[nodiscard]] static QStringList localPathsFromDrop(const QMimeData& mimeData);
};

} // namespace workpane::ui
