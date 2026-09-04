#include "AiTaskDialog.h"

#include "CronExpression.h"
#include "ui/Components.h"
#include "ui/Icons.h"
#include "ui/TabBar.h"
#include "ui/Theme.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimeZone>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>
#include <utility>

namespace workpane::plugins::ai {

constexpr int agentRow = 4;
constexpr int workingDirectoryRow = 5;
constexpr int commandRow = 6;
constexpr int commandTimeoutRow = 7;
constexpr int dialogMinimumWidth = 780;
constexpr int dialogMinimumHeight = 520;

AiTaskDialog::AiTaskDialog(PluginHost& host, const QString& workspaceId, std::optional<AiTask> task, const ExecutionSettings& defaults, const QVector<AiAgent>& agents, QWidget* parent) : QDialog(parent), m_host(host), m_workspaceId(workspaceId), m_original(std::move(task)), m_defaults(defaults), m_agents(agents) {
    setObjectName(QStringLiteral("aiTaskDialog"));
    setWindowTitle(m_host.translate(m_original.has_value() ? QStringLiteral("ai.task.edit") : QStringLiteral("ai.task.add")));
    setMinimumSize(dialogMinimumWidth, dialogMinimumHeight);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* tabs = new ui::TabWidget(m_host.theme(), this);
    tabs->setObjectName(QStringLiteral("aiTaskDialogTabs"));
    tabs->addTab(createGeneralPage(), m_host.translate(QStringLiteral("ai.task.tab-general")));
    tabs->addTab(createPromptPage(), m_host.translate(QStringLiteral("ai.task.tab-prompt")));
    layout->addWidget(tabs, 1);

    m_validation = new QLabel(this);
    m_validation->setObjectName(QStringLiteral("aiTaskValidation"));
    m_validation->setWordWrap(true);
    m_validation->hide();
    layout->addWidget(m_validation);

    auto* footer = new QWidget(this);
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(14, 10, 14, 12);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, footer);
    buttons->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("primaryButton"));
    footerLayout->addStretch(1);
    footerLayout->addWidget(buttons);
    layout->addWidget(footer);

    connect(buttons, &QDialogButtonBox::accepted, this, &AiTaskDialog::submit);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateExecutionFields();
    updateScheduleFields();
}

