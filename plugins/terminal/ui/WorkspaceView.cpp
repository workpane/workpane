#include "ui/WorkspaceView.h"

#include "plugins/PluginManager.h"
#include "ui/Icons.h"
#include "ui/SessionDrag.h"
#include "ui/ShelfSessionChip.h"
#include "ui/TerminalPane.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QMimeData>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QVBoxLayout>

#include <utility>

namespace workpane::plugins::terminalplugin {

struct GridPosition final {
    int row{0};
    int column{0};
    int rowSpan{1};
    int columnSpan{1};
};

class WorkspaceViewHelper final {
  public:
    static SessionDragSource* sessionDragSourceFromDrop(const QDropEvent& event);
    static GridPosition positionFor(const QString& presetId, int index, int columns);
};

class SlotFrame final : public QFrame {
  public:
    SlotFrame(int slotIndex, QString assignedSessionId, QWidget* parent = nullptr) : QFrame(parent), m_slotIndex(slotIndex), m_assignedSessionId(std::move(assignedSessionId)) {
        setAcceptDrops(true);
        setObjectName(QStringLiteral("workspaceSlot"));

        m_dropIndicator = new QFrame(this);
        m_dropIndicator->setObjectName(QStringLiteral("slotDropIndicator"));
        m_dropIndicator->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_dropIndicator->hide();
    }

    void registerDropSurface(QWidget& surface) {
        registerDropWidget(surface);

        for (auto* child : surface.findChildren<QWidget*>(QString{}, Qt::FindDirectChildrenOnly)) {
            registerDropWidget(*child);
        }
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (!m_dropSurfaces.contains(qobject_cast<QWidget*>(watched))) {
            return QFrame::eventFilter(watched, event);
        }

        if (event->type() == QEvent::DragEnter) {
            auto* dragEvent = static_cast<QDragEnterEvent*>(event);
            if (WorkspaceViewHelper::sessionDragSourceFromDrop(*dragEvent) == nullptr) {
                return QFrame::eventFilter(watched, event);
            }

            m_internalDragActive = true;
            dragEnterEvent(dragEvent);
            return true;
        }

        if (event->type() == QEvent::DragMove) {
            auto* dragEvent = static_cast<QDragMoveEvent*>(event);
            if (WorkspaceViewHelper::sessionDragSourceFromDrop(*dragEvent) == nullptr) {
                return QFrame::eventFilter(watched, event);
            }

            m_internalDragActive = true;
            dragMoveEvent(dragEvent);
            return true;
        }

        if (event->type() == QEvent::DragLeave) {
            if (!m_internalDragActive) {
                return QFrame::eventFilter(watched, event);
            }

            dragLeaveEvent(static_cast<QDragLeaveEvent*>(event));
            m_internalDragActive = false;
            return true;
        }

        if (event->type() == QEvent::Drop) {
            auto* drop = static_cast<QDropEvent*>(event);
            if (WorkspaceViewHelper::sessionDragSourceFromDrop(*drop) == nullptr) {
                return QFrame::eventFilter(watched, event);
            }

            dropEvent(drop);
            m_internalDragActive = false;
            return true;
        }

        return QFrame::eventFilter(watched, event);
    }

    void dragEnterEvent(QDragEnterEvent* event) override {
        const auto* source = WorkspaceViewHelper::sessionDragSourceFromDrop(*event);

        if (source == nullptr || source->draggedSessionId() == m_assignedSessionId) {
            event->ignore();
            return;
        }

        setDropActive(true);
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }

    void dragMoveEvent(QDragMoveEvent* event) override {
        const auto* source = WorkspaceViewHelper::sessionDragSourceFromDrop(*event);

        if (source == nullptr || source->draggedSessionId() == m_assignedSessionId) {
            setDropActive(false);
            event->ignore();
            return;
        }

        setDropActive(true);
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }

    void dragLeaveEvent(QDragLeaveEvent* event) override {
        setDropActive(false);
        event->accept();
    }

