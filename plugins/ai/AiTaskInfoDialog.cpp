#include "AiTaskInfoDialog.h"

#include "AiConversationView.h"
#include "AiPlugin.h"
#include "ui/Components.h"
#include "ui/Icons.h"
#include "ui/TabBar.h"
#include "ui/Theme.h"

#include <QAction>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace workpane::plugins::ai {

constexpr int reloadDebounceMs = 200;

constexpr int compactRowHeight = 34;
constexpr int dialogMinimumWidth = 820;
constexpr int dialogMinimumHeight = 560;

class AiTaskInfoDialogHelper final {
  public:
    static int rowOf(const QVector<TaskExecution>& executions, const QString& executionId);
    static int rowOfEntry(const QVector<ExecutionLogEntry>& entries, const QString& entryId);
    static QString entryAt(const QVector<ExecutionLogEntry>& entries, int row);
    static QString statusKey(ExecutionStatus status);
    static QString costText(const TaskExecution& execution);
    static QString kindKey(ExecutionLogKind kind);
    static QString levelKey(ExecutionLogLevel level);
};

QString AiTaskInfoDialogHelper::statusKey(ExecutionStatus status) {
    switch (status) {
    case ExecutionStatus::Running:
        return QStringLiteral("ai.status.running");
    case ExecutionStatus::Succeeded:
        return QStringLiteral("ai.status.succeeded");
    case ExecutionStatus::Failed:
        return QStringLiteral("ai.status.failed");
    case ExecutionStatus::Cancelled:
        return QStringLiteral("ai.status.cancelled");
    }

    return QStringLiteral("ai.status.running");
}

QString AiTaskInfoDialogHelper::kindKey(ExecutionLogKind kind) {
    return QStringLiteral("ai.log-kind.") + AiTaskRepository::executionLogKindName(kind);
}

// A run whose model or price the catalog does not declare reports no cost rather than one that reads as free.
QString AiTaskInfoDialogHelper::costText(const TaskExecution& execution) {
    const auto spent = ProviderCatalog::runCost(execution.providerId, execution.modelId, execution.inputTokens, execution.outputTokens);
    return spent.has_value() ? QStringLiteral("USD %1").arg(QLocale::system().toString(spent.value(), 'f', 4)) : QString{};
}

int AiTaskInfoDialogHelper::rowOf(const QVector<TaskExecution>& executions, const QString& executionId) {
    for (int row = 0; row < static_cast<int>(executions.size()); ++row) {
        if (executions.at(row).id == executionId) {
            return row;
        }
    }

    return -1;
}

int AiTaskInfoDialogHelper::rowOfEntry(const QVector<ExecutionLogEntry>& entries, const QString& entryId) {
    for (int row = 0; row < static_cast<int>(entries.size()); ++row) {
        if (entries.at(row).id == entryId) {
            return row;
        }
    }

    return -1;
}

QString AiTaskInfoDialogHelper::entryAt(const QVector<ExecutionLogEntry>& entries, int row) {
    return row >= 0 && row < static_cast<int>(entries.size()) ? entries.at(row).id : QString{};
}

QString AiTaskInfoDialogHelper::levelKey(ExecutionLogLevel level) {
    switch (level) {
    case ExecutionLogLevel::Debug:
        return QStringLiteral("ai.log-level.debug");
    case ExecutionLogLevel::Info:
        return QStringLiteral("ai.log-level.info");
    case ExecutionLogLevel::Warning:
        return QStringLiteral("ai.log-level.warning");
    case ExecutionLogLevel::Error:
        return QStringLiteral("ai.log-level.error");
    }

    return QStringLiteral("ai.log-level.info");
}