QWidget* AiTaskDialog::createGeneralPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(14);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(10);

    m_title = new QLineEdit(m_original.has_value() ? m_original->title : QString{}, page);
    m_title->setObjectName(QStringLiteral("aiTaskTitleField"));
    m_description = new QLineEdit(m_original.has_value() ? m_original->description : QString{}, page);
    m_description->setObjectName(QStringLiteral("aiTaskDescriptionField"));

    form->addRow(m_host.translate(QStringLiteral("ai.task.title")), m_title);
    form->addRow(m_host.translate(QStringLiteral("ai.task.description")), m_description);
    m_issueUrl = new QLineEdit(m_original.has_value() ? m_original->issueUrl : QString{}, page);
    m_issueUrl->setObjectName(QStringLiteral("aiTaskIssueUrl"));
    m_issueUrl->setPlaceholderText(m_host.translate(QStringLiteral("ai.task.issue-url-placeholder")));
    form->addRow(m_host.translate(QStringLiteral("ai.task.issue-url")), m_issueUrl);

    m_executionKind = new ui::ComboBox(m_host.theme(), page);
    m_executionKind->setObjectName(QStringLiteral("aiTaskExecutionKind"));
    m_executionKind->addItem(m_host.translate(QStringLiteral("ai.task.kind-agent")), AiTaskRepository::taskExecutionKindName(TaskExecutionKind::Agent));
    m_executionKind->addItem(m_host.translate(QStringLiteral("ai.task.kind-command")), AiTaskRepository::taskExecutionKindName(TaskExecutionKind::Command));
    ui::Components::sortComboBoxItems(m_executionKind);
    m_executionKind->setCurrentIndex(std::max(0, m_executionKind->findData(AiTaskRepository::taskExecutionKindName(m_original.has_value() ? m_original->executionKind : TaskExecutionKind::Agent))));
    form->addRow(m_host.translate(QStringLiteral("ai.task.execution-kind")), m_executionKind);

    // Every row shares one form, so the labels of the general and the execution fields align in a single column.
    m_executionForm = form;

    m_agent = new ui::ComboBox(m_host.theme(), page);
    m_agent->setObjectName(QStringLiteral("aiTaskAgent"));

    for (const auto& agent : m_agents) {
        m_agent->addItem(agent.name, agent.id);
    }

    ui::Components::sortComboBoxItems(m_agent);

    if (m_original.has_value() && !m_original->agentId.isEmpty()) {
        m_agent->setCurrentIndex(std::max(0, m_agent->findData(m_original->agentId)));
    }

    m_workdir = new QLineEdit(m_original.has_value() ? m_original->workdir : QDir::homePath(), page);
    m_workdir->setObjectName(QStringLiteral("aiTaskWorkdir"));
    auto* browse = ui::Components::toolButton(ui::IconName::Folder, m_host.theme(), m_host.translate(QStringLiteral("ai.task.choose-workdir")), page);
    auto* workdirRow = new QWidget(page);
    auto* workdirLayout = new QHBoxLayout(workdirRow);
    workdirLayout->setContentsMargins(0, 0, 0, 0);
    workdirLayout->setSpacing(6);
    workdirLayout->addWidget(m_workdir, 1);
    workdirLayout->addWidget(browse);

    m_command = new QLineEdit(m_original.has_value() ? m_original->command : QString{}, page);
    m_command->setObjectName(QStringLiteral("aiTaskCommand"));
    m_commandTimeout = new QSpinBox(page);
    m_commandTimeout->setObjectName(QStringLiteral("aiTaskCommandTimeout"));
    m_commandTimeout->setRange(0, 86400);
    m_commandTimeout->setValue(m_original.has_value() ? m_original->commandTimeoutSeconds : m_defaults.commandTimeoutSeconds);
    m_commandTimeout->setToolTip(m_host.translate(QStringLiteral("ai.task.unlimited-hint")));

    m_executionForm->addRow(m_host.translate(QStringLiteral("ai.task.agent")), m_agent);
    m_executionForm->addRow(m_host.translate(QStringLiteral("ai.task.workdir")), workdirRow);
    m_executionForm->addRow(m_host.translate(QStringLiteral("ai.task.command")), m_command);
    m_executionForm->addRow(m_host.translate(QStringLiteral("ai.task.command-timeout")), ui::Components::stepperRow(m_commandTimeout, m_host.theme(), page));
    layout->addLayout(form);

    // clang-format off
    connect(m_executionKind, &QComboBox::currentIndexChanged, this, [this]() { updateExecutionFields(); });
    connect(browse, &QToolButton::clicked, this, [this]() { const QString selected = QFileDialog::getExistingDirectory(this, m_host.translate(QStringLiteral("ai.task.choose-workdir")), m_workdir->text().isEmpty() ? QDir::homePath() : m_workdir->text()); if (!selected.isEmpty()) { m_workdir->setText(selected); } });
    // clang-format on

    layout->addWidget(ui::Components::sectionTitleLabel(m_host.translate(QStringLiteral("ai.task.schedule")), page));
    m_scheduleForm = new QFormLayout();
    m_scheduleForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_scheduleForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_scheduleForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_scheduleForm->setHorizontalSpacing(16);
    m_scheduleForm->setVerticalSpacing(10);

    // The kinds read from no schedule to the most expressive one, which is the order that explains them.
    m_scheduleKind = new ui::ComboBox(m_host.theme(), page);
    m_scheduleKind->setObjectName(QStringLiteral("aiTaskScheduleKind"));
    m_scheduleKind->addItem(m_host.translate(QStringLiteral("ai.schedule.none")), QString{});
    m_scheduleKind->addItem(m_host.translate(QStringLiteral("ai.schedule.once")), AiTaskRepository::scheduleKindName(ScheduleKind::Once));
    m_scheduleKind->addItem(m_host.translate(QStringLiteral("ai.schedule.interval")), AiTaskRepository::scheduleKindName(ScheduleKind::Interval));
    m_scheduleKind->addItem(m_host.translate(QStringLiteral("ai.schedule.cron")), AiTaskRepository::scheduleKindName(ScheduleKind::Cron));
    m_scheduleAt = new ui::DateTimeField(m_host.theme(), page);
    m_scheduleAt->setObjectName(QStringLiteral("aiTaskScheduleAt"));
    m_scheduleAt->setDateTime(QDateTime::currentDateTime().addSecs(3600));
    m_scheduleInterval = new QSpinBox(page);
    m_scheduleInterval->setObjectName(QStringLiteral("aiTaskScheduleInterval"));
    m_scheduleInterval->setRange(1, 100000);
    m_scheduleInterval->setSuffix(m_host.translate(QStringLiteral("ai.schedule.minutes")));
    m_scheduleCron = new QLineEdit(page);
    m_scheduleCron->setObjectName(QStringLiteral("aiTaskScheduleCron"));
    m_scheduleCron->setPlaceholderText(QStringLiteral("0 3 * * 1"));

    if (m_original.has_value() && m_original->schedule.has_value()) {
        const auto& schedule = m_original->schedule.value();
        m_scheduleKind->setCurrentIndex(m_scheduleKind->findData(AiTaskRepository::scheduleKindName(schedule.kind)));
        m_scheduleAt->setDateTime(schedule.onceAtUtc.isValid() ? schedule.onceAtUtc.toLocalTime() : QDateTime::currentDateTime().addSecs(3600));
        m_scheduleInterval->setValue(schedule.intervalSeconds > 0 ? static_cast<int>(schedule.intervalSeconds / 60) : 1);
        m_scheduleCron->setText(schedule.cronExpression);
    }

    m_scheduleForm->addRow(m_host.translate(QStringLiteral("ai.schedule.type")), m_scheduleKind);
    m_scheduleForm->addRow(m_host.translate(QStringLiteral("ai.schedule.once")), ui::Components::stepperRow(m_scheduleAt, m_host.theme(), page));
    m_scheduleForm->addRow(m_host.translate(QStringLiteral("ai.schedule.interval")), ui::Components::stepperRow(m_scheduleInterval, m_host.theme(), page));
    m_scheduleForm->addRow(m_host.translate(QStringLiteral("ai.schedule.cron")), m_scheduleCron);
    layout->addLayout(m_scheduleForm);
    layout->addStretch(1);

    // clang-format off
    connect(m_scheduleKind, &QComboBox::currentIndexChanged, this, [this]() { updateScheduleFields(); });
    // clang-format on
    return page;
}

