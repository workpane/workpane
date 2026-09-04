#include "ui/TerminalPane.h"

#include "ui/Components.h"
#include "ui/Icons.h"
#include "ui/SessionDrag.h"
#include "ui/TerminalWidget.h"
#include "ui/Theme.h"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDrag>
#include <QEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace workpane::plugins::terminalplugin {

class TerminalPaneHelper final {
  public:
    static QString displayPath(const QString& path);
};

QString TerminalPaneHelper::displayPath(const QString& path) {
    const QString home = QDir::homePath();

    if (path == home) {
        return QStringLiteral("~");
    }
    if (path.startsWith(home + QLatin1Char('/'))) {
        return QStringLiteral("~") + path.sliced(home.size());
    }

    return path;
}

TerminalPane::TerminalPane(terminalcore::TerminalSession& session, plugins::PluginHost& host, QWidget* parent) : QFrame(parent), m_session(&session), m_host(host), m_sessionId(session.id()) {
    setObjectName(QStringLiteral("terminalPane"));
    setFrameShape(QFrame::NoFrame);
    setProperty("selected", false);

    auto* rootLayout = new QVBoxLayout(this);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_header = new QWidget(this);
    m_header->setObjectName(QStringLiteral("terminalHeader"));
    m_header->setProperty("active", false);
    m_header->setFixedHeight(m_host.theme().metric(ui::ThemeMetric::TerminalHeaderHeight));
    m_header->setCursor(Qt::OpenHandCursor);
    m_header->installEventFilter(this);
    auto* headerLayout = new QHBoxLayout(m_header);
    headerLayout->setContentsMargins(7, 0, 3, 0);
    headerLayout->setSpacing(6);

    m_focusIndicator = new ui::StatusIndicator(m_header);
    m_focusIndicator->setObjectName(QStringLiteral("terminalFocusIndicator"));
    m_focusIndicator->setColor(m_host.theme().color(ui::ThemeColor::TextMuted));
    m_nameLabel = new QLabel(m_header);
    m_nameLabel->setObjectName(QStringLiteral("terminalTitle"));
    QFont nameFont = m_nameLabel->font();
    nameFont.setPointSizeF(std::max(9.0, nameFont.pointSizeF() - 1.0));
    nameFont.setWeight(QFont::DemiBold);
    m_nameLabel->setFont(nameFont);
    QPalette namePalette = m_nameLabel->palette();
    namePalette.setColor(QPalette::WindowText, m_host.theme().color(ui::ThemeColor::TextMuted));
    m_nameLabel->setPalette(namePalette);
    m_nameLabel->setMinimumWidth(40);
    m_nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_nameLabel->installEventFilter(this);
    QFont detailFont = nameFont;
    detailFont.setWeight(QFont::Normal);
    m_shellLabel = new QLabel(m_header);
    m_shellLabel->setObjectName(QStringLiteral("mutedLabel"));
    m_shellLabel->setFont(detailFont);

    m_focusButton = ui::Components::toolButton(ui::IconName::Focus, m_host.theme(), m_host.translate(QStringLiteral("terminal.actions.focus")), m_header);
    m_actionsButton = ui::Components::toolButton(ui::IconName::More, m_host.theme(), m_host.translate(QStringLiteral("terminal.actions.menu")), m_header);
    auto* closeButton = ui::Components::toolButton(ui::IconName::Close, m_host.theme(), m_host.translate(QStringLiteral("terminal.actions.close-terminal")), m_header);

    headerLayout->addWidget(m_focusIndicator);
    headerLayout->addWidget(m_nameLabel, 1);
    m_bellIndicator = new QLabel(m_header);
    m_bellIndicator->setObjectName(QStringLiteral("terminalBellIndicator"));
    m_bellIndicator->setPixmap(ui::IconCatalog::icon(ui::IconName::Bell, m_host.theme().color(ui::ThemeColor::Warning)).pixmap(m_host.theme().metric(ui::ThemeMetric::SmallIconSize)));
    m_bellIndicator->setToolTip(m_host.translate(QStringLiteral("terminal.session.bell")));
    m_bellIndicator->hide();

    headerLayout->addWidget(m_shellLabel);
    headerLayout->addWidget(m_bellIndicator);
    headerLayout->addWidget(m_focusButton);
    headerLayout->addWidget(m_actionsButton);
    headerLayout->addWidget(closeButton);
    rootLayout->addWidget(m_header);

    m_terminal = new ui::TerminalWidget(m_host, this);
    m_terminal->setSession(m_session);
    rootLayout->addWidget(m_terminal, 1);

    m_exitBar = new QWidget(this);
    m_exitBar->setObjectName(QStringLiteral("terminalExitBar"));
    auto* exitLayout = new QHBoxLayout(m_exitBar);
    exitLayout->setContentsMargins(9, 4, 6, 4);
    m_exitLabel = new QLabel(m_exitBar);
    auto* restartButton = new QToolButton(m_exitBar);
    restartButton->setObjectName(QStringLiteral("inlineActionButton"));
    restartButton->setText(m_host.translate(QStringLiteral("terminal.actions.restart")));
    restartButton->setIcon(ui::IconCatalog::icon(ui::IconName::Refresh, m_host.theme()));
    restartButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    restartButton->setAutoRaise(true);
    exitLayout->addWidget(m_exitLabel, 1);
    exitLayout->addWidget(restartButton);
    rootLayout->addWidget(m_exitBar);

    connect(m_focusButton, &QToolButton::clicked, this, &TerminalPane::requestFocusMode);
    connect(m_actionsButton, &QToolButton::clicked, this, &TerminalPane::showActionsMenu);
    connect(closeButton, &QToolButton::clicked, this, &TerminalPane::requestClose);
    connect(restartButton, &QToolButton::clicked, m_session, &terminalcore::TerminalSession::restart);
    connect(m_terminal, &ui::TerminalWidget::focused, this, &TerminalPane::selectSession);
    connect(m_terminal, &ui::TerminalWidget::interactionError, this, &TerminalPane::interactionError);
    connect(m_terminal, &ui::TerminalWidget::linkActivated, this, &TerminalPane::openAddress);
    // clang-format off
    connect(m_session, &terminalcore::TerminalSession::bellRang, this, [this]() { m_bellIndicator->setVisible(!m_terminal->hasFocus()); });
    connect(m_terminal, &ui::TerminalWidget::focused, this, [this]() { m_bellIndicator->hide(); });
    connect(m_session, &terminalcore::TerminalSession::notificationPosted, this, [this](const QString& title, const QString& body) { m_host.notify(title.isEmpty() ? m_host.translate(QStringLiteral("terminal.plugin.title")) : title, body, plugins::AlertSeverity::Information); });
    // clang-format on
    connect(m_session, &terminalcore::TerminalSession::nameChanged, this, &TerminalPane::updateSessionDetails);
    connect(m_session, &terminalcore::TerminalSession::cwdChanged, this, &TerminalPane::updateSessionDetails);
    connect(m_session, &terminalcore::TerminalSession::statusChanged, this, &TerminalPane::updateSessionDetails);

    updateSessionDetails();
    updateHeaderVisibility();
}