    void dropEvent(QDropEvent* event) override {
        auto* source = WorkspaceViewHelper::sessionDragSourceFromDrop(*event);
        setDropActive(false);

        if (source == nullptr || source->draggedSessionId() == m_assignedSessionId) {
            event->ignore();
            return;
        }

        event->setDropAction(Qt::MoveAction);
        event->accept();
        source->setDropDestination({SessionDropTarget::Slot, m_slotIndex});
    }

    void resizeEvent(QResizeEvent* event) override {
        QFrame::resizeEvent(event);
        m_dropIndicator->setGeometry(rect().adjusted(1, 1, -1, -1));
    }

  private:
    void registerDropWidget(QWidget& widget) {
        widget.setAcceptDrops(true);
        widget.installEventFilter(this);
        m_dropSurfaces.append(&widget);
    }

    void setDropActive(bool active) {
        m_dropIndicator->setVisible(active);

        if (active) {
            m_dropIndicator->raise();
        }
    }

    int m_slotIndex;
    QString m_assignedSessionId;
    QFrame* m_dropIndicator{nullptr};
    QList<QWidget*> m_dropSurfaces;
    bool m_internalDragActive{false};
};
class ShelfFrame final : public QFrame {
  public:
    explicit ShelfFrame(plugins::terminalplugin::workspace::WorkspaceManager& manager, QWidget* parent = nullptr) : QFrame(parent), m_manager(manager) {
        setAcceptDrops(true);
    }

  protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        const auto* source = WorkspaceViewHelper::sessionDragSourceFromDrop(*event);

        if (source == nullptr || m_manager.currentShelf().contains(source->draggedSessionId())) {
            event->ignore();
            return;
        }

        setDropActive(true);
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }

    void dragMoveEvent(QDragMoveEvent* event) override {
        const auto* source = WorkspaceViewHelper::sessionDragSourceFromDrop(*event);

        if (source == nullptr || m_manager.currentShelf().contains(source->draggedSessionId())) {
            setDropActive(false);
            event->ignore();
            return;
        }

        setDropActive(true);
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }

    void dragLeaveEvent(QDragLeaveEvent* event) override {
        setDropActive(false);
        event->accept();
    }

    void dropEvent(QDropEvent* event) override {
        auto* source = WorkspaceViewHelper::sessionDragSourceFromDrop(*event);
        setDropActive(false);

        if (source == nullptr || m_manager.currentShelf().contains(source->draggedSessionId())) {
            event->ignore();
            return;
        }

        event->setDropAction(Qt::MoveAction);
        event->accept();
        source->setDropDestination({SessionDropTarget::Shelf, -1});
    }

  private:
    void setDropActive(bool active) {
        if (property("dropActive").toBool() == active) {
            return;
        }

        setProperty("dropActive", active);
        style()->unpolish(this);
        style()->polish(this);
        update();
    }

    plugins::terminalplugin::workspace::WorkspaceManager& m_manager;
};

SessionDragSource* WorkspaceViewHelper::sessionDragSourceFromDrop(const QDropEvent& event) {
    const QString mimeType = QString::fromLatin1(sessionDragMimeType);
    auto* source = dynamic_cast<SessionDragSource*>(event.source());

    if (source == nullptr || !event.mimeData()->hasFormat(mimeType)) {
        return nullptr;
    }

    const QString sessionId = QString::fromUtf8(event.mimeData()->data(mimeType));

    if (sessionId.isEmpty() || source->draggedSessionId() != sessionId) {
        return nullptr;
    }

    return source;
}

GridPosition WorkspaceViewHelper::positionFor(const QString& presetId, int index, int columns) {
    if (presetId == QStringLiteral("3-left")) {
        return index == 0 ? GridPosition{0, 0, 2, 1} : GridPosition{index - 1, 1};
    }
    if (presetId == QStringLiteral("3-bottom")) {
        return index == 2 ? GridPosition{1, 0, 1, 2} : GridPosition{0, index};
    }

    return {index / columns, index % columns};
}