QWidget* AiTaskDialog::createPromptPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(10);
    layout->addWidget(ui::Components::sectionTitleLabel(m_host.translate(QStringLiteral("ai.task.prompt")), page));
    m_prompt = new ui::TextField(m_host.translate(QStringLiteral("ai.task.prompt-placeholder")), page);
    m_prompt->setPlainText(m_original.has_value() ? m_original->prompt : QString{});
    m_prompt->setObjectName(QStringLiteral("aiTaskPromptField"));
    layout->addWidget(m_prompt, 1);
    return page;
}

// The working directory belongs to both kinds, because an agent needs it for its file tools and a command runs inside it.
void AiTaskDialog::updateExecutionFields() {
    const bool command = m_executionKind->currentData().toString() == AiTaskRepository::taskExecutionKindName(TaskExecutionKind::Command);
    m_executionForm->setRowVisible(agentRow, !command);
    m_executionForm->setRowVisible(workingDirectoryRow, true);
    m_executionForm->setRowVisible(commandRow, command);
    m_executionForm->setRowVisible(commandTimeoutRow, command);
}

void AiTaskDialog::updateScheduleFields() {
    const QString kind = m_scheduleKind->currentData().toString();
    m_scheduleForm->setRowVisible(1, kind == AiTaskRepository::scheduleKindName(ScheduleKind::Once));
    m_scheduleForm->setRowVisible(2, kind == AiTaskRepository::scheduleKindName(ScheduleKind::Interval));
    m_scheduleForm->setRowVisible(3, kind == AiTaskRepository::scheduleKindName(ScheduleKind::Cron));
}