QString TerminalPane::sessionId() const {
    return m_sessionId;
}

QString TerminalPane::draggedSessionId() const {
    return m_sessionId;
}

void TerminalPane::setDropDestination(SessionDropDestination destination) {
    m_dropDestination = destination;
}

void TerminalPane::setSelected(bool isSelected) {
    if (m_selected == isSelected) {
        return;
    }

    m_selected = isSelected;
    m_header->setProperty("active", m_selected);
    m_focusIndicator->setColor(m_host.theme().color(m_selected ? ui::ThemeColor::Accent : ui::ThemeColor::TextMuted));
    QPalette namePalette = m_nameLabel->palette();
    namePalette.setColor(QPalette::WindowText, m_host.theme().color(m_selected ? ui::ThemeColor::Text : ui::ThemeColor::TextMuted));
    m_nameLabel->setPalette(namePalette);
    m_header->style()->unpolish(m_header);
    m_header->style()->polish(m_header);
    m_header->update();
}

void TerminalPane::setFocusMode(bool focusMode) {
    m_focusButton->setIcon(ui::IconCatalog::icon(focusMode ? ui::IconName::Restore : ui::IconName::Focus, m_host.theme()));
    m_focusButton->setToolTip(m_host.translate(focusMode ? QStringLiteral("terminal.actions.restore-layout") : QStringLiteral("terminal.actions.focus")));
}

void TerminalPane::focusTerminal() {
    m_terminal->setFocus(Qt::OtherFocusReason);
}

