#include "AiTasksView.h"

#include "AiConversationView.h"
#include "AiPlugin.h"
#include "AiTaskDialog.h"
#include "AiTaskInfoDialog.h"
#include "ui/Components.h"
#include "ui/Icons.h"
#include "ui/TabBar.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QDialog>
#include <QDrag>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <optional>
#include <utility>

namespace workpane::plugins::ai {

const QString taskMimeType = QStringLiteral("application/x-workpane-ai-task");

class AiTasksViewHelper final {
  public:
    static QString columnTitleKey(TaskColumn column);
    static QString scheduleSummary(const AiTask& task, PluginHost& host);
};

QString AiTasksViewHelper::columnTitleKey(TaskColumn column) {
    return QStringLiteral("ai.column.%1").arg(AiTaskRepository::columnName(column));
}

// A schedule that already ran keeps saying when it ran, because the card is where the user remembers it.
QString AiTasksViewHelper::scheduleSummary(const AiTask& task, PluginHost& host) {
    if (!task.schedule.has_value()) {
        return {};
    }
    if (task.schedule->enabled) {
        return host.translate(QStringLiteral("ai.task.scheduled")).arg(ui::Components::localTimestamp(task.schedule->nextRunAtUtc));
    }
    if (task.schedule->lastTriggeredAtUtc.isValid()) {
        return host.translate(QStringLiteral("ai.task.schedule-done")).arg(ui::Components::localTimestamp(task.schedule->lastTriggeredAtUtc));
    }

    return {};
}

class TaskCard final : public QFrame {
  public:
    TaskCard(AiPlugin& plugin, PluginHost& host, AiTask task, QWidget* parent) : QFrame(parent), m_plugin(plugin), m_host(host), m_task(std::move(task)) {
        setObjectName(QStringLiteral("aiTaskCard"));
        setAttribute(Qt::WA_StyledBackground, true);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(10, 9, 10, 9);
        layout->setSpacing(6);

        m_title = new QLabel(this);
        m_title->setObjectName(QStringLiteral("aiTaskTitle"));
        m_title->setWordWrap(true);
        layout->addWidget(m_title);

        m_description = new QLabel(this);
        m_description->setObjectName(QStringLiteral("mutedLabel"));
        m_description->setWordWrap(true);
        layout->addWidget(m_description);

        m_badge = new QLabel(this);
        m_badge->setObjectName(QStringLiteral("aiTaskBadge"));
        m_badge->setAlignment(Qt::AlignCenter);
        m_badge->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        m_phase = new QLabel(this);
        m_phase->setObjectName(QStringLiteral("mutedLabel"));
        m_phase->setWordWrap(true);
        layout->addWidget(m_badge, 0, Qt::AlignLeft);
        layout->addWidget(m_phase);

        m_error = new QLabel(this);
        m_error->setObjectName(QStringLiteral("aiTaskError"));
        m_error->setWordWrap(true);
        m_error->hide();
        layout->addWidget(m_error);

        m_schedules = new QLabel(this);
        m_schedules->setObjectName(QStringLiteral("mutedLabel"));
        layout->addWidget(m_schedules);

        auto* actions = new QHBoxLayout();
        actions->setContentsMargins(0, 0, 0, 0);
        actions->setSpacing(4);
        m_start = ui::Components::toolButton(ui::IconName::Start, m_host.theme(), m_host.translate(QStringLiteral("ai.task.start")), this);
        m_start->setObjectName(QStringLiteral("aiTaskStart"));
        m_stop = ui::Components::toolButton(ui::IconName::Stop, m_host.theme(), m_host.translate(QStringLiteral("ai.task.stop")), this);
        m_stop->setObjectName(QStringLiteral("aiTaskStop"));
        m_edit = ui::Components::toolButton(ui::IconName::Edit, m_host.theme(), m_host.translate(QStringLiteral("ai.task.edit")), this);
        m_edit->setObjectName(QStringLiteral("aiTaskEdit"));
        m_remove = ui::Components::toolButton(ui::IconName::Clear, m_host.theme(), m_host.translate(QStringLiteral("ai.task.remove")), this);
        m_remove->setObjectName(QStringLiteral("aiTaskRemove"));
        m_info = ui::Components::toolButton(ui::IconName::Information, m_host.theme(), m_host.translate(QStringLiteral("ai.task.info")), this);
        m_info->setObjectName(QStringLiteral("aiTaskInfo"));
        // An agent task is a conversation, so the card opens it directly.
        m_chat = ui::Components::toolButton(ui::IconName::Chat, m_host.theme(), m_host.translate(QStringLiteral("ai.task.chat")), this);
        m_chat->setObjectName(QStringLiteral("aiTaskChat"));
        // The schedule is stopped from the card, so the action exists only while the task carries one.
        m_schedule = ui::Components::toolButton(ui::IconName::Schedule, m_host.theme(), m_host.translate(QStringLiteral("ai.task.schedule-remove")), this);
        m_schedule->setObjectName(QStringLiteral("aiTaskSchedule"));
        // The working directory is a folder like any other, so it is opened and served from the card that declares it.
        m_openFolder = ui::Components::toolButton(ui::IconName::Folder, m_host.theme(), m_host.translate(QStringLiteral("ai.task.open-workdir")), this);
        m_openFolder->setObjectName(QStringLiteral("aiTaskOpenWorkdir"));
        m_serveFolder = ui::Components::toolButton(ui::IconName::WebServer, m_host.theme(), m_host.translate(QStringLiteral("ai.task.serve-workdir")), this);
        m_serveFolder->setObjectName(QStringLiteral("aiTaskServeWorkdir"));
        actions->addWidget(m_start);
        actions->addWidget(m_stop);
        actions->addWidget(m_chat);
        actions->addWidget(m_info);
        actions->addWidget(m_schedule);
        actions->addWidget(m_openFolder);
        actions->addWidget(m_serveFolder);
        actions->addWidget(m_edit);
        actions->addStretch();
        actions->addWidget(m_remove);
        layout->addLayout(actions);

        setTask(m_task);
    }