std::optional<TaskSchedule> AiTaskDialog::buildSchedule() const {
    const QString kind = m_scheduleKind->currentData().toString();

    if (kind.isEmpty()) {
        return std::nullopt;
    }

    TaskSchedule schedule;
    schedule.kind = AiTaskRepository::parseScheduleKind(kind).value();
    schedule.timeZoneId = QTimeZone::systemTimeZoneId();
    schedule.onceAtUtc = schedule.kind == ScheduleKind::Once ? m_scheduleAt->dateTime().toUTC() : QDateTime{};
    schedule.intervalSeconds = schedule.kind == ScheduleKind::Interval ? m_scheduleInterval->value() * 60 : 0;
    schedule.cronExpression = schedule.kind == ScheduleKind::Cron ? m_scheduleCron->text().trimmed() : QString{};
    return schedule;
}

void AiTaskDialog::showValidation(const QString& messageKey) {
    m_validation->setText(m_host.translate(messageKey));
    m_validation->show();
    ui::Components::growDialogToContents(this);
}

void AiTaskDialog::submit() {
    m_validation->hide();

    if (m_title->text().trimmed().isEmpty()) {
        showValidation(QStringLiteral("ai.validation.title"));
        m_title->setFocus();
        return;
    }

    const bool command = m_executionKind->currentData().toString() == AiTaskRepository::taskExecutionKindName(TaskExecutionKind::Command);

    if (!command && m_prompt->toPlainText().trimmed().isEmpty()) {
        showValidation(QStringLiteral("ai.validation.prompt"));
        m_prompt->setFocus();
        return;
    }

    if (command && m_command->text().trimmed().isEmpty()) {
        showValidation(QStringLiteral("ai.validation.command"));
        m_command->setFocus();
        return;
    }

    const QString workdir = m_workdir->text().trimmed();

    if ((command || !workdir.isEmpty()) && (!QDir(workdir).isAbsolute() || !QDir(workdir).exists())) {
        showValidation(QStringLiteral("ai.validation.workdir"));
        m_workdir->setFocus();
        return;
    }

    if (!command && m_agent->currentData().toString().isEmpty()) {
        showValidation(QStringLiteral("ai.validation.agent-missing"));
        m_agent->setFocus();
        return;
    }

    const QUrl issue(m_issueUrl->text().trimmed());

    if (!m_issueUrl->text().trimmed().isEmpty() && (!issue.isValid() || issue.host().isEmpty() || (issue.scheme() != QStringLiteral("http") && issue.scheme() != QStringLiteral("https")))) {
        showValidation(QStringLiteral("ai.validation.issue-url"));
        m_issueUrl->setFocus();
        return;
    }

    const auto schedule = buildSchedule();

    if (schedule.has_value() && schedule->kind == ScheduleKind::Once && schedule->onceAtUtc <= QDateTime::currentDateTimeUtc()) {
        showValidation(QStringLiteral("ai.validation.schedule-past"));
        m_scheduleAt->setFocus();
        return;
    }

    if (schedule.has_value() && schedule->kind == ScheduleKind::Cron && !CronExpression::parse(schedule->cronExpression).hasValue()) {
        showValidation(QStringLiteral("ai.validation.cron"));
        m_scheduleCron->setFocus();
        return;
    }

    m_accepted = m_original.value_or(AiTask{});
    m_accepted.workspaceId = m_workspaceId;
    m_accepted.title = m_title->text().trimmed();
    m_accepted.description = m_description->text().trimmed();
    m_accepted.prompt = m_prompt->toPlainText().trimmed();
    m_accepted.issueUrl = m_issueUrl->text().trimmed();
    m_accepted.executionKind = AiTaskRepository::parseTaskExecutionKind(m_executionKind->currentData().toString()).value();
    m_accepted.agentId = command ? QString{} : m_agent->currentData().toString();
    m_accepted.workdir = workdir.isEmpty() ? QString{} : QDir::cleanPath(workdir);
    m_accepted.command = m_command->text().trimmed();
    m_accepted.commandTimeoutSeconds = m_commandTimeout->value();
    m_accepted.column = m_original.has_value() ? m_original->column : TaskColumn::Todo;
    m_accepted.schedule = schedule;

    accept();
}

const AiTask& AiTaskDialog::task() const {
    return m_accepted;
}

} // namespace workpane::plugins::ai