AiTaskInfoDialog::AiTaskInfoDialog(AiPlugin& plugin, PluginHost& host, const AiTask& task, QWidget* parent) : QDialog(parent), m_plugin(plugin), m_host(host), m_task(task) {
    setObjectName(QStringLiteral("aiTaskInfoDialog"));
    setWindowTitle(m_host.translate(QStringLiteral("ai.task.info")));
    setMinimumSize(dialogMinimumWidth, dialogMinimumHeight);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    setWindowTitle(m_task.title);
    auto* header = new ui::PageHeader(m_host.theme(), m_task.title, this);
    m_status = new QLabel(header);
    m_status->setObjectName(QStringLiteral("mutedLabel"));
    m_busy = new ui::BusyIndicator(m_host.theme(), header);
    m_phase = new QLabel(header);
    m_phase->setObjectName(QStringLiteral("mutedLabel"));
    m_stop = new QPushButton(ui::IconCatalog::destructiveIcon(ui::IconName::Stop, m_host.theme()), m_host.translate(QStringLiteral("ai.task.stop")), header);
    m_stop->setObjectName(QStringLiteral("destructiveButton"));
    m_stop->hide();
    header->addStretch();
    header->addWidget(m_busy);
    header->addWidget(m_phase);
    header->addWidget(m_status);
    header->addWidget(m_stop);
    layout->addWidget(header);

    m_tabs = new ui::TabWidget(m_host.theme(), this);
    m_tabs->setObjectName(QStringLiteral("aiTaskInfoTabs"));
    auto* tabs = m_tabs;

    // A task is a conversation with its agent, so the surface that answers for it opens on that conversation.
    if (m_task.executionKind == TaskExecutionKind::Agent) {
        m_conversation = new AiConversationView(m_plugin, m_host, tabs);
        m_conversation->setTask(m_task.id);
        tabs->addTab(m_conversation, m_host.translate(QStringLiteral("ai.task.tab-chat")));
    }

    auto* executionsPage = new QWidget(tabs);
    auto* executionsLayout = new QVBoxLayout(executionsPage);
    executionsLayout->setContentsMargins(0, 0, 0, 0);
    executionsLayout->setSpacing(0);
    m_executionGrid = ui::Components::dataGrid({m_host.translate(QStringLiteral("ai.execution.started")), m_host.translate(QStringLiteral("ai.execution.status")), m_host.translate(QStringLiteral("ai.execution.tokens")), m_host.translate(QStringLiteral("ai.execution.cost")), m_host.translate(QStringLiteral("ai.execution.finish-reason")), m_host.translate(QStringLiteral("ai.execution.error"))}, executionsPage);
    m_executionGrid->setObjectName(QStringLiteral("aiExecutionGrid"));
    m_executionGrid->setWordWrap(true);
    m_executionGrid->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui::Components::stretchGridColumn(m_executionGrid, 4);
    executionsLayout->addWidget(m_executionGrid, 1);
    tabs->addTab(executionsPage, m_host.translate(QStringLiteral("ai.task.tab-executions")));

    // A command run exchanges nothing with a model, so the log surface belongs to an agent run.
    if (m_task.executionKind == TaskExecutionKind::Agent) {
        auto* logsPage = new QWidget(tabs);
        auto* logsLayout = new QVBoxLayout(logsPage);
        logsLayout->setContentsMargins(0, 0, 0, 0);
        logsLayout->setSpacing(0);
        m_logGrid = ui::Components::dataGrid({m_host.translate(QStringLiteral("ai.log.timestamp")), m_host.translate(QStringLiteral("ai.log.level")), m_host.translate(QStringLiteral("ai.log.event")), m_host.translate(QStringLiteral("ai.log.detail"))}, logsPage);
        m_logGrid->setObjectName(QStringLiteral("aiLogGrid"));
        m_logGrid->setWordWrap(true);
        m_logGrid->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        ui::Components::stretchGridColumn(m_logGrid, 3);
        logsLayout->addWidget(m_logGrid, 1);
        tabs->addTab(logsPage, m_host.translate(QStringLiteral("ai.task.tab-logs")));
    }

    m_outputPages = new QStackedWidget(tabs);
    m_outputPages->setObjectName(QStringLiteral("aiOutputPages"));
    m_content = new ui::MarkdownView(m_host.theme(), m_outputPages);
    m_content->setObjectName(QStringLiteral("aiExecutionContent"));
    m_outputEmpty = ui::Components::emptyStateLabel({}, m_outputPages);
    m_outputEmpty->setObjectName(QStringLiteral("aiOutputEmpty"));
    m_outputPages->addWidget(m_content);
    m_outputPages->addWidget(m_outputEmpty);
    tabs->addTab(m_outputPages, m_host.translate(QStringLiteral("ai.task.tab-output")));

    layout->addWidget(tabs, 1);

    // clang-format off
    connect(m_stop, &QPushButton::clicked, this, [this]() { auto future = m_plugin.stopTask(m_task.id); future.then(this, [this](Result<void> result) { if (!result.hasValue()) { m_host.notify(m_host.translate(QStringLiteral("ai.plugin.title")), m_host.translate(QStringLiteral("ai.error.stop")), AlertSeverity::Error); } }); });
    connect(&m_plugin, &AiPlugin::taskRunStateChanged, this, [this](const QString& taskId) { if (taskId == m_task.id) { updateRunState(); scheduleReload(); } });
    connect(&m_plugin, &AiPlugin::executionActivity, this, [this](const QString& taskId) { if (taskId == m_task.id) { scheduleReload(); } });
    // clang-format on
    auto* closeAction = new QAction(m_host.translate(QStringLiteral("ai.conversation.back")), this);
    closeAction->setObjectName(QStringLiteral("aiTaskSurfaceClose"));
    closeAction->setShortcuts(QKeySequence::keyBindings(QKeySequence::Close));
    closeAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(closeAction);
    // clang-format off
    connect(closeAction, &QAction::triggered, this, [this]() { reject(); });
    // clang-format on

    m_reload = new QTimer(this);
    m_reload->setSingleShot(true);
    m_reload->setInterval(reloadDebounceMs);
    // clang-format off
    connect(m_reload, &QTimer::timeout, this, [this]() { loadExecutions(); });
    // clang-format on
    updateRunState();

    auto* footer = new QWidget(this);
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(14, 10, 14, 12);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, footer);
    footerLayout->addStretch(1);
    footerLayout->addWidget(buttons);
    layout->addWidget(footer);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // clang-format off
    connect(m_executionGrid, &QTableWidget::currentCellChanged, this, [this](int row) { showExecution(row); });
    // clang-format on

    loadExecutions();
}