    // A card outlives the state it presents, because destroying it while it is delivering an event is what breaks the application.
    void setTask(AiTask task) {
        m_task = std::move(task);
        m_title->setText(m_task.title);
        m_description->setText(m_task.description);
        m_description->setVisible(!m_task.description.isEmpty());
        const QString schedule = AiTasksViewHelper::scheduleSummary(m_task, m_host);
        m_schedules->setText(schedule);
        m_schedules->setVisible(!schedule.isEmpty());
        m_schedule->setVisible(m_task.schedule.has_value());
        m_openFolder->setVisible(!m_task.workdir.isEmpty());
        m_serveFolder->setVisible(!m_task.workdir.isEmpty());
        m_chat->setVisible(m_task.executionKind == TaskExecutionKind::Agent);
        updateRunState();
    }

    [[nodiscard]] const AiTask& task() const {
        return m_task;
    }

  protected:
    // A task waiting in To Do that never ran is opened to be written, and every other one to be read.
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        QFrame::mouseDoubleClickEvent(event);
        const bool untouched = m_task.column == TaskColumn::Todo && !m_plugin.hasLastExecution(m_task.id) && m_plugin.runState(m_task.id) == TaskRunState::Idle;

        if (untouched) {
            m_edit->click();
            return;
        }

        m_info->click();
    }