WorkspaceView::WorkspaceView(plugins::terminalplugin::workspace::WorkspaceManager& manager, plugins::terminalplugin::TerminalSettingsStore& settings, plugins::PluginHost& host, QWidget* parent) : QWidget(parent), m_manager(manager), m_settings(settings), m_host(host) {
    m_rootLayout = new QVBoxLayout(this);
    m_rootLayout->setContentsMargins(0, 0, 0, 0);
    m_rootLayout->setSpacing(0);

    m_shelf = new ShelfFrame(m_manager, this);
    m_shelf->setObjectName(QStringLiteral("sessionShelf"));
    m_shelf->setFixedHeight(44);
    auto* shelfFrameLayout = new QHBoxLayout(m_shelf);
    shelfFrameLayout->setContentsMargins(8, 4, 8, 4);
    shelfFrameLayout->setSpacing(7);

    auto* shelfIcon = new QLabel(m_shelf);
    shelfIcon->setPixmap(ui::IconCatalog::icon(ui::IconName::Shelf, m_host.theme()).pixmap(16, 16));
    shelfIcon->setToolTip(m_host.translate(QStringLiteral("terminal.shelf.outside-active-layout")));
    shelfFrameLayout->addWidget(shelfIcon);

    m_shelfScrollArea = new QScrollArea(m_shelf);
    m_shelfScrollArea->setObjectName(QStringLiteral("sessionShelfScroll"));
    m_shelfScrollArea->setFrameShape(QFrame::NoFrame);
    m_shelfScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_shelfScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_shelfScrollArea->setWidgetResizable(true);
    m_shelfContents = new QWidget(m_shelfScrollArea);
    m_shelfContents->setObjectName(QStringLiteral("sessionShelfContents"));
    m_shelfLayout = new QHBoxLayout(m_shelfContents);
    m_shelfLayout->setContentsMargins(0, 0, 0, 0);
    m_shelfLayout->setSpacing(5);
    m_shelfLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    m_shelfScrollArea->setWidget(m_shelfContents);
    shelfFrameLayout->addWidget(m_shelfScrollArea, 1);

    connect(&m_manager, &plugins::terminalplugin::workspace::WorkspaceManager::layoutChanged, this, &WorkspaceView::synchronize);
    connect(&m_manager, &plugins::terminalplugin::workspace::WorkspaceManager::currentTabChanged, this, &WorkspaceView::synchronize);
    connect(&m_manager, &plugins::terminalplugin::workspace::WorkspaceManager::focusedSessionChanged, this, &WorkspaceView::updateSelection);
    // clang-format off
    connect(&m_settings, &plugins::terminalplugin::TerminalSettingsStore::fontChanged, this, [this]() { applyTerminalFont(); });
    connect(&m_settings, &plugins::terminalplugin::TerminalSettingsStore::fontSizeChanged, this, [this]() { applyTerminalFont(); });
    // clang-format on
    connect(&m_settings, &plugins::terminalplugin::TerminalSettingsStore::confirmMultilinePasteChanged, this, &WorkspaceView::applyPasteConfirmation);
    synchronize();
}

void WorkspaceView::focusTerminal(const QString& sessionId) {
    for (auto* pane : m_panes) {
        if (pane->sessionId() == sessionId) {
            pane->focusTerminal();
            return;
        }
    }
}

void WorkspaceView::focusCurrentTerminal() {
    focusTerminal(m_manager.currentFocusedSessionId());
}

void WorkspaceView::synchronize() {
    const QVariantList slotValues = m_manager.currentSlots();
    const QVariantList shelf = m_manager.currentShelf();
    const QString tabId = m_manager.currentTabId();
    const QString presetId = m_manager.currentPresetId();

    if (slotValues != m_renderedSlots || shelf != m_renderedShelf || tabId != m_renderedTabId || presetId != m_renderedPresetId) {
        m_renderedSlots = slotValues;
        m_renderedShelf = shelf;
        m_renderedTabId = tabId;
        m_renderedPresetId = presetId;
        rebuildWorkspace();
        return;
    }

    updateSelection();
}

void WorkspaceView::updateSelection(const QString& sessionId) {
    const QString selectedId = sessionId.isEmpty() ? m_manager.currentFocusedSessionId() : sessionId;

    for (auto* pane : m_panes) {
        pane->setSelected(pane->sessionId() == selectedId);
    }
}