void AiTaskInfoDialog::showConversation() {
    if (m_conversation != nullptr) {
        m_tabs->setCurrentWidget(m_conversation);
    }
}

// Everything on the surface follows the run, so a message, a phase, an execution and a log entry appear while it is open.
void AiTaskInfoDialog::updateRunState() {
    const bool running = m_plugin.runState(m_task.id) != TaskRunState::Idle;
    m_busy->setRunning(running);
    m_stop->setVisible(running);
    m_phase->setVisible(running);
    m_phase->setText(running ? m_host.translate(QStringLiteral("ai.phase.") + AiPlugin::phaseName(m_plugin.executionPhase(m_task.id))) : QString{});
}

void AiTaskInfoDialog::scheduleReload() {
    m_reload->start();
}

QString AiTaskInfoDialog::selectedExecutionId() const {
    const int row = m_executionGrid->currentRow();
    return row >= 0 && row < static_cast<int>(m_executions.size()) ? m_executions.at(row).id : QString{};
}

void AiTaskInfoDialog::loadExecutions() {
    auto future = m_plugin.executions(m_task.id);
    // clang-format off
    future.then(this, [this](Result<QVector<TaskExecution>> loaded) { showExecutions(loaded); });
    // clang-format on
}

void AiTaskInfoDialog::showExecutions(const Result<QVector<TaskExecution>>& loaded) {
    if (!loaded.hasValue()) {
        m_status->setText(loaded.error().message);
        return;
    }

    // A reload keeps the execution the reader opened and the place they were reading, because a run writes while they read.
    const QString reading = selectedExecutionId();
    const int restingAt = m_executionGrid->verticalScrollBar()->value();
    m_executions = loaded.value();
    m_status->setText(m_host.translate(QStringLiteral("ai.execution.count")).arg(QString::number(m_executions.size())));
    m_executionGrid->setRowCount(static_cast<int>(m_executions.size()));

    for (int row = 0; row < static_cast<int>(m_executions.size()); ++row) {
        const TaskExecution& execution = m_executions.at(row);
        m_executionGrid->setItem(row, 0, new QTableWidgetItem(ui::Components::localTimestamp(execution.startedAtUtc)));
        m_executionGrid->setItem(row, 1, new QTableWidgetItem(m_host.translate(AiTaskInfoDialogHelper::statusKey(execution.status))));
        m_executionGrid->setItem(row, 2, new QTableWidgetItem(m_host.translate(QStringLiteral("ai.execution.token-usage")).arg(QString::number(execution.inputTokens), QString::number(execution.outputTokens))));
        m_executionGrid->setItem(row, 3, new QTableWidgetItem(AiTaskInfoDialogHelper::costText(execution)));
        m_executionGrid->setItem(row, 4, new QTableWidgetItem(execution.finishReason));
        m_executionGrid->setItem(row, 5, new QTableWidgetItem(execution.errorMessage));
    }

    const int wanted = AiTaskInfoDialogHelper::rowOf(m_executions, reading);

    if (m_executions.isEmpty()) {
        return;
    }

    const QSignalBlocker quiet(m_executionGrid);
    m_executionGrid->setCurrentCell(std::max(0, wanted), 0);
    m_executionGrid->verticalScrollBar()->setValue(restingAt);
    showExecution(std::max(0, wanted));
}