  public:
    void updateRunState() {
        const TaskRunState state = m_plugin.runState(m_task.id);
        const bool running = state == TaskRunState::Running;
        const bool waiting = state == TaskRunState::Waiting;
        setProperty("running", running);
        setProperty("waiting", waiting);
        style()->unpolish(this);
        style()->polish(this);
        m_badge->setText(m_host.translate(badgeKey()));
        const ExecutionPhase phase = m_plugin.executionPhase(m_task.id);
        const QString sentence = m_host.translate(QStringLiteral("ai.phase.") + AiPlugin::phaseName(phase));
        const QString detail = m_plugin.executionDetail(m_task.id);
        m_phase->setText(phase == ExecutionPhase::Idle ? QString{} : (sentence.contains(QLatin1String("%1")) ? sentence.arg(detail) : sentence));
        m_phase->setVisible(phase != ExecutionPhase::Idle);
        m_badge->setProperty("badge", badgeName());
        m_badge->style()->unpolish(m_badge);
        m_badge->style()->polish(m_badge);
        // A run that ended without answering did not necessarily fail, so the card says what stopped it beside its status.
        const QString failure = m_plugin.lastError(m_task.id);
        const AgentStopReason stopReason = m_plugin.lastStopReason(m_task.id);
        const bool stopped = state == TaskRunState::Idle && m_plugin.hasLastExecution(m_task.id) && stopReason != AgentStopReason::Answered && stopReason != AgentStopReason::Failed;
        const QString reason = stopped ? m_host.translate(QStringLiteral("ai.stop-reason.") + AiTaskRepository::agentStopReasonName(stopReason)) : QString{};
        const QString note = failure.isEmpty() ? reason : m_host.translate(QStringLiteral("ai.task.last-error")).arg(failure);
        m_error->setText(note);
        m_error->setVisible(!note.isEmpty());
        m_start->setEnabled(state == TaskRunState::Idle);
        m_stop->setEnabled(state != TaskRunState::Idle);
        m_edit->setEnabled(state == TaskRunState::Idle);
        m_remove->setEnabled(state == TaskRunState::Idle);
        m_schedule->setEnabled(state == TaskRunState::Idle);
    }

    [[nodiscard]] bool scheduled() const {
        return m_task.schedule.has_value() && m_task.schedule->enabled && m_task.schedule->nextRunAtUtc.isValid();
    }

    [[nodiscard]] QString badgeName() const {
        const TaskRunState state = m_plugin.runState(m_task.id);

        if (state == TaskRunState::Running) {
            return QStringLiteral("running");
        }
        if (state == TaskRunState::Waiting) {
            return QStringLiteral("queued");
        }
        if (!m_plugin.hasLastExecution(m_task.id)) {
            return scheduled() ? QStringLiteral("scheduled") : QStringLiteral("idle");
        }

        switch (m_plugin.lastExecutionStatus(m_task.id)) {
        case ExecutionStatus::Succeeded:
            return m_plugin.lastStopReason(m_task.id) == AgentStopReason::Answered ? QStringLiteral("succeeded") : QStringLiteral("stopped");
        case ExecutionStatus::Failed:
            return QStringLiteral("failed");
        case ExecutionStatus::Cancelled:
            return QStringLiteral("cancelled");
        case ExecutionStatus::Running:
            return QStringLiteral("running");
        }

        return QStringLiteral("idle");
    }

    [[nodiscard]] QString badgeKey() const {
        return QStringLiteral("ai.badge.") + badgeName();
    }

    [[nodiscard]] QToolButton* chatButton() const {
        return m_chat;
    }

    [[nodiscard]] QToolButton* infoButton() const {
        return m_info;
    }

    [[nodiscard]] QToolButton* startButton() const {
        return m_start;
    }

    [[nodiscard]] QToolButton* stopButton() const {
        return m_stop;
    }

    [[nodiscard]] QToolButton* editButton() const {
        return m_edit;
    }

    [[nodiscard]] QToolButton* scheduleButton() const {
        return m_schedule;
    }

    [[nodiscard]] QToolButton* removeButton() const {
        return m_remove;
    }

    [[nodiscard]] QToolButton* openFolderButton() const {
        return m_openFolder;
    }

    [[nodiscard]] QToolButton* serveFolderButton() const {
        return m_serveFolder;
    }

