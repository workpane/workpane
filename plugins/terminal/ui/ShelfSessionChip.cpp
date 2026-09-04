#include "ui/ShelfSessionChip.h"

#include "ui/Icons.h"
#include "ui/SessionDrag.h"

#include <QApplication>
#include <QDrag>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QToolButton>

#include <utility>

namespace workpane::plugins::terminalplugin {

ShelfSessionChip::ShelfSessionChip(QString sessionId, const QString& name, plugins::PluginHost& host, QWidget* parent) : QFrame(parent), m_sessionId(std::move(sessionId)), m_host(host) {
    setObjectName(QStringLiteral("shelfChip"));
    setCursor(Qt::OpenHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    setAccessibleName(m_host.translate(QStringLiteral("terminal.shelf.accessible-name")).arg(name));
    setToolTip(m_host.translate(QStringLiteral("terminal.shelf.instructions")));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(7, 0, 3, 0);
    layout->setSpacing(5);

    auto* terminalIcon = new QLabel(this);
    terminalIcon->setPixmap(ui::IconCatalog::icon(ui::IconName::Terminal, m_host.theme()).pixmap(16, 16));
    terminalIcon->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto* nameLabel = new QLabel(name, this);
    nameLabel->setObjectName(QStringLiteral("shelfSessionName"));
    nameLabel->setMaximumWidth(125);
    nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto* closeButton = new QToolButton(this);
    closeButton->setAutoRaise(true);
    closeButton->setIcon(ui::IconCatalog::icon(ui::IconName::Close, m_host.theme()));
    closeButton->setFixedSize(22, 22);
    closeButton->setCursor(Qt::ArrowCursor);
    closeButton->setToolTip(m_host.translate(QStringLiteral("terminal.actions.close-terminal")));

    layout->addWidget(terminalIcon);
    layout->addWidget(nameLabel);
    layout->addWidget(closeButton);

    connect(closeButton, &QToolButton::clicked, this, &ShelfSessionChip::requestClose);
}

QString ShelfSessionChip::draggedSessionId() const {
    return m_sessionId;
}

void ShelfSessionChip::setDropDestination(SessionDropDestination destination) {
    m_dropDestination = destination;
}

void ShelfSessionChip::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space) {
        emit activated(m_sessionId);
        event->accept();
        return;
    }

    QFrame::keyPressEvent(event);
}

void ShelfSessionChip::mousePressEvent(QMouseEvent* event) {
    m_dragStarted = false;
    m_leftButtonPressed = event->button() == Qt::LeftButton;

    if (m_leftButtonPressed) {
        m_dragOrigin = event->position().toPoint();
        setCursor(Qt::ClosedHandCursor);
        setFocus(Qt::MouseFocusReason);
    }

    QFrame::mousePressEvent(event);
}

void ShelfSessionChip::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragStarted) {
        event->accept();
        return;
    }

    if (!m_leftButtonPressed || !event->buttons().testFlag(Qt::LeftButton) || (event->position().toPoint() - m_dragOrigin).manhattanLength() < QApplication::startDragDistance()) {
        QFrame::mouseMoveEvent(event);
        return;
    }

    m_dragStarted = true;
    beginDrag();
    m_leftButtonPressed = false;
    setCursor(Qt::OpenHandCursor);
}

void ShelfSessionChip::mouseReleaseEvent(QMouseEvent* event) {
    const bool activate = m_leftButtonPressed && !m_dragStarted && event->button() == Qt::LeftButton && rect().contains(event->position().toPoint());
    m_leftButtonPressed = false;
    m_dragStarted = false;
    setCursor(Qt::OpenHandCursor);

    if (activate) {
        emit activated(m_sessionId);
        event->accept();
        return;
    }

    QFrame::mouseReleaseEvent(event);
}

void ShelfSessionChip::requestClose() {
    emit closeRequested(m_sessionId);
}

void ShelfSessionChip::beginDrag() {
    m_dropDestination = {};

    Qt::DropAction result = Qt::IgnoreAction;
    {
        QDrag drag(this);
        auto* mimeData = new QMimeData();
        mimeData->setData(QString::fromLatin1(sessionDragMimeType), m_sessionId.toUtf8());
        drag.setMimeData(mimeData);
        drag.setPixmap(grab());
        drag.setHotSpot(m_dragOrigin);
        result = drag.exec(Qt::MoveAction);
    }

    const SessionDropDestination destination = m_dropDestination;
    m_dropDestination = {};

    if (result == Qt::MoveAction && destination.target == SessionDropTarget::Slot) {
        emit slotDropRequested(m_sessionId, destination.slotIndex);
    }
}

} // namespace workpane::plugins::terminalplugin