void AiTaskInfoDialog::showExecution(int row) {
    if (row < 0 || row >= static_cast<int>(m_executions.size())) {
        if (m_logGrid != nullptr) {
            m_logGrid->setRowCount(0);
        }
        m_content->clear();
        showOutputPlaceholder(m_host.translate(QStringLiteral("ai.output.none")));
        return;
    }

    const TaskExecution& execution = m_executions.at(row);
    m_content->setDocumentMarkdown(execution.content);

    if (execution.content.isEmpty()) {
        showOutputPlaceholder(outputPlaceholder(execution));
    } else {
        m_outputPages->setCurrentWidget(m_content);
    }

    if (m_logGrid == nullptr) {
        return;
    }

    // A later selection must win, so a completion that belongs to an earlier one is discarded.
    const quint64 revision = ++m_logRevision;
    auto future = m_plugin.executionLogs(execution.id);
    // clang-format off
    future.then(this, [this, revision](Result<QVector<ExecutionLogEntry>> logs) { if (revision == m_logRevision) { showLogs(logs); } });
    // clang-format on
}

void AiTaskInfoDialog::showLogs(const Result<QVector<ExecutionLogEntry>>& logs) {
    if (!logs.hasValue()) {
        m_logGrid->setRowCount(0);
        m_logEntries.clear();
        return;
    }

    // Entries arrive above the reader, so the surface keeps the entry they were on and follows the newest only while they are already on it.
    const bool following = m_logGrid->verticalScrollBar()->value() == 0;
    const QString anchor = AiTaskInfoDialogHelper::entryAt(m_logEntries, m_logGrid->rowAt(0));
    m_logEntries = logs.value();
    m_logGrid->setRowCount(static_cast<int>(m_logEntries.size()));

    for (int entry = 0; entry < static_cast<int>(m_logEntries.size()); ++entry) {
        const ExecutionLogEntry& log = m_logEntries.at(entry);
        m_logGrid->setItem(entry, 0, new QTableWidgetItem(ui::Components::localTimestamp(log.timestampUtc)));
        m_logGrid->setItem(entry, 1, new QTableWidgetItem(m_host.translate(AiTaskInfoDialogHelper::levelKey(log.level))));
        m_logGrid->setItem(entry, 2, new QTableWidgetItem(m_host.translate(AiTaskInfoDialogHelper::kindKey(log.kind))));
        m_logGrid->removeCellWidget(entry, 3);
        if (!AiTaskRepository::carriesExchangedPayload(log.kind)) {
            m_logGrid->setItem(entry, 3, new QTableWidgetItem(log.detail));
            continue;
        }

        m_logGrid->setItem(entry, 3, new QTableWidgetItem(QString{}));

        // A row action is the shared compact button packed at the start of its cell, so it reads as an action instead of as text.
        auto* actions = new QWidget(m_logGrid);
        auto* actionsLayout = new QHBoxLayout(actions);
        actionsLayout->setContentsMargins(4, 0, 4, 0);
        actionsLayout->setSpacing(3);
        actionsLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        auto* open = ui::Components::chipButton(m_host.translate(QStringLiteral("ai.log.open-payload")), m_host.theme(), actions);
        actionsLayout->addWidget(open);

        const QString title = m_host.translate(AiTaskInfoDialogHelper::kindKey(log.kind));
        const QString payload = log.detail;
        // clang-format off
        connect(open, &QToolButton::clicked, this, [this, title, payload]() { showPayload(title, payload); });
        // clang-format on
        m_logGrid->setCellWidget(entry, 3, actions);
        m_logGrid->setRowHeight(entry, m_logGrid->rowHeight(entry) < compactRowHeight ? compactRowHeight : m_logGrid->rowHeight(entry));
    }

    if (following) {
        m_logGrid->verticalScrollBar()->setValue(0);
        return;
    }

    const int reading = AiTaskInfoDialogHelper::rowOfEntry(m_logEntries, anchor);

    if (reading < 0) {
        return;
    }

    m_logGrid->scrollToItem(m_logGrid->item(reading, 0), QAbstractItemView::PositionAtTop);
}