  protected:
    void mousePressEvent(QMouseEvent* event) override {
        m_pressedAt = event->position().toPoint();
        QFrame::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!event->buttons().testFlag(Qt::LeftButton) || (event->position().toPoint() - m_pressedAt).manhattanLength() < QApplication::startDragDistance()) {
            QFrame::mouseMoveEvent(event);
            return;
        }

        auto* payload = new QMimeData();
        payload->setData(taskMimeType, m_task.id.toUtf8());
        auto* drag = new QDrag(this);
        drag->setMimeData(payload);
        drag->exec(Qt::MoveAction);
    }

  private:
    AiPlugin& m_plugin;
    PluginHost& m_host;
    AiTask m_task;
    QLabel* m_title{nullptr};
    QLabel* m_description{nullptr};
    QLabel* m_schedules{nullptr};
    QLabel* m_badge{nullptr};
    QLabel* m_phase{nullptr};
    QLabel* m_error{nullptr};
    QToolButton* m_start{nullptr};
    QToolButton* m_stop{nullptr};
    QToolButton* m_edit{nullptr};
    QToolButton* m_remove{nullptr};
    QToolButton* m_info{nullptr};
    QToolButton* m_chat{nullptr};
    QToolButton* m_schedule{nullptr};
    QToolButton* m_openFolder{nullptr};
    QToolButton* m_serveFolder{nullptr};
    QPoint m_pressedAt;
};

class KanbanColumn final : public QWidget {
  public:
    KanbanColumn(TaskColumn column, PluginHost& host, std::function<void(const QString&, TaskColumn)> onDrop, QWidget* parent) : QWidget(parent), m_column(column), m_onDrop(std::move(onDrop)) {
        setObjectName(QStringLiteral("aiKanbanColumn"));
        setAcceptDrops(true);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        auto* title = new QLabel(host.translate(AiTasksViewHelper::columnTitleKey(column)), this);
        title->setObjectName(QStringLiteral("aiColumnTitle"));
        title->setAlignment(Qt::AlignCenter);
        title->setFixedHeight(host.theme().metric(ui::ThemeMetric::WorkspaceBarHeight));
        layout->addWidget(title);

        auto* scrollArea = new QScrollArea(this);
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameShape(QFrame::NoFrame);
        m_cards = new QWidget(scrollArea);
        m_cardsLayout = new QVBoxLayout(m_cards);
        m_cardsLayout->setContentsMargins(8, 8, 8, 8);
        m_cardsLayout->setSpacing(8);
        m_cardsLayout->addStretch();
        scrollArea->setWidget(m_cards);
        layout->addWidget(scrollArea, 1);
    }

    // A card already in this column is left where it is, so a board that only changed state moves nothing.
    void adoptCard(TaskCard* card) {
        if (m_cardsLayout->indexOf(card) >= 0) {
            return;
        }

        m_cardsLayout->insertWidget(m_cardsLayout->count() - 1, card);
    }

  protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasFormat(taskMimeType)) {
            event->acceptProposedAction();
        }
    }

    void dragMoveEvent(QDragMoveEvent* event) override {
        if (event->mimeData()->hasFormat(taskMimeType)) {
            event->acceptProposedAction();
        }
    }

    void dropEvent(QDropEvent* event) override {
        if (!event->mimeData()->hasFormat(taskMimeType)) {
            return;
        }

        event->acceptProposedAction();
        m_onDrop(QString::fromUtf8(event->mimeData()->data(taskMimeType)), m_column);
    }

  private:
    TaskColumn m_column;
    std::function<void(const QString&, TaskColumn)> m_onDrop;
    QWidget* m_cards{nullptr};
    QVBoxLayout* m_cardsLayout{nullptr};
};