void TerminalPane::deactivate() {
    m_terminal->setSession(nullptr);

    if (m_session != nullptr) {
        disconnect(m_session, nullptr, this, nullptr);
        m_session.clear();
    }
}

void TerminalPane::setTerminalFont(const QString& family, int size) {
    m_terminal->setTerminalFont(family, size);
}

void TerminalPane::setConfirmMultilinePaste(bool enabled) {
    m_terminal->setConfirmMultilinePaste(enabled);
}

bool TerminalPane::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_nameLabel && event->type() == QEvent::MouseButtonDblClick) {
        if (m_session == nullptr) {
            return true;
        }

        const QString currentName = m_session->name();
        const QPointer<TerminalPane> pane(this);
        bool accepted = false;
        const QString name = QInputDialog::getText(this, m_host.translate(QStringLiteral("terminal.session.rename-title")), m_host.translate(QStringLiteral("terminal.session.name")), QLineEdit::Normal, currentName, &accepted);
        if (accepted && pane != nullptr && pane->m_session != nullptr) {
            pane->m_session->setName(name);
        }
        return true;
    }

    if (watched != m_header) {
        return QFrame::eventFilter(watched, event);
    }

    if (event->type() == QEvent::MouseButtonPress) {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            m_dragStarted = false;
            m_dragOrigin = mouseEvent->position().toPoint();
            selectSession();
            m_header->setCursor(Qt::ClosedHandCursor);
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        m_dragStarted = false;
        m_header->setCursor(Qt::OpenHandCursor);
    } else if (event->type() == QEvent::MouseMove) {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (!m_dragStarted && mouseEvent->buttons().testFlag(Qt::LeftButton) && (mouseEvent->position().toPoint() - m_dragOrigin).manhattanLength() >= QApplication::startDragDistance()) {
            m_dragStarted = true;
            beginDrag();
            m_header->setCursor(Qt::OpenHandCursor);
        }
    }

    return QFrame::eventFilter(watched, event);
}

void TerminalPane::resizeEvent(QResizeEvent* event) {
    QFrame::resizeEvent(event);
    updateHeaderVisibility();
}

void TerminalPane::selectSession() {
    emit selected(m_sessionId);
}

void TerminalPane::requestClose() {
    if (m_session == nullptr) {
        return;
    }

    const QString name = m_session->name();
    emit closeRequested(m_sessionId, name);
}

void TerminalPane::requestFocusMode() {
    emit focusModeRequested(m_sessionId);
}

void TerminalPane::showActionsMenu() {
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    auto* restartAction = menu->addAction(ui::IconCatalog::icon(ui::IconName::Refresh, m_host.theme()), m_host.translate(QStringLiteral("terminal.actions.restart-shell")));
    restartAction->setIconVisibleInMenu(true);
    restartAction->setData(QStringLiteral("restart"));
    auto* shelfAction = menu->addAction(ui::IconCatalog::icon(ui::IconName::Shelf, m_host.theme()), m_host.translate(QStringLiteral("terminal.actions.move-to-shelf")));
    shelfAction->setIconVisibleInMenu(true);
    shelfAction->setData(QStringLiteral("shelf"));

    // The directory the shell is standing in is a folder like any other, so it is opened where folders are edited.
    if (m_host.capabilityAvailable(QString::fromLatin1(plugins::openFolderCapability)) && m_session != nullptr && !m_session->cwd().isEmpty()) {
        auto* editorAction = menu->addAction(ui::IconCatalog::icon(ui::IconName::Folder, m_host.theme()), m_host.translate(QStringLiteral("terminal.actions.open-in-editor")));
        editorAction->setIconVisibleInMenu(true);
        editorAction->setData(QStringLiteral("editor"));
    }

    if (m_host.capabilityAvailable(QString::fromLatin1(plugins::serveFolderCapability)) && m_session != nullptr && !m_session->cwd().isEmpty()) {
        auto* serverAction = menu->addAction(ui::IconCatalog::icon(ui::IconName::WebServer, m_host.theme()), m_host.translate(QStringLiteral("terminal.actions.serve-directory")));
        serverAction->setIconVisibleInMenu(true);
        serverAction->setData(QStringLiteral("server"));
    }

    auto* closeAction = menu->addAction(ui::IconCatalog::icon(ui::IconName::Close, m_host.theme()), m_host.translate(QStringLiteral("terminal.actions.close-terminal")));
    closeAction->setIconVisibleInMenu(true);
    closeAction->setData(QStringLiteral("close"));
    connect(menu, &QMenu::triggered, this, &TerminalPane::handleAction);
    menu->popup(QCursor::pos());
}