void WorkspaceView::createTerminalInSlot() {
    const auto* button = qobject_cast<QPushButton*>(sender());

    if (button == nullptr) {
        return;
    }

    const QString sessionId = m_manager.createTerminal(button->property("slotIndex").toInt());

    if (!sessionId.isEmpty()) {
        focusTerminal(sessionId);
    }
}

void WorkspaceView::selectShelfSession(const QString& sessionId) {
    m_manager.activateShelvedSession(sessionId);
    focusTerminal(sessionId);
}

void WorkspaceView::closeShelfSession(const QString& sessionId) {
    emit closeTerminalRequested(sessionId, m_manager.sessionData(sessionId).value("name").toString());
}

void WorkspaceView::selectSession(const QString& sessionId) {
    m_manager.focusSession(sessionId);
}

void WorkspaceView::requestFocusMode(const QString& sessionId) {
    m_focusModeSessionId = m_focusModeSessionId == sessionId ? QString{} : sessionId;
    rebuildWorkspace();
}

void WorkspaceView::assignSessionToSlot(const QString& sessionId, int slotIndex) {
    m_manager.assignToSlot(sessionId, slotIndex);
    focusTerminal(sessionId);
}

void WorkspaceView::moveSessionToShelf(const QString& sessionId) {
    m_manager.moveToShelf(sessionId);
}

void WorkspaceView::applyTerminalFont() {
    for (auto* pane : m_panes) {
        pane->setTerminalFont(m_settings.fontFamily(), m_settings.fontSize());
    }
}

void WorkspaceView::applyPasteConfirmation(bool enabled) {
    for (auto* pane : m_panes) {
        pane->setConfirmMultilinePaste(enabled);
    }
}

void WorkspaceView::rebuildWorkspace() {
    retireWorkspaceHosts();
    m_rootLayout->removeWidget(m_shelf);

    if (!m_focusModeSessionId.isEmpty() && !m_renderedSlots.contains(m_focusModeSessionId)) {
        m_focusModeSessionId.clear();
    }

    if (!m_focusModeSessionId.isEmpty()) {
        m_focusHost = new QWidget(this);
        auto* focusLayout = new QVBoxLayout(m_focusHost);
        focusLayout->setContentsMargins(0, 0, 0, 0);
        // A slot whose session is gone shows what an empty slot shows, because there is nothing to attach a terminal to.
        auto* pane = createTerminalPane(m_focusModeSessionId);
        if (pane != nullptr) {
            pane->setFocusMode(true);
        }
        focusLayout->addWidget(pane != nullptr ? static_cast<QWidget*>(pane) : createEmptySlot(0));
        m_rootLayout->insertWidget(0, m_focusHost, 1);
    } else {
        m_gridHost = new QWidget(this);
        m_gridHost->setObjectName(QStringLiteral("terminalGrid"));
        auto* grid = new QGridLayout(m_gridHost);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(1);
        const int columns = m_manager.currentLayoutColumns();
        const int slotCount = static_cast<int>(m_renderedSlots.size());
        const int rows = (slotCount + columns - 1) / columns;

        for (int column = 0; column < columns; ++column) {
            grid->setColumnStretch(column, 1);
        }
        for (int row = 0; row < rows; ++row) {
            grid->setRowStretch(row, 1);
        }

        for (int index = 0; index < m_renderedSlots.size(); ++index) {
            const QString sessionId = m_renderedSlots.at(index).toString();
            auto* slot = new SlotFrame(index, sessionId, m_gridHost);
            auto* slotLayout = new QVBoxLayout(slot);
            slotLayout->setContentsMargins(0, 0, 0, 0);
            slotLayout->setSpacing(0);
            QWidget* content = sessionId.isEmpty() ? createEmptySlot(index) : static_cast<QWidget*>(createTerminalPane(sessionId));
            content = content == nullptr ? createEmptySlot(index) : content;
            slotLayout->addWidget(content);
            slot->registerDropSurface(*content);

            const auto position = WorkspaceViewHelper::positionFor(m_renderedPresetId, index, columns);
            grid->addWidget(slot, position.row, position.column, position.rowSpan, position.columnSpan);
        }
        m_rootLayout->insertWidget(0, m_gridHost, 1);
    }

    rebuildShelf();
    m_rootLayout->addWidget(m_shelf);
    updateSelection();
}