AiTasksView::AiTasksView(AiPlugin& plugin, PluginHost& host, QWidget* parent) : QWidget(parent), m_plugin(plugin), m_host(host) {
    setObjectName(QStringLiteral("aiTasksView"));
    auto* stackLayout = new QVBoxLayout(this);
    stackLayout->setContentsMargins(0, 0, 0, 0);
    stackLayout->setSpacing(0);
    m_stack = new QStackedWidget(this);
    stackLayout->addWidget(m_stack);

    auto* board = new QWidget(m_stack);
    auto* root = new QVBoxLayout(board);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    m_stack->addWidget(board);

    auto* header = new ui::PageHeader(m_host.theme(), m_host.translate(QStringLiteral("ai.tasks.title")), board);
    auto* addTask = ui::Components::toolButton(ui::IconName::Add, m_host.theme(), m_host.translate(QStringLiteral("ai.task.add")), header);
    addTask->setObjectName(QStringLiteral("aiAddTask"));
    auto* addWorkspace = ui::Components::toolButton(ui::IconName::Workspace, m_host.theme(), m_host.translate(QStringLiteral("ai.workspace.add")), header);
    addWorkspace->setObjectName(QStringLiteral("aiAddWorkspace"));
    auto* renameWorkspaceButton = ui::Components::toolButton(ui::IconName::Edit, m_host.theme(), m_host.translate(QStringLiteral("ai.workspace.rename")), header);
    renameWorkspaceButton->setObjectName(QStringLiteral("aiRenameWorkspace"));
    header->addStretch();
    header->addWidget(addTask);
    header->addWidget(addWorkspace);
    header->addWidget(renameWorkspaceButton);
    root->addWidget(header);

    m_workspaces = new ui::TabBar(m_host.theme(), board);
    m_workspaces->setObjectName(QStringLiteral("aiWorkspaces"));
    m_workspaces->setTabsClosable(true);
    m_workspaces->setExpanding(false);
    root->addWidget(m_workspaces);

    m_workspaceSeparator = ui::Components::horizontalDivider(board);
    root->addWidget(m_workspaceSeparator);

    m_kanban = new QWidget(board);
    auto* kanbanLayout = new QHBoxLayout(m_kanban);
    kanbanLayout->setContentsMargins(0, 0, 0, 0);
    kanbanLayout->setSpacing(0);
    // clang-format off
    const auto dropHandler = [this](const QString& taskId, TaskColumn column) { auto future = m_plugin.moveTask(taskId, column); future.then(this, [this, taskId](Result<void> result) { if (!result.hasValue()) { showError(result.error(), moveFailureMessage(taskId, result.error())); } }); };
    // clang-format on
    const auto boardColumns = AiTaskRepository::columns();

    for (const auto column : boardColumns) {
        if (column != boardColumns.first()) {
            kanbanLayout->addWidget(ui::Components::verticalDivider(m_kanban));
        }
        auto* view = new KanbanColumn(column, m_host, dropHandler, m_kanban);
        m_columns.insert(column, view);
        kanbanLayout->addWidget(view, 1);
    }

    root->addWidget(m_kanban, 1);

    m_empty = new QWidget(board);
    auto* emptyLayout = new QVBoxLayout(m_empty);
    emptyLayout->setAlignment(Qt::AlignCenter);
    emptyLayout->setSpacing(14);
    emptyLayout->addWidget(ui::Components::emptyStateLabel(m_host.translate(QStringLiteral("ai.workspace.empty")), m_empty));
    auto* createFirstWorkspace = new QPushButton(ui::IconCatalog::primaryIcon(ui::IconName::Add, m_host.theme()), m_host.translate(QStringLiteral("ai.workspace.add")), m_empty);
    createFirstWorkspace->setObjectName(QStringLiteral("primaryButton"));
    createFirstWorkspace->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    emptyLayout->addWidget(createFirstWorkspace, 0, Qt::AlignHCenter);
    root->addWidget(m_empty, 1);

    connect(createFirstWorkspace, &QPushButton::clicked, this, &AiTasksView::createWorkspace);

    // clang-format off
    connect(addWorkspace, &QToolButton::clicked, this, [this]() { createWorkspace(); });
    connect(renameWorkspaceButton, &QToolButton::clicked, this, [this]() { renameWorkspace(); });
    connect(addTask, &QToolButton::clicked, this, [this]() { createTask(); });
    connect(m_workspaces, &QTabBar::tabCloseRequested, this, [this](int index) { m_workspaces->setCurrentIndex(index); removeWorkspace(); });
    connect(m_workspaces, &QTabBar::currentChanged, this, [this](int index) { if (!m_synchronizing && index >= 0) { auto future = m_plugin.activateWorkspace(m_workspaces->tabData(index).toString()); future.then(this, [this](Result<void> result) { if (!result.hasValue()) { showError(result.error(), m_host.translate(QStringLiteral("ai.error.workspace-save"))); } }); } });
    connect(&m_plugin, &AiPlugin::workspacesChanged, this, [this]() { synchronizeWorkspaces(); });
    connect(&m_plugin, &AiPlugin::tasksChanged, this, [this]() { refreshKanban(); });
    connect(&m_plugin, &AiPlugin::taskRunStateChanged, this, [this](const QString& taskId) { if (auto* card = m_cards.value(taskId, nullptr); card != nullptr) { card->updateRunState(); } });
    // clang-format on

    synchronizeWorkspaces();
}