void TerminalPane::handleAction(QAction* action) {
    const QString actionId = action->data().toString();

    if (actionId == QStringLiteral("restart")) {
        if (m_session != nullptr) {
            m_session->restart();
        }
        return;
    }

    if (actionId == QStringLiteral("shelf")) {
        emit shelfRequested(m_sessionId);
        return;
    }

    if (actionId == QStringLiteral("editor")) {
        offerDirectory(QString::fromLatin1(plugins::openFolderCapability));
        return;
    }

    if (actionId == QStringLiteral("server")) {
        offerDirectory(QString::fromLatin1(plugins::serveFolderCapability));
        return;
    }

    if (actionId == QStringLiteral("close")) {
        requestClose();
    }
}

// An address printed by a program opens where the application browses, and falls back to the browser of the system when the plugin is not there.
void TerminalPane::openAddress(const QString& address) {
    const QUrl url(address, QUrl::StrictMode);

    if (!url.isValid()) {
        return;
    }

    if (!m_host.capabilityAvailable(QString::fromLatin1(plugins::openPageCapability))) {
        QDesktopServices::openUrl(url);
        return;
    }

    // clang-format off
    const auto answered = [this](Result<QJsonObject> result) { if (!result.hasValue()) { m_host.notify(m_host.translate(QStringLiteral("terminal.plugin.title")), result.error().message, plugins::AlertSeverity::Error); } };
    // clang-format on
    m_host.invokeCapability(QString::fromLatin1(plugins::openPageCapability), {{QStringLiteral("url"), url.toString()}}, *this, answered);
}

// The plugin that receives the folder decides what to do with it and reveals itself, so the terminal never reaches into another plugin.
void TerminalPane::offerDirectory(const QString& capability) {
    if (m_session == nullptr || m_session->cwd().isEmpty()) {
        return;
    }

    // clang-format off
    const auto answered = [this](Result<QJsonObject> result) { if (!result.hasValue()) { m_host.notify(m_host.translate(QStringLiteral("terminal.plugin.title")), result.error().message, plugins::AlertSeverity::Error); } };
    // clang-format on
    m_host.invokeCapability(capability, {{QStringLiteral("path"), m_session->cwd()}}, *this, answered);
}

void TerminalPane::updateSessionDetails() {
    if (m_session == nullptr) {
        return;
    }

    m_nameLabel->setText(QStringLiteral("%1  %2").arg(m_session->name(), TerminalPaneHelper::displayPath(m_session->cwd())));
    m_nameLabel->setToolTip(QStringLiteral("%1\n%2").arg(m_session->name(), m_session->cwd()));
    m_shellLabel->setText(m_session->name() == m_session->shellName() ? QString{} : m_session->shellName());

    m_exitBar->setVisible(m_session->status() == QStringLiteral("Exited"));
    m_exitLabel->setText(m_host.translate(QStringLiteral("terminal.session.process-exited")).arg(m_session->exitCode()));
}

void TerminalPane::updateHeaderVisibility() {
    m_shellLabel->setVisible(width() >= 480);
    m_focusButton->setVisible(width() >= 365);
    m_actionsButton->setVisible(width() >= 260);
}

void TerminalPane::beginDrag() {
    m_dropDestination = {};

    Qt::DropAction result = Qt::IgnoreAction;
    {
        QDrag drag(this);
        auto* mimeData = new QMimeData();
        mimeData->setData(QString::fromLatin1(sessionDragMimeType), m_sessionId.toUtf8());
        drag.setMimeData(mimeData);
        drag.setPixmap(grab(QRect(0, 0, std::min(width(), 360), m_header->height())));
        drag.setHotSpot(m_dragOrigin);
        result = drag.exec(Qt::MoveAction);
    }

    const SessionDropDestination destination = m_dropDestination;
    m_dropDestination = {};

    if (result != Qt::MoveAction) {
        return;
    }

    if (destination.target == SessionDropTarget::Slot) {
        emit slotDropRequested(m_sessionId, destination.slotIndex);
        return;
    }

    if (destination.target == SessionDropTarget::Shelf) {
        emit shelfRequested(m_sessionId);
    }
}

} // namespace workpane::plugins::terminalplugin