void WorkspaceView::retireWorkspaceHosts() {
    for (auto* pane : m_panes) {
        pane->deactivate();
    }

    m_panes.clear();

    if (m_gridHost != nullptr) {
        m_gridHost->hide();
        m_rootLayout->removeWidget(m_gridHost);
        m_gridHost->deleteLater();
        m_gridHost = nullptr;
    }

    if (m_focusHost != nullptr) {
        m_focusHost->hide();
        m_rootLayout->removeWidget(m_focusHost);
        m_focusHost->deleteLater();
        m_focusHost = nullptr;
    }
}

void WorkspaceView::rebuildShelf() {
    while (QLayoutItem* item = m_shelfLayout->takeAt(0)) {
        if (item->widget() != nullptr) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    m_shelf->setVisible(!m_renderedShelf.isEmpty());

    if (m_renderedShelf.isEmpty()) {
        return;
    }

    for (const auto& value : m_renderedShelf) {
        const QString sessionId = value.toString();
        const QVariantMap details = m_manager.sessionData(sessionId);
        auto* chip = new ShelfSessionChip(sessionId, details.value("name").toString(), m_host, m_shelf);
        connect(chip, &ShelfSessionChip::activated, this, &WorkspaceView::selectShelfSession);
        connect(chip, &ShelfSessionChip::closeRequested, this, &WorkspaceView::closeShelfSession);
        connect(chip, &ShelfSessionChip::slotDropRequested, this, &WorkspaceView::assignSessionToSlot);
        m_shelfLayout->addWidget(chip);
    }

    m_shelfLayout->addStretch(1);
    m_shelfContents->adjustSize();
}

QWidget* WorkspaceView::createEmptySlot(int slotIndex) {
    auto* empty = new QWidget(m_gridHost);
    empty->setObjectName(QStringLiteral("emptySlot"));
    auto* layout = new QVBoxLayout(empty);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(5);
    auto* button = new QPushButton(ui::IconCatalog::icon(ui::IconName::Add, m_host.theme()), m_host.translate(QStringLiteral("terminal.actions.new-terminal")), empty);
    button->setProperty("slotIndex", slotIndex);
    button->setToolTip(m_host.translate(QStringLiteral("terminal.slot.create")));
    auto* hint = new QLabel(m_host.translate(QStringLiteral("terminal.slot.drop-session")), empty);
    hint->setObjectName(QStringLiteral("mutedLabel"));
    hint->setAlignment(Qt::AlignCenter);
    layout->addWidget(button, 0, Qt::AlignCenter);
    layout->addWidget(hint, 0, Qt::AlignCenter);
    connect(button, &QPushButton::clicked, this, &WorkspaceView::createTerminalInSlot);
    return empty;
}

TerminalPane* WorkspaceView::createTerminalPane(const QString& sessionId) {
    auto* session = qobject_cast<terminalcore::TerminalSession*>(m_manager.sessionObject(sessionId));

    if (session == nullptr) {
        return nullptr;
    }

    auto* pane = new TerminalPane(*session, m_host, m_gridHost != nullptr ? m_gridHost : m_focusHost);
    pane->setTerminalFont(m_settings.fontFamily(), m_settings.fontSize());
    pane->setConfirmMultilinePaste(m_settings.confirmMultilinePaste());
    connect(pane, &TerminalPane::selected, this, &WorkspaceView::selectSession);
    connect(pane, &TerminalPane::closeRequested, this, &WorkspaceView::closeTerminalRequested);
    connect(pane, &TerminalPane::focusModeRequested, this, &WorkspaceView::requestFocusMode);
    connect(pane, &TerminalPane::shelfRequested, this, &WorkspaceView::moveSessionToShelf);
    connect(pane, &TerminalPane::slotDropRequested, this, &WorkspaceView::assignSessionToSlot);
    connect(pane, &TerminalPane::interactionError, this, &WorkspaceView::interactionError);
    m_panes.append(pane);
    return pane;
}

} // namespace workpane::plugins::terminalplugin