// A task is answered by one surface, so the conversation, the executions and their logs open together.
void AiTasksView::openTaskSurface(const AiTask& task, bool onConversation) {
    auto* surface = new AiTaskInfoDialog(m_plugin, m_host, task, this);
    surface->setAttribute(Qt::WA_DeleteOnClose);

    if (onConversation) {
        surface->showConversation();
    }

    ui::Components::showDialogWindow(surface, task.title);
}

QString AiTasksView::activeWorkspaceId() const {
    for (const auto& workspace : m_plugin.workspaces()) {
        if (workspace.active) {
            return workspace.id;
        }
    }

    return {};
}

void AiTasksView::synchronizeWorkspaces() {
    m_synchronizing = true;

    while (m_workspaces->count() > 0) {
        m_workspaces->removeTab(0);
    }

    for (const auto& workspace : m_plugin.workspaces()) {
        const int index = m_workspaces->addTab(workspace.name);
        m_workspaces->setTabData(index, workspace.id);
        if (workspace.active) {
            m_workspaces->setCurrentIndex(index);
        }
    }

    m_synchronizing = false;

    const bool hasWorkspace = m_workspaces->count() > 0;
    m_workspaces->setVisible(hasWorkspace);
    m_workspaceSeparator->setVisible(hasWorkspace);
    m_kanban->setVisible(hasWorkspace);
    m_empty->setVisible(!hasWorkspace);
    refreshKanban();
}

void AiTasksView::createWorkspace() {
    bool accepted = false;
    const QString name = QInputDialog::getText(this, m_host.translate(QStringLiteral("ai.workspace.add")), m_host.translate(QStringLiteral("ai.workspace.name")), QLineEdit::Normal, {}, &accepted).trimmed();

    if (!accepted || name.isEmpty()) {
        return;
    }

    auto future = m_plugin.createWorkspace(name);
    // clang-format off
    future.then(this, [this](Result<QString> result) { if (!result.hasValue()) { showError(result.error(), m_host.translate(QStringLiteral("ai.error.workspace-save"))); } });
    // clang-format on
}

void AiTasksView::renameWorkspace() {
    const QString workspaceId = activeWorkspaceId();

    if (workspaceId.isEmpty()) {
        return;
    }

    QString current;

    for (const auto& workspace : m_plugin.workspaces()) {
        current = workspace.id == workspaceId ? workspace.name : current;
    }

    bool accepted = false;
    const QString name = QInputDialog::getText(this, m_host.translate(QStringLiteral("ai.workspace.rename")), m_host.translate(QStringLiteral("ai.workspace.name")), QLineEdit::Normal, current, &accepted).trimmed();

    if (!accepted || name.isEmpty() || name == current) {
        return;
    }

    auto future = m_plugin.renameWorkspace(workspaceId, name);
    // clang-format off
    future.then(this, [this](Result<void> result) { if (!result.hasValue()) { showError(result.error(), m_host.translate(QStringLiteral("ai.error.workspace-save"))); } });
    // clang-format on
}