// A payload is data, so it is presented verbatim and only indented when it really is JSON.
// It answers nothing, so it opens without a nested event loop and the chip that opened it survives the reload that follows.
void AiTaskInfoDialog::showPayload(const QString& title, const QString& payload) {
    auto* viewer = new QDialog(this);
    viewer->setObjectName(QStringLiteral("aiPayloadDialog"));
    viewer->setAttribute(Qt::WA_DeleteOnClose);
    viewer->setWindowTitle(title);
    viewer->setMinimumSize(dialogMinimumWidth, dialogMinimumHeight);

    auto* layout = new QVBoxLayout(viewer);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(new ui::PageHeader(m_host.theme(), title, viewer));

    auto* content = new QPlainTextEdit(viewer);
    content->setObjectName(QStringLiteral("aiPayloadContent"));
    content->setReadOnly(true);
    content->setFrameShape(QFrame::NoFrame);
    content->setLineWrapMode(QPlainTextEdit::NoWrap);
    content->setFont(m_host.theme().font(ui::ThemeFont::Monospace));
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8());
    content->setPlainText(document.isNull() ? payload : QString::fromUtf8(document.toJson(QJsonDocument::Indented)));
    layout->addWidget(content, 1);

    auto* footer = new QWidget(viewer);
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(14, 10, 14, 12);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, footer);
    footerLayout->addStretch(1);
    footerLayout->addWidget(buttons);
    layout->addWidget(footer);

    connect(buttons, &QDialogButtonBox::rejected, viewer, &QDialog::reject);
    ui::Components::showDialogWindow(viewer, title);
}

QString AiTaskInfoDialog::outputPlaceholder(const TaskExecution& execution) const {
    switch (execution.status) {
    case ExecutionStatus::Running:
        return m_host.translate(QStringLiteral("ai.output.running"));
    case ExecutionStatus::Failed:
        return execution.errorMessage.isEmpty() ? m_host.translate(QStringLiteral("ai.output.failed")) : execution.errorMessage;
    case ExecutionStatus::Cancelled:
        return m_host.translate(QStringLiteral("ai.output.cancelled"));
    case ExecutionStatus::Succeeded:
        // A run that ended for a reason of its own says which one, because an empty answer explains nothing by itself.
        return execution.stopReason == AgentStopReason::Answered ? m_host.translate(QStringLiteral("ai.output.empty")) : m_host.translate(QStringLiteral("ai.stop-reason.") + AiTaskRepository::agentStopReasonName(execution.stopReason));
    }

    return m_host.translate(QStringLiteral("ai.output.empty"));
}

void AiTaskInfoDialog::showOutputPlaceholder(const QString& message) {
    m_outputEmpty->setText(message);
    m_outputPages->setCurrentWidget(m_outputEmpty);
}

} // namespace workpane::plugins::ai