void AiTasksView::removeWorkspace() {
    const QString workspaceId = activeWorkspaceId();

    if (workspaceId.isEmpty() || !m_host.confirm(this, m_host.translate(QStringLiteral("ai.workspace.remove")), m_host.translate(QStringLiteral("ai.workspace.remove-message")), {}, m_host.translate(QStringLiteral("ai.workspace.remove")), true)) {
        return;
    }

    auto future = m_plugin.removeWorkspace(workspaceId);
    // clang-format off
    future.then(this, [this](Result<void> result) { if (!result.hasValue()) { showError(result.error(), m_host.translate(QStringLiteral("ai.error.workspace-save"))); } });
    // clang-format on
}

void AiTasksView::createTask() {
    if (activeWorkspaceId().isEmpty()) {
        return;
    }

    editTask(std::nullopt);
}

// Stopping a schedule from the card removes it, so the action disappears with the schedule it was there to stop.
void AiTasksView::removeSchedule(const AiTask& task) {
    if (!task.schedule.has_value() || !m_host.confirm(this, m_host.translate(QStringLiteral("ai.task.schedule-remove")), m_host.translate(QStringLiteral("ai.task.schedule-remove-message")), task.title, m_host.translate(QStringLiteral("ai.task.schedule-remove")), true)) {
        return;
    }

    AiTask updated = task;
    updated.schedule.reset();
    auto future = m_plugin.saveTask(std::move(updated));
    // clang-format off
    future.then(this, [this](Result<void> result) { if (!result.hasValue()) { showError(result.error(), m_host.translate(QStringLiteral("ai.error.task-save"))); } });
    // clang-format on
}

void AiTasksView::editTask(std::optional<AiTask> task) {
    AiTaskDialog dialog(m_host, activeWorkspaceId(), std::move(task), m_plugin.executionSettings(), m_plugin.agents(), this);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    auto future = m_plugin.saveTask(dialog.task());
    // clang-format off
    future.then(this, [this](Result<void> result) { if (!result.hasValue()) { showError(result.error(), m_host.translate(QStringLiteral("ai.error.schedule-save"))); } });
    // clang-format on
}

// The error carries the diagnostic and the message is the sentence the user reads, which for a request is the one the answering plugin published.
// Dropping a card into Doing starts the task, so a run that has nobody to run it says that rather than reading as a card that could not be saved.
QString AiTasksView::moveFailureMessage(const QString& taskId, const Error& error) const {
    if (error.code == QStringLiteral("ai_agent_unknown")) {
        // clang-format off
        const auto moved = std::find_if(m_plugin.tasks().constBegin(), m_plugin.tasks().constEnd(), [&taskId](const AiTask& candidate) { return candidate.id == taskId; });
        // clang-format on
        if (moved != m_plugin.tasks().constEnd()) {
            return m_host.translate(QStringLiteral("ai.error.agent-removed")).arg(moved->agentId);
        }
    }

    return m_host.translate(QStringLiteral("ai.error.task-save"));
}

void AiTasksView::showError(const Error& error, const QString& message) {
    m_host.log(LogLevel::Error, QStringLiteral("ai.tasks"), error.message, {{QStringLiteral("code"), error.code}, {QStringLiteral("detail"), error.detail}});
    m_host.notify(m_host.translate(QStringLiteral("ai.error.title")), message, AlertSeverity::Error);
}

// The board is reconciled rather than rebuilt, because destroying a card while it is delivering an event is what breaks the application.
void AiTasksView::refreshKanban() {
    const QString workspaceId = activeWorkspaceId();
    QSet<QString> present;

    for (const auto& task : m_plugin.tasks()) {
        if (task.workspaceId != workspaceId) {
            continue;
        }

        present.insert(task.id);
        TaskCard* card = m_cards.value(task.id, nullptr);
        if (card == nullptr) {
            card = createCard(task);
            m_cards.insert(task.id, card);
        }
        card->setTask(task);
        m_columns.value(task.column)->adoptCard(card);
    }

    // Only a task that is gone takes its card with it, and nothing removes a task but the person looking at it.
    for (auto entry = m_cards.begin(); entry != m_cards.end();) {
        if (present.contains(entry.key())) {
            ++entry;
            continue;
        }
        entry.value()->setParent(nullptr);
        entry.value()->deleteLater();
        entry = m_cards.erase(entry);
    }
}

// Every action reads the task the card carries now, so one that moved or was edited never acts on what it carried before.
TaskCard* AiTasksView::createCard(const AiTask& task) {
    // clang-format off
    // clang-format on
    auto* card = new TaskCard(m_plugin, m_host, task, m_columns.value(task.column));
    const QString taskId = task.id;
    // clang-format off
    connect(card->startButton(), &QToolButton::clicked, this, [this, taskId]() { auto future = m_plugin.startTask(taskId); future.then(this, [this](Result<void> result) { if (!result.hasValue()) { showError(result.error(), m_host.translate(QStringLiteral("ai.error.task-run"))); } }); });
    connect(card->stopButton(), &QToolButton::clicked, this, [this, taskId]() { auto future = m_plugin.stopTask(taskId); future.then(this, [this](Result<void> result) { if (!result.hasValue()) { showError(result.error(), m_host.translate(QStringLiteral("ai.error.task-run"))); } }); });
    connect(card->infoButton(), &QToolButton::clicked, this, [this, card]() { openTaskSurface(card->task(), false); });
    connect(card->chatButton(), &QToolButton::clicked, this, [this, card]() { openTaskSurface(card->task(), true); });
    connect(card->editButton(), &QToolButton::clicked, this, [this, card]() { editTask(card->task()); });
    connect(card->scheduleButton(), &QToolButton::clicked, this, [this, card]() { removeSchedule(card->task()); });
    connect(card->openFolderButton(), &QToolButton::clicked, this, [this, card]() { requestFolderDestination(QString::fromLatin1(openFolderCapability), card->task().workdir); });
    connect(card->serveFolderButton(), &QToolButton::clicked, this, [this, card]() { requestFolderDestination(QString::fromLatin1(serveFolderCapability), card->task().workdir); });
    connect(card->removeButton(), &QToolButton::clicked, this, [this, card, taskId]() { if (m_host.confirm(this, m_host.translate(QStringLiteral("ai.task.remove")), m_host.translate(QStringLiteral("ai.task.remove-message")), card->task().title, m_host.translate(QStringLiteral("ai.task.remove")), true)) { auto future = m_plugin.removeTask(taskId); future.then(this, [this](Result<void> result) { if (!result.hasValue()) { showError(result.error(), m_host.translate(QStringLiteral("ai.error.task-save"))); } }); } });
    // clang-format on
    return card;
}

// Whoever provides the capability decides what to do with the folder and reveals itself, so the board never reaches into another plugin.
void AiTasksView::requestFolderDestination(const QString& capability, const QString& path) {
    if (path.isEmpty()) {
        return;
    }

    if (!m_host.capabilityAvailable(capability)) {
        m_host.notify(m_host.translate(QStringLiteral("ai.error.title")), m_host.translate(QStringLiteral("ai.error.destination-unavailable")), AlertSeverity::Warning);
        return;
    }

    // clang-format off
    const auto answered = [this](Result<QJsonObject> result) { if (!result.hasValue()) { showError(result.error(), result.error().message); } };
    // clang-format on
    m_host.invokeCapability(capability, {{QStringLiteral("path"), path}}, *this, answered);
}

} // namespace workpane::plugins::ai
