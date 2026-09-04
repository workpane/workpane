#include "AiPlugin.h"

#include "AiCliChatClient.h"

#include "AiAgentPrompt.h"

#include "AiAgentSettingsView.h"
#include "AiConnectionSettingsView.h"
#include "AiMcpSettingsView.h"
#include "AiRateLimitSettingsView.h"
#include "AiServiceSettingsView.h"
#include "AiTasksView.h"
#include "AiTranslations.h"
#include "CronExpression.h"
#include "persistence/StoredValues.h"
#include "ui/ApplicationShortcuts.h"
#include "ui/Components.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QSysInfo>
#include <QTimeZone>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <limits>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>

namespace workpane::plugins::ai {

constexpr auto startTaskCapability = "ai.task.start";
// A history is read newest first in pages, like every other collection the project stores.
constexpr int conversationPageSize = 100;

class AiPluginHelper final {
  public:
    static QString identifier();
    static QString stopReasonKey(AgentStopReason reason);
    static Result<AiTask> prepareTaskSchedule(AiTask task, const QDateTime& nowUtc);
    static Result<TaskSchedule> advanceSchedule(TaskSchedule schedule, const QDateTime& triggeredAtUtc);
    static QVector<ToolCall> toolCallsFromDocument(const QJsonArray& calls);
    static QJsonArray toolCallsDocument(const QVector<ToolCall>& calls);
};

QString AiPluginHelper::identifier() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QVector<ToolCall> AiPluginHelper::toolCallsFromDocument(const QJsonArray& calls) {
    QVector<ToolCall> parsed;
    parsed.reserve(calls.size());

    for (const auto& value : calls) {
        const QJsonObject call = value.toObject();
        parsed.append({call.value(QStringLiteral("id")).toString(), call.value(QStringLiteral("name")).toString(), call.value(QStringLiteral("arguments")).toObject()});
    }

    return parsed;
}

QJsonArray AiPluginHelper::toolCallsDocument(const QVector<ToolCall>& calls) {
    QJsonArray document;

    for (const auto& call : calls) {
        document.append(QJsonObject{{QStringLiteral("id"), call.id}, {QStringLiteral("name"), call.name}, {QStringLiteral("arguments"), call.arguments}});
    }

    return document;
}

QString AiPluginHelper::stopReasonKey(AgentStopReason reason) {
    return QStringLiteral("ai.stop-reason.") + AiTaskRepository::agentStopReasonName(reason);
}

Result<AiTask> AiPluginHelper::prepareTaskSchedule(AiTask task, const QDateTime& nowUtc) {
    if (!task.schedule.has_value()) {
        return Result<AiTask>::success(std::move(task));
    }

    auto& schedule = task.schedule.value();
    schedule.enabled = true;
    schedule.lastTriggeredAtUtc = {};

    if (schedule.kind == ScheduleKind::Once) {
        schedule.nextRunAtUtc = schedule.onceAtUtc;
        if (!schedule.onceAtUtc.isValid() || schedule.onceAtUtc.timeSpec() != Qt::UTC || schedule.onceAtUtc <= nowUtc) {
            return Result<AiTask>::failure({"ai_tasks_schedule_once_past", "The one-time schedule must be in the future", {}});
        }
    }

    if (schedule.kind == ScheduleKind::Interval) {
        schedule.nextRunAtUtc = nowUtc.addSecs(schedule.intervalSeconds);
    }

    if (schedule.kind == ScheduleKind::Cron) {
        const auto expression = CronExpression::parse(schedule.cronExpression);
        if (!expression.hasValue()) {
            return Result<AiTask>::failure(expression.error());
        }
        const auto next = expression.value().nextAfter(nowUtc, QTimeZone(schedule.timeZoneId));
        if (!next.hasValue()) {
            return Result<AiTask>::failure(next.error());
        }
        schedule.nextRunAtUtc = next.value();
    }

    const auto validation = AiTaskRepository::validateSchedule(schedule);
    return validation.hasValue() ? Result<AiTask>::success(std::move(task)) : Result<AiTask>::failure(validation.error());
}

Result<TaskSchedule> AiPluginHelper::advanceSchedule(TaskSchedule schedule, const QDateTime& triggeredAtUtc) {
    schedule.lastTriggeredAtUtc = triggeredAtUtc;

    if (schedule.kind == ScheduleKind::Once) {
        schedule.enabled = false;
        schedule.nextRunAtUtc = {};
        return Result<TaskSchedule>::success(std::move(schedule));
    }

    if (schedule.kind == ScheduleKind::Interval) {
        do {
            schedule.nextRunAtUtc = schedule.nextRunAtUtc.addSecs(schedule.intervalSeconds);
        } while (schedule.nextRunAtUtc <= triggeredAtUtc);
        return Result<TaskSchedule>::success(std::move(schedule));
    }

    const auto expression = CronExpression::parse(schedule.cronExpression);

    if (!expression.hasValue()) {
        return Result<TaskSchedule>::failure(expression.error());
    }

    const auto next = expression.value().nextAfter(triggeredAtUtc, QTimeZone(schedule.timeZoneId));

    if (!next.hasValue()) {
        return Result<TaskSchedule>::failure(next.error());
    }

    schedule.nextRunAtUtc = next.value();
    return Result<TaskSchedule>::success(std::move(schedule));
}

// clang-format off
AiPlugin::AiPlugin() : AiPlugin([](AiRequestGate& gate, const ModelConnection& connection) {
    const ProviderDescriptor* provider = ProviderCatalog::findProvider(connection.providerId);
    if (provider != nullptr && provider->protocol == WireProtocol::CommandLine) {
        return std::unique_ptr<AiChatClient>(new AiCliChatClient());
    }
    return std::unique_ptr<AiChatClient>(new AiHttpChatClient(gate));
}) {}
// clang-format on

AiPlugin::AiPlugin(ChatClientFactory clientFactory) : m_clientFactory(std::move(clientFactory)) {}

// A client is a child, and a child is destroyed after the members it reaches, so one still waiting for its deferred deletion would withdraw from a gate that is already gone.
AiPlugin::~AiPlugin() {
    qDeleteAll(findChildren<AiChatClient*>(Qt::FindDirectChildrenOnly));
}

QString AiPlugin::id() const {
    return QStringLiteral("ai");
}

QString AiPlugin::titleKey() const {
    return QStringLiteral("ai.plugin.title");
}

QStringList AiPlugin::dependencies() const {
    return {QStringLiteral("logs")};
}

int AiPlugin::databaseSchemaVersion() const {
    return 2;
}

TranslationCatalog AiPlugin::translations() const {
    return translations::AiCatalog::catalog();
}

QString AiPlugin::styleSheet(const ui::Theme&) const {
    return QStringLiteral("QTextBrowser#aiConversationContent { background: transparent; border: none; } QLabel#aiConversationToolName { font-weight: 600; } QWidget#aiConversationTool { border-radius: @controlRadiuspx; } QTextBrowser#aiConversationToolDetail { background: transparent; border: none; font-family: @monospaceFamily; font-size: @captionFontSizept; } QPushButton#aiConversationSend { background: @accent; border: none; padding: 0px; min-width: @roundButtonSizepx; max-width: @roundButtonSizepx; min-height: @roundButtonSizepx; max-height: @roundButtonSizepx; border-radius: @roundButtonRadiuspx; } QPushButton#aiConversationSend:hover { background: @accentHover; } QPushButton#aiConversationSend:disabled { background: @borderStrong; } QScrollArea#aiConversationScroll, QWidget#aiConversationMessages { background: @terminal; border: none; } workpane--ui--TextField#aiConversationComposer { border-radius: @badgeRadiuspx; padding: @controlVerticalPaddingpx @controlHorizontalPaddingpx; } QFrame#aiTaskCard { background: @raised; border: 1px solid @border; border-radius: 3px; } QFrame#aiTaskCard[running=\"true\"] { border-color: @success; } QFrame#aiTaskCard[waiting=\"true\"] { border-color: @warning; } QLabel#aiTaskTitle { font-weight: 600; } QLabel#aiTaskError { color: @dangerText; background: @dangerBackground; border-left: 2px solid @danger; padding: 5px 7px; } QLabel#aiTaskValidation { color: @dangerText; background: @dangerBackground; border-left: 2px solid @danger; padding: 9px 14px; } QLabel#aiTaskBadge { border-radius: @badgeRadiuspx; padding: @badgeVerticalPaddingpx @badgeHorizontalPaddingpx; font-size: @interfaceFontSizept; font-weight: 600; color: @text; background: @borderStrong; } QLabel#aiTaskBadge[badge=\"queued\"] { color: @onAccent; background: @warning; } QLabel#aiTaskBadge[badge=\"running\"] { color: @onAccent; background: @accent; } QLabel#aiTaskBadge[badge=\"succeeded\"] { color: @onAccent; background: @success; } QLabel#aiTaskBadge[badge=\"failed\"] { color: @onAccent; background: @danger; } QLabel#aiTaskBadge[badge=\"cancelled\"] { color: @textMuted; background: @hover; } QLabel#aiTaskBadge[badge=\"scheduled\"] { color: @onAccent; background: @information; } QLabel#aiTaskBadge[badge=\"stopped\"] { color: @onAccent; background: @warning; } QLabel#aiColumnTitle { font-weight: 600; } QTextBrowser#aiExecutionContent, QPlainTextEdit#aiPayloadContent { padding: @controlVerticalPaddingpx @controlHorizontalPaddingpx; }");
}

QVector<NavigationItem> AiPlugin::navigationItems(const ui::Theme& theme) const {
    return {{QStringLiteral("tasks"), QStringLiteral("ai.navigation.tasks"), ui::IconCatalog::icon(ui::IconName::Tasks, theme), NavigationPlacement::Primary, NavigationOrder::Board}};
}

QVector<SettingsGroup> AiPlugin::settingsGroups() const {
    const SettingsSection connections{QStringLiteral("general"), QStringLiteral("ai.settings.connections"), {QStringLiteral("ai.settings.provider"), QStringLiteral("ai.settings.model"), QStringLiteral("ai.settings.api-key"), QStringLiteral("ai.settings.default-connection")}};
    const SettingsSection providerScope{QStringLiteral("selection"), QStringLiteral("ai.settings.provider-selection"), {QStringLiteral("ai.settings.provider-scope"), QStringLiteral("ai.settings.provider")}};
    const SettingsSection rateLimits{QStringLiteral("rate-limits"), QStringLiteral("ai.settings.rate-limits"), {QStringLiteral("ai.settings.rate-limit-interval"), QStringLiteral("ai.settings.rate-limit-per-minute"), QStringLiteral("ai.settings.rate-limit-concurrent")}};
    const SettingsSection agents{QStringLiteral("general"), QStringLiteral("ai.settings.agents"), {QStringLiteral("ai.agent.identifier"), QStringLiteral("ai.agent.name"), QStringLiteral("ai.agent.connection"), QStringLiteral("ai.agent.system-prompt")}};
    const SettingsSection speech{QStringLiteral("speech"), QStringLiteral("ai.settings.speech"), {QStringLiteral("ai.settings.speech-provider"), QStringLiteral("ai.settings.speech-voice"), QStringLiteral("ai.settings.speech-key")}};
    const SettingsSection search{QStringLiteral("search"), QStringLiteral("ai.settings.search"), {QStringLiteral("ai.settings.search-provider"), QStringLiteral("ai.settings.search-instance"), QStringLiteral("ai.settings.search-key")}};
    const SettingsSection mcp{QStringLiteral("mcp"), QStringLiteral("ai.settings.mcp"), {QStringLiteral("ai.mcp.identifier"), QStringLiteral("ai.mcp.transport"), QStringLiteral("ai.mcp.command"), QStringLiteral("ai.mcp.address")}};
    const SettingsSection execution{QStringLiteral("general"), QStringLiteral("ai.settings.execution"), {QStringLiteral("ai.settings.maximum-iterations"), QStringLiteral("ai.settings.command-timeout"), QStringLiteral("ai.settings.parallel-limit")}};

    return {{QStringLiteral("connections"), QStringLiteral("ai.settings.group-connections"), {connections}}, {QStringLiteral("providers"), QStringLiteral("ai.settings.group-providers"), {providerScope, rateLimits}}, {QStringLiteral("agents"), QStringLiteral("ai.settings.group-agents"), {agents}}, {QStringLiteral("tools"), QStringLiteral("ai.settings.group-tools"), {mcp, search, speech}}, {QStringLiteral("general"), QStringLiteral("ai.settings.group-general"), {execution}}};
}

Result<void> AiPlugin::initialize(PluginHost& host) {
    if (m_host != nullptr) {
        return Result<void>::failure({"ai_tasks_already_initialized", "The AI plugin is already initialized", {}});
    }

    m_host = &host;
    m_asyncContext = std::make_unique<QObject>();
    m_repository = std::make_unique<AiTaskRepository>(host);
    m_tools = std::make_unique<AiToolRegistry>(host);
    const auto migration = m_repository->initialize();

    if (!migration.hasValue()) {
        shutdown();
        return migration;
    }

    const AiSettings settings = m_repository->settings();

    if (const auto& catalogError = ProviderCatalog::aiCatalogError(); !catalogError.hasValue()) {
        shutdown();
        return catalogError;
    }

    m_settings = settings;
    m_committedSettings = m_settings;
    m_gate.setLimits(m_settings.rateLimits);
    refreshToolConfiguration();
    restartMcpClients();

    const auto state = reloadState();

    if (!state.hasValue()) {
        shutdown();
        return state;
    }

    m_scheduleTimer.setSingleShot(true);
    // clang-format off
    connect(&m_scheduleTimer, &QChronoTimer::timeout, this, [this]() { processSchedules(); });
    // clang-format on
    armScheduleTimer();

    const auto capability = host.provideCapability({QString::fromLatin1(startTaskCapability)});

    if (!capability.hasValue()) {
        shutdown();
        return capability;
    }

    // The queue survives a restart, so what was waiting when the application closed is dispatched as soon as it is loaded.
    dispatchQueue();
    return Result<void>::success();
}

void AiPlugin::stepChatFontSize(ui::ContentFontStep step) {
    ExecutionSettings stepped = m_settings.execution;
    stepped.chatFontSize = step == ui::ContentFontStep::Reset ? defaultChatFontSize : ui::ContentFontSizes::steppedContentFontSize(stepped.chatFontSize, step == ui::ContentFontStep::Increase ? 1 : -1);
    auto future = saveExecutionSettings(stepped);
    // clang-format off
    future.then(this, [this](Result<void> result) { if (!result.hasValue()) { reportFailure(result.error(), m_host->translate(QStringLiteral("ai.error.settings-save"))); } });
    // clang-format on
}

QWidget* AiPlugin::createNavigationView(const QString& itemId, QWidget* parent) {
    if (m_host == nullptr) {
        return nullptr;
    }
    if (itemId == QStringLiteral("tasks")) {
        auto* view = new AiTasksView(*this, *m_host, parent);
        // clang-format off
        ui::ApplicationShortcuts::installContentFontShortcuts(view, [this](ui::ContentFontStep step) { stepChatFontSize(step); });
        // clang-format on
        return view;
    }

    return nullptr;
}

QWidget* AiPlugin::createSettingsSection(const QString& groupId, const QString& sectionId, QWidget* parent) {
    if (m_host == nullptr) {
        return nullptr;
    }
    if (groupId == QStringLiteral("connections")) {
        return sectionId == QStringLiteral("general") ? new AiConnectionSettingsView(*this, *m_host, parent) : nullptr;
    }

    if (groupId == QStringLiteral("providers")) {
        if (sectionId == QStringLiteral("selection")) {
            return createProviderSelectionSection(parent);
        }
        return sectionId == QStringLiteral("rate-limits") ? new AiRateLimitSettingsView(*this, *m_host, m_providerScope, parent) : nullptr;
    }

    if (groupId == QStringLiteral("agents")) {
        return sectionId == QStringLiteral("general") ? new AiAgentSettingsView(*this, *m_host, parent) : nullptr;
    }

    if (groupId == QStringLiteral("tools")) {
        if (sectionId == QStringLiteral("mcp")) {
            return new AiMcpSettingsView(*this, *m_host, parent);
        }
        if (sectionId == QStringLiteral("search")) {
            return new AiSearchSettingsView(*this, *m_host, parent);
        }
        return sectionId == QStringLiteral("speech") ? new AiSpeechSettingsView(*this, *m_host, parent) : nullptr;
    }

    if (groupId != QStringLiteral("general") || sectionId != QStringLiteral("general")) {
        return nullptr;
    }

    return createExecutionSection(parent);
}

// The selector governs every per-provider section of this group, so each of them reads the scope instead of asking again.
QWidget* AiPlugin::createProviderSelectionSection(QWidget* parent) {
    const auto [page, layout] = ui::Components::settingsSectionPage(parent);
    auto* form = ui::Components::settingsForm();
    auto* provider = new ui::ComboBox(m_host->theme(), page);
    provider->setObjectName(QStringLiteral("aiScopeProvider"));

    for (const auto* descriptor : ModelConnections::providersAnswering(ModelEndpoint::Chat)) {
        provider->addItem(m_host->translate(descriptor->titleKey), descriptor->id);
    }

    ui::Components::sortComboBoxItems(provider);
    const int selected = provider->findData(m_providerScope.providerId());
    provider->setCurrentIndex(selected < 0 ? 0 : selected);
    ui::Components::addSettingsRow(form, m_host->translate(QStringLiteral("ai.settings.provider-scope")), provider);
    layout->addLayout(form);
    layout->addStretch(1);
    m_providerScope.setProviderId(provider->currentData().toString());

    // clang-format off
    connect(provider, &QComboBox::currentIndexChanged, page, [this, provider]() { m_providerScope.setProviderId(provider->currentData().toString()); });
    // clang-format on
    return page;
}

QWidget* AiPlugin::createExecutionSection(QWidget* parent) {
    const auto [page, layout] = ui::Components::settingsSectionPage(parent);
    auto* form = ui::Components::settingsForm();
    auto* iterations = new QSpinBox(page);
    iterations->setObjectName(QStringLiteral("aiMaximumIterations"));
    iterations->setRange(0, ProviderCatalog::aiLimits().maximumAgentIterations);
    iterations->setValue(m_settings.execution.maximumIterations);
    auto* timeout = new QSpinBox(page);
    timeout->setObjectName(QStringLiteral("aiCommandTimeout"));
    timeout->setRange(0, ProviderCatalog::aiLimits().maximumCommandTimeoutSeconds);
    timeout->setValue(m_settings.execution.commandTimeoutSeconds);
    auto* chatFont = new QSpinBox(page);
    chatFont->setObjectName(QStringLiteral("aiChatFontSize"));
    chatFont->setRange(ui::minimumContentFontSize, ui::maximumContentFontSize);
    chatFont->setValue(m_settings.execution.chatFontSize);
    auto* parallel = new QSpinBox(page);
    parallel->setObjectName(QStringLiteral("aiParallelLimit"));
    parallel->setRange(0, ProviderCatalog::aiLimits().maximumParallelExecutions);
    parallel->setValue(m_settings.execution.parallelExecutions);
    ui::Components::addSettingsRow(form, m_host->translate(QStringLiteral("ai.settings.maximum-iterations")), ui::Components::stepperRow(iterations, m_host->theme(), page));
    ui::Components::addSettingsRow(form, m_host->translate(QStringLiteral("ai.settings.command-timeout")), ui::Components::stepperRow(timeout, m_host->theme(), page));
    ui::Components::addSettingsRow(form, m_host->translate(QStringLiteral("ai.settings.parallel-limit")), ui::Components::stepperRow(parallel, m_host->theme(), page));
    ui::Components::addSettingsRow(form, m_host->translate(QStringLiteral("ai.settings.chat-font-size")), ui::Components::stepperRow(chatFont, m_host->theme(), page));
    layout->addLayout(form);
    layout->addStretch(1);

    // clang-format off
    const auto persist = [this, iterations, timeout, parallel, chatFont]() { auto future = saveExecutionSettings({iterations->value(), timeout->value(), parallel->value(), chatFont->value()}); future.then(iterations, [this](Result<void> result) { if (!result.hasValue()) { reportFailure(result.error(), m_host->translate(QStringLiteral("ai.error.settings-save"))); } }); };
    connect(iterations, &QSpinBox::valueChanged, page, persist);
    connect(timeout, &QSpinBox::valueChanged, page, persist);
    connect(parallel, &QSpinBox::valueChanged, page, persist);
    connect(chatFont, &QSpinBox::valueChanged, page, persist);
    // clang-format on
    return page;
}

void AiPlugin::handleRequest(const QString&, const QString& topic, const QJsonObject& payload, PluginReply reply) {
    if (topic == QString::fromLatin1(startTaskCapability) && SettingsReaders::hasExactKeys(payload, {QStringLiteral("taskId")}) && payload.value(QStringLiteral("taskId")).isString()) {
        auto future = startTask(payload.value(QStringLiteral("taskId")).toString());
        // clang-format off
        future.then(m_asyncContext.get(), [reply](Result<void> result) { reply(result.hasValue() ? Result<QJsonObject>::success({}) : Result<QJsonObject>::failure(result.error())); });
        // clang-format on
        return;
    }

    reply(SettingsReaders::unhandledTopic(topic));
}

void AiPlugin::shutdown() {
    m_scheduleTimer.stop();
    QStringList unfinishedCalls;

    for (auto& execution : m_active) {
        for (const auto& pending : execution->toolCalls) {
            if (pending.deadline != nullptr) {
                pending.deadline->stop();
                pending.deadline->deleteLater();
            }
            if (pending.started && !pending.finished) {
                unfinishedCalls.append(pending.call.id);
            }
        }
        if (execution->summaryClient != nullptr) {
            execution->summaryClient->disconnect(this);
            execution->summaryClient->cancel();
            execution->summaryClient->deleteLater();
        }
        if (execution->client != nullptr) {
            execution->client->disconnect(this);
            execution->client->cancel();
            execution->client->deleteLater();
        }
        if (execution->runner != nullptr) {
            execution->runner->disconnect(this);
            execution->runner->cancel();
            execution->runner->deleteLater();
        }
    }

    m_active.clear();

    // The answer of a cancelled command reaches an execution that is already gone, so the runs are cleared before the commands are stopped.
    for (const auto& callId : unfinishedCalls) {
        if (m_tools != nullptr) {
            m_tools->cancel(callId);
        }
    }

    for (auto* client : m_mcpClients) {
        client->disconnect(this);
        client->stop();
        client->deleteLater();
    }

    m_mcpClients.clear();
    // A transport thread outlives the client that asked it to end, so this waits for the ones still finishing before the library they run is unloaded.
    agent::mcp::McpClient::drainTransports();
    m_settings.mcpServers.clear();
    m_asyncContext.reset();
    m_queue.clear();
    m_cancelledTaskIds.clear();
    m_lastErrors.clear();
    m_lastStatuses.clear();
    m_lastStopReasons.clear();
    m_phases.clear();
    m_phasesBeforeThrottle.clear();
    m_conversations.clear();
    m_conversationSequences.clear();
    m_loadedConversations.clear();
    m_logSequences.clear();
    m_tasks.clear();
    m_workspaces.clear();
    m_settings.execution.parallelExecutions = 0;
    m_tools.reset();
    m_repository.reset();
    m_host = nullptr;
}

PluginHost& AiPlugin::host() const {
    return *m_host;
}

const QVector<AiWorkspace>& AiPlugin::workspaces() const {
    return m_workspaces;
}

const QVector<AiTask>& AiPlugin::tasks() const {
    return m_tasks;
}

TaskRunState AiPlugin::runState(const QString& taskId) const {
    if (m_active.contains(taskId)) {
        return TaskRunState::Running;
    }

    return m_queue.contains(taskId) ? TaskRunState::Waiting : TaskRunState::Idle;
}

ExecutionStatus AiPlugin::lastExecutionStatus(const QString& taskId) const {
    return m_lastStatuses.value(taskId, ExecutionStatus::Running);
}

AgentStopReason AiPlugin::lastStopReason(const QString& taskId) const {
    return m_lastStopReasons.value(taskId, AgentStopReason::Answered);
}

bool AiPlugin::hasLastExecution(const QString& taskId) const {
    return m_lastStatuses.contains(taskId);
}

QString AiPlugin::lastError(const QString& taskId) const {
    return m_lastErrors.value(taskId);
}

QString AiPlugin::phaseName(ExecutionPhase phase) {
    switch (phase) {
    case ExecutionPhase::Idle:
        return QStringLiteral("idle");
    case ExecutionPhase::Queued:
        return QStringLiteral("queued");
    case ExecutionPhase::Throttled:
        return QStringLiteral("throttled");
    case ExecutionPhase::Sending:
        return QStringLiteral("sending");
    case ExecutionPhase::Streaming:
        return QStringLiteral("streaming");
    case ExecutionPhase::Compacting:
        return QStringLiteral("compacting");
    case ExecutionPhase::CallingTool:
        return QStringLiteral("calling-tool");
    case ExecutionPhase::Running:
        return QStringLiteral("running");
    }

    return QStringLiteral("idle");
}

ExecutionPhase AiPlugin::executionPhase(const QString& taskId) const {
    if (const auto phase = m_phases.constFind(taskId); phase != m_phases.constEnd()) {
        return phase.value();
    }

    return m_queue.contains(taskId) ? ExecutionPhase::Queued : ExecutionPhase::Idle;
}

// The card says what the run is doing, so a turn calling tools names them instead of saying only that a tool is running.
QString AiPlugin::executionDetail(const QString& taskId) const {
    const std::shared_ptr<ActiveExecution> position = m_active.value(taskId);

    if (position == nullptr || m_host == nullptr) {
        return {};
    }

    QStringList running;

    for (const auto& pending : position->toolCalls) {
        if (pending.started && !pending.finished) {
            running.append(pending.call.name);
        }
    }

    if (running.size() == 1) {
        return running.first();
    }

    return running.isEmpty() ? QString{} : m_host->translate(QStringLiteral("ai.phase.tool-count")).arg(QString::number(running.size()));
}

const QVector<ModelConnection>& AiPlugin::connections() const {
    return m_settings.connections;
}

const QString& AiPlugin::defaultConnectionKey() const {
    return m_settings.defaultConnectionKey;
}

std::optional<ModelConnection> AiPlugin::defaultConnection() const {
    const ModelConnection* connection = ModelConnections::findConnection(m_settings.connections, m_settings.defaultConnectionKey);
    return connection == nullptr ? std::nullopt : std::optional<ModelConnection>(*connection);
}

// A task is handed to an agent, so an agent that was removed fails the run with its own name instead of silently running on another one.
Result<AiAgent> AiPlugin::agentForTask(const AiTask& task) const {
    const AiAgent* agent = TaskContracts::findAgent(m_settings.agents, task.agentId);

    if (agent == nullptr) {
        return Result<AiAgent>::failure({"ai_agent_unknown", "The agent this task runs on is not configured", task.agentId});
    }

    return Result<AiAgent>::success(*agent);
}

Result<ModelConnection> AiPlugin::connectionForAgent(const AiAgent& agent) const {
    const ModelConnection* connection = ModelConnections::findConnection(m_settings.connections, agent.connectionKey);

    if (connection == nullptr) {
        return Result<ModelConnection>::failure({"ai_connection_unknown", "The connection this agent runs on is not configured", agent.connectionKey});
    }

    return ModelConnections::validateConnection(*connection);
}

int AiPlugin::parallelExecutions() const {
    return m_settings.execution.parallelExecutions;
}

const ExecutionSettings& AiPlugin::executionSettings() const {
    return m_settings.execution;
}

SpeechSettings AiPlugin::effectiveSpeechSettings() const {
    SpeechSettings effective = m_settings.speech;

    if (effective.apiKey.isEmpty()) {
        effective.apiKey = TaskContracts::declaredSpeechSettings(effective.providerId).apiKey;
    }

    if (effective.voiceId.isEmpty()) {
        effective.voiceId = TaskContracts::declaredSpeechSettings(effective.providerId).voiceId;
    }

    return effective;
}

QFuture<Result<void>> AiPlugin::persistSettings(AiSettings next) {
    if (m_repository == nullptr) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_tasks_not_initialized", "The AI plugin is not initialized", {}}));
    }

    m_settings = std::move(next);

    const AiSettings candidate = m_settings;
    const quint64 revision = ++m_settingsRevision;
    auto future = m_repository->saveSettings(candidate);
    // clang-format off
    return future.then(m_asyncContext.get(), [this, candidate, revision](Result<void> result) {
        if (result.hasValue()) {
            if (revision > m_committedSettingsRevision) {
                m_committedSettings = candidate;
                m_committedSettingsRevision = revision;
            }
        } else if (revision == m_settingsRevision) {
            m_settings = m_committedSettings;
        }
        m_gate.setLimits(m_settings.rateLimits);
        refreshToolConfiguration();
        return result;
    });
    // clang-format on
}

QFuture<Result<void>> AiPlugin::saveSpeechSettings(const SpeechSettings& settings) {
    AiSettings next = m_settings;
    next.speech = settings;
    return persistSettings(std::move(next));
}

QFuture<Result<void>> AiPlugin::saveExecutionSettings(const ExecutionSettings& settings) {
    if (settings.parallelExecutions < 0) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_tasks_parallel_limit_invalid", "The parallel execution limit is invalid", QString::number(settings.parallelExecutions)}));
    }

    AiSettings next = m_settings;
    next.execution = settings;
    auto future = persistSettings(std::move(next));
    // clang-format off
    return future.then(m_asyncContext.get(), [this](Result<void> result) { if (result.hasValue()) { emit executionSettingsChanged(); dispatchQueue(); } return result; });
    // clang-format on
}

QFuture<Result<QVector<TaskExecution>>> AiPlugin::executions(const QString& taskId) {
    if (m_repository == nullptr) {
        return QtFuture::makeReadyValueFuture(Result<QVector<TaskExecution>>::failure({"ai_tasks_not_initialized", "The AI plugin is not initialized", {}}));
    }

    return m_repository->executions(taskId);
}

QFuture<Result<QVector<ExecutionLogEntry>>> AiPlugin::executionLogs(const QString& executionId) {
    if (m_repository == nullptr) {
        return QtFuture::makeReadyValueFuture(Result<QVector<ExecutionLogEntry>>::failure({"ai_tasks_not_initialized", "The AI plugin is not initialized", {}}));
    }

    return m_repository->executionLogs(executionId);
}

// Editing a connection is one act, so every agent that ran on it follows to the key it now carries.
QFuture<Result<void>> AiPlugin::replaceConnection(const QString& previousKey, const ModelConnection& connection) {
    if (ModelConnections::findConnection(m_settings.connections, previousKey) == nullptr) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_connection_unknown", "The AI connection is not configured", previousKey}));
    }

    const QString nextKey = ModelConnections::connectionKey(connection);
    QVector<ModelConnection> connections = m_settings.connections;

    for (auto& stored : connections) {
        if (ModelConnections::connectionKey(stored) == previousKey) {
            stored = connection;
        }
    }

    const auto validated = ModelConnections::validateConnectionSet(connections);

    if (!validated.hasValue()) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure(validated.error()));
    }

    AiSettings next = m_settings;
    next.connections = connections;
    next.defaultConnectionKey = m_settings.defaultConnectionKey == previousKey ? nextKey : m_settings.defaultConnectionKey;

    for (auto& agent : next.agents) {
        if (agent.connectionKey == previousKey) {
            agent.connectionKey = nextKey;
        }
    }

    auto future = persistSettings(std::move(next));
    // clang-format off
    return future.then(m_asyncContext.get(), [this](Result<void> result) { if (result.hasValue()) { dispatchQueue(); } return result; });
    // clang-format on
}

QFuture<Result<void>> AiPlugin::saveConnections(const QVector<ModelConnection>& connections, const QString& defaultConnectionKey) {
    const auto validated = ModelConnections::validateConnectionSet(connections);

    if (!validated.hasValue()) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure(validated.error()));
    }
    if (!defaultConnectionKey.isEmpty() && ModelConnections::findConnection(connections, defaultConnectionKey) == nullptr) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_connection_unknown", "The default AI connection is not configured", defaultConnectionKey}));
    }

    // A connection an agent runs on is refused while that agent exists, because the agent would be left naming nothing.
    for (const auto& agent : m_settings.agents) {
        if (ModelConnections::findConnection(connections, agent.connectionKey) == nullptr) {
            return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_connection_in_use", "The connection is used by an agent", agent.name}));
        }
    }

    AiSettings next = m_settings;
    next.connections = connections;
    next.defaultConnectionKey = defaultConnectionKey;
    auto future = persistSettings(std::move(next));
    // clang-format off
    return future.then(m_asyncContext.get(), [this](Result<void> result) { if (result.hasValue()) { dispatchQueue(); } return result; });
    // clang-format on
}

// The pace belongs to the service, so every connection and every workspace that reaches it waits behind the same limit.
QFuture<Result<void>> AiPlugin::saveRateLimits(const QVector<ProviderRateLimit>& limits) {
    QSet<QString> providers;

    for (const auto& limit : limits) {
        if (ProviderCatalog::findProvider(limit.providerId) == nullptr) {
            return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_rate_limit_invalid", "The provider is not supported", limit.providerId}));
        }
        if (providers.contains(limit.providerId)) {
            return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_rate_limit_invalid", "The provider rate limit is declared twice", limit.providerId}));
        }
        if (limit.minimumIntervalMs < 0 || limit.minimumIntervalMs > ProviderCatalog::aiLimits().maximumRequestDelayMs || limit.maximumRequestsPerMinute < 0 || limit.maximumRequestsPerMinute > ProviderCatalog::aiLimits().maximumRequestsPerMinute || limit.maximumConcurrentRequests < 0 || limit.maximumConcurrentRequests > ProviderCatalog::aiLimits().maximumConcurrentRequests) {
            return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_rate_limit_invalid", "The provider rate limit is out of range", limit.providerId}));
        }
        providers.insert(limit.providerId);
    }

    AiSettings next = m_settings;
    next.rateLimits = limits;
    return persistSettings(std::move(next));
}

QVector<ConversationMessage> AiPlugin::conversation(const QString& taskId) const {
    return m_conversations.value(taskId);
}

// The conversation is read once and kept, because a turn that had to wait for storage before speaking would stall the card.
QFuture<Result<void>> AiPlugin::loadConversation(const QString& taskId) {
    if (m_repository == nullptr) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_tasks_not_initialized", "The AI plugin is not initialized", {}}));
    }
    if (m_loadedConversations.contains(taskId)) {
        return QtFuture::makeReadyValueFuture(Result<void>::success());
    }

    auto future = m_repository->conversation(taskId, 0, conversationPageSize);
    // clang-format off
    const auto loaded = [this, taskId](Result<QVector<ConversationMessage>> result) {
        if (!result.hasValue()) {
            return Result<void>::failure(result.error());
        }
        m_conversations.insert(taskId, result.value());
        m_loadedConversations.insert(taskId);
        emit conversationChanged(taskId);
        return Result<void>::success();
    };
    // clang-format on
    return future.then(m_asyncContext.get(), loaded);
}

// A reader scrolling up asks for the page before the oldest one on screen, which answers whether there was one.
QFuture<Result<bool>> AiPlugin::loadOlderConversation(const QString& taskId) {
    const QVector<ConversationMessage>& loaded = m_conversations[taskId];

    if (m_repository == nullptr || loaded.isEmpty()) {
        return QtFuture::makeReadyValueFuture(Result<bool>::success(false));
    }

    auto future = m_repository->conversation(taskId, loaded.first().sequence, conversationPageSize);
    // clang-format off
    const auto prepended = [this, taskId](Result<QVector<ConversationMessage>> result) {
        if (!result.hasValue()) {
            return Result<bool>::failure(result.error());
        }
        if (result.value().isEmpty()) {
            return Result<bool>::success(false);
        }
        m_conversations[taskId] = result.value() + m_conversations.value(taskId);
        emit conversationChanged(taskId);
        return Result<bool>::success(true);
    };
    // clang-format on
    return future.then(m_asyncContext.get(), prepended);
}

ConversationMessage AiPlugin::buildMessage(const QString& taskId, ConversationRole role, const QString& content, const QJsonArray& toolCalls, const QString& toolCallId) {
    ConversationMessage message;
    message.id = AiPluginHelper::identifier();
    message.taskId = taskId;
    message.sequence = ++m_conversationSequences[taskId];
    message.role = role;
    message.content = content;
    message.toolCalls = toolCalls;
    message.toolCallId = toolCallId;
    message.createdAtUtc = QDateTime::currentDateTimeUtc();
    return message;
}

// The conversation is the source of truth, so a turn that could not be written is taken back and the run that produced it stops.
QFuture<Result<void>> AiPlugin::recordConversation(const QString& taskId, const QVector<ConversationMessage>& messages) {
    if (messages.isEmpty()) {
        return QtFuture::makeReadyValueFuture(Result<void>::success());
    }

    m_conversations[taskId].append(messages);
    emit conversationChanged(taskId);

    auto future = m_repository->appendConversation(messages);
    // clang-format off
    const auto written = [this, taskId, messages](Result<void> result) {
        if (result.hasValue()) {
            return result;
        }
        QVector<ConversationMessage>& conversation = m_conversations[taskId];
        for (const auto& message : messages) {
            conversation.removeIf([&message](const ConversationMessage& stored) { return stored.id == message.id; });
        }
        const QString reason = m_host->translate(QStringLiteral("ai.error.conversation-save"));
        emit conversationChanged(taskId);
        reportFailure(result.error(), reason);
        if (m_active.contains(taskId)) {
            m_lastErrors.insert(taskId, reason);
            completeExecution(taskId, ExecutionStatus::Failed, reason, AgentStopReason::Failed);
            return result;
        }
        m_lastErrors.insert(taskId, reason);
        m_lastStatuses.insert(taskId, ExecutionStatus::Failed);
        m_lastStopReasons.insert(taskId, AgentStopReason::Failed);
        emit taskRunStateChanged(taskId);
        return result;
    };
    // clang-format on
    return future.then(m_asyncContext.get(), written);
}

// A message typed while a turn is running joins the conversation at once and is claimed at the next iteration of that turn.
QFuture<Result<void>> AiPlugin::sendMessage(const QString& taskId, const QString& text) {
    const auto* target = task(taskId);

    if (m_repository == nullptr || target == nullptr || text.trimmed().isEmpty()) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_conversation_invalid", "The message is invalid", taskId}));
    }
    if (target->executionKind != TaskExecutionKind::Agent) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_conversation_invalid", "A command task holds no conversation", taskId}));
    }

    auto future = loadConversation(taskId);
    // clang-format off
    const auto queued = [this, taskId, text](Result<void> result) {
        if (!result.hasValue()) {
            return result;
        }
        if (runState(taskId) == TaskRunState::Idle) {
            auto written = recordConversation(taskId, {buildMessage(taskId, ConversationRole::User, text.trimmed(), {}, {})});
            // clang-format off
            auto started = written.then(m_asyncContext.get(), [this, taskId](Result<void> stored) { return stored.hasValue() ? enqueueRun(taskId) : QtFuture::makeReadyValueFuture(stored); }).unwrap();
            started.then(m_asyncContext.get(), [](Result<void>) {});
            // clang-format on
            return Result<void>::success();
        }
        std::ignore = recordConversation(taskId, {buildMessage(taskId, ConversationRole::User, text.trimmed(), {}, {})});
        return Result<void>::success();
    };
    // clang-format on
    return future.then(m_asyncContext.get(), queued);
}

QFuture<Result<void>> AiPlugin::resetConversation(const QString& taskId) {
    if (m_repository == nullptr || task(taskId) == nullptr) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_tasks_task_unknown", "The AI task is unknown", taskId}));
    }
    if (runState(taskId) != TaskRunState::Idle) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_tasks_task_busy", "The AI task is already queued or running", taskId}));
    }

    auto future = m_repository->clearConversation(taskId);
    // clang-format off
    const auto cleared = [this, taskId](Result<void> result) {
        if (!result.hasValue()) {
            return result;
        }
        m_conversations.insert(taskId, {});
        m_conversationSequences.insert(taskId, 0);
        m_loadedConversations.insert(taskId);
        m_lastStatuses.remove(taskId);
        m_lastStopReasons.remove(taskId);
        m_lastErrors.remove(taskId);
        emit conversationChanged(taskId);
        emit taskRunStateChanged(taskId);
        return result;
    };
    // clang-format on
    return future.then(m_asyncContext.get(), cleared);
}

QVector<AiAgent> AiPlugin::agents() const {
    return m_settings.agents;
}

// An agent that is removed stops the tasks it was running, because a task without its specialist has nobody to answer it.
QFuture<Result<void>> AiPlugin::saveAgents(const QVector<AiAgent>& agents) {
    const auto validated = TaskContracts::validateAgentSet(agents, m_settings.connections);

    if (!validated.hasValue()) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure(validated.error()));
    }

    QStringList orphaned;

    for (const auto& task : m_tasks) {
        if (task.executionKind == TaskExecutionKind::Agent && TaskContracts::findAgent(agents, task.agentId) == nullptr) {
            orphaned.append(task.id);
        }
    }

    AiSettings next = m_settings;
    next.agents = agents;
    auto future = persistSettings(std::move(next));
    // clang-format off
    return future.then(m_asyncContext.get(), [this, orphaned](Result<void> result) { if (result.hasValue()) { stopOrphanedTasks(orphaned); } return result; });
    // clang-format on
}

// A message that arrived while the turn was giving its final answer was never carried to the model, so it opens the next turn.
// A summary is machinery rather than something the reader said, so it never opens one.
void AiPlugin::continueWhenAnswerIsPending(const QString& taskId, qint64 deliveredSequence) {
    // clang-format off
    const QVector<ConversationMessage>& conversation = m_conversations[taskId];
    const bool pending = std::any_of(conversation.constBegin(), conversation.constEnd(), [deliveredSequence](const ConversationMessage& message) { return message.role == ConversationRole::User && message.summarizedUntil == 0 && message.sequence > deliveredSequence; });
    // clang-format on

    if (!pending || runState(taskId) != TaskRunState::Idle) {
        return;
    }

    auto future = enqueueRun(taskId);
    // clang-format off
    future.then(m_asyncContext.get(), [](Result<void>) {});
    // clang-format on
}

void AiPlugin::recordAgentRemoved(const QString& taskId, const QString& agentId) {
    m_lastStatuses.insert(taskId, ExecutionStatus::Failed);
    m_lastStopReasons.insert(taskId, AgentStopReason::Failed);
    m_lastErrors.insert(taskId, m_host->translate(QStringLiteral("ai.error.agent-removed")).arg(agentId));
    emit taskRunStateChanged(taskId);
}

void AiPlugin::stopOrphanedTasks(const QStringList& taskIds) {
    for (const auto& taskId : taskIds) {
        const auto* orphan = task(taskId);
        if (orphan == nullptr || TaskContracts::findAgent(m_settings.agents, orphan->agentId) != nullptr) {
            continue;
        }

        if (runState(taskId) != TaskRunState::Idle) {
            auto stopped = stopTask(taskId);
            // clang-format off
            stopped.then(m_asyncContext.get(), [](Result<void>) {});
            // clang-format on
        }

        recordAgentRemoved(taskId, orphan->agentId);
    }
}

QVector<ProviderRateLimit> AiPlugin::rateLimits() const {
    return m_settings.rateLimits;
}

QFuture<Result<QString>> AiPlugin::createWorkspace(const QString& name) {
    if (m_repository == nullptr || name.trimmed().isEmpty()) {
        return QtFuture::makeReadyValueFuture(Result<QString>::failure({"ai_tasks_workspace_invalid", "The AI workspace name is invalid", name}));
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{AiPluginHelper::identifier(), name.trimmed(), static_cast<int>(m_workspaces.size()), true, now, now};
    auto future = m_repository->createWorkspace(workspace);
    // clang-format off
    return future.then(m_asyncContext.get(), [this, workspace](Result<void> result) {
        if (!result.hasValue()) {
            return Result<QString>::failure(result.error());
        }
        for (auto& existing : m_workspaces) {
            existing.active = false;
        }
        m_workspaces.append(workspace);
        emit workspacesChanged();
        return Result<QString>::success(workspace.id);
    });
    // clang-format on
}

QFuture<Result<void>> AiPlugin::renameWorkspace(const QString& workspaceId, const QString& name) {
    if (m_repository == nullptr || name.trimmed().isEmpty()) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_tasks_workspace_invalid", "The AI workspace name is invalid", workspaceId}));
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    auto future = m_repository->renameWorkspace(workspaceId, name.trimmed(), now);
    // clang-format off
    return future.then(m_asyncContext.get(), [this, workspaceId, name = name.trimmed(), now](Result<void> result) {
        if (result.hasValue()) {
            for (auto& workspace : m_workspaces) {
                if (workspace.id == workspaceId) {
                    workspace.name = name;
                    workspace.updatedAtUtc = now;
                }
            }
            emit workspacesChanged();
        }
        return result;
    });
    // clang-format on
}

QFuture<Result<void>> AiPlugin::removeWorkspace(const QString& workspaceId) {
    if (m_repository == nullptr || m_workspaces.size() <= 1) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_tasks_workspace_required", "The last AI workspace cannot be removed", workspaceId}));
    }

    for (const auto& task : m_tasks) {
        if (task.workspaceId == workspaceId && runState(task.id) != TaskRunState::Idle) {
            return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_tasks_workspace_busy", "The AI workspace still has active tasks", workspaceId}));
        }
    }

    QStringList removedTaskIds;

    for (const auto& task : m_tasks) {
        removedTaskIds.append(task.workspaceId == workspaceId ? QStringList{task.id} : QStringList{});
    }

    QVector<AiWorkspace> remaining;
    bool removedActiveWorkspace = false;

    for (const auto& workspace : m_workspaces) {
        removedActiveWorkspace = removedActiveWorkspace || (workspace.id == workspaceId && workspace.active);
        if (workspace.id != workspaceId) {
            remaining.append(workspace);
        }
    }

    for (int index = 0; index < remaining.size(); ++index) {
        remaining[index].position = index;
        remaining[index].active = removedActiveWorkspace ? index == 0 : remaining.at(index).active;
    }

    auto future = m_repository->removeWorkspace(workspaceId, remaining);
    // clang-format off
    return future.then(m_asyncContext.get(), [this, removedTaskIds](Result<void> result) {
        if (!result.hasValue()) {
            return result;
        }
        for (const auto& taskId : removedTaskIds) {
            forgetTask(taskId);
        }
        const auto reloaded = reloadState();
        if (reloaded.hasValue()) {
            emit workspacesChanged();
            emit tasksChanged();
        }
        return reloaded;
    });
    // clang-format on
}

QFuture<Result<void>> AiPlugin::activateWorkspace(const QString& workspaceId) {
    if (m_repository == nullptr) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_tasks_workspace_invalid", "The AI workspace identifier is invalid", workspaceId}));
    }

    auto future = m_repository->activateWorkspace(workspaceId);
    // clang-format off
    return future.then(m_asyncContext.get(), [this, workspaceId](Result<void> result) {
        if (result.hasValue()) {
            for (auto& workspace : m_workspaces) {
                workspace.active = workspace.id == workspaceId;
            }
            emit workspacesChanged();
        }
        return result;
    });
    // clang-format on
}

QFuture<Result<void>> AiPlugin::saveTask(AiTask task) {
    if (m_repository == nullptr) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_tasks_not_initialized", "The AI plugin is not initialized", task.id}));
    }
    if (runState(task.id) != TaskRunState::Idle) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_tasks_task_busy", "A running AI task cannot be edited", task.id}));
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const auto* existing = this->task(task.id);
    task.id = task.id.isEmpty() ? AiPluginHelper::identifier() : task.id;
    task.createdAtUtc = existing == nullptr ? now : existing->createdAtUtc;
    task.updatedAtUtc = now;
    int columnSize = 0;

    for (const auto& candidate : m_tasks) {
        columnSize += candidate.workspaceId == task.workspaceId && candidate.column == task.column ? 1 : 0;
    }

    task.position = existing == nullptr ? columnSize : existing->position;
    const auto prepared = AiPluginHelper::prepareTaskSchedule(std::move(task), now);

    if (!prepared.hasValue()) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure(prepared.error()));
    }

    const AiTask stored = prepared.value();
    auto future = m_repository->saveTask(stored);
    // clang-format off
    return future.then(m_asyncContext.get(), [this, stored](Result<void> result) {
        if (result.hasValue()) {
            const auto position = std::find_if(m_tasks.begin(), m_tasks.end(), [&stored](const AiTask& candidate) { return candidate.id == stored.id; });
            if (position == m_tasks.end()) {
                m_tasks.append(stored);
            } else {
                *position = stored;
            }
            emit tasksChanged();
            armScheduleTimer();
        }
        return result;
    });
    // clang-format on
}

// A task that is gone leaves nothing behind, so everything keyed by it is forgotten in one place rather than in each caller that removes one.
void AiPlugin::forgetTask(const QString& taskId) {
    m_active.remove(taskId);
    m_lastStatuses.remove(taskId);
    m_lastStopReasons.remove(taskId);
    m_lastErrors.remove(taskId);
    m_phases.remove(taskId);
    m_phasesBeforeThrottle.remove(taskId);
    m_cancelledTaskIds.remove(taskId);
    m_conversations.remove(taskId);
    m_conversationSequences.remove(taskId);
    m_loadedConversations.remove(taskId);
    m_queue.removeAll(taskId);
}

QFuture<Result<void>> AiPlugin::removeTask(const QString& taskId) {
    if (m_repository == nullptr) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_tasks_not_initialized", "The AI plugin is not initialized", taskId}));
    }
    if (runState(taskId) != TaskRunState::Idle) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_tasks_task_busy", "A running AI task cannot be removed", taskId}));
    }

    auto future = m_repository->removeTask(taskId);
    // clang-format off
    return future.then(m_asyncContext.get(), [this, taskId](Result<void> result) {
        if (result.hasValue()) {
            forgetTask(taskId);
            m_tasks.removeIf([&taskId](const AiTask& candidate) { return candidate.id == taskId; });
            emit tasksChanged();
            armScheduleTimer();
        }
        return result;
    });
    // clang-format on
}

QFuture<Result<void>> AiPlugin::moveTask(const QString& taskId, TaskColumn column) {
    const auto* moved = task(taskId);

    if (m_repository == nullptr || moved == nullptr) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_tasks_task_unknown", "The AI task is unknown", taskId}));
    }
    if (column == TaskColumn::Doing) {
        return startTask(taskId);
    }
    if (runState(taskId) != TaskRunState::Idle) {
        return stopTask(taskId);
    }

    int position = 0;

    for (const auto& candidate : m_tasks) {
        position += candidate.workspaceId == moved->workspaceId && candidate.column == column ? 1 : 0;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    auto future = m_repository->moveTask(taskId, column, position, now);
    // clang-format off
    return future.then(m_asyncContext.get(), [this, taskId, column, position, now](Result<void> result) {
        if (result.hasValue()) {
            for (auto& candidate : m_tasks) {
                if (candidate.id == taskId) {
                    candidate.column = column;
                    candidate.position = position;
                    candidate.updatedAtUtc = now;
                }
            }
            emit tasksChanged();
        }
        return result;
    });
    // clang-format on
}

QFuture<Result<void>> AiPlugin::startTask(const QString& taskId) {
    const auto* started = task(taskId);

    if (m_repository == nullptr || started == nullptr) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_tasks_task_unknown", "The AI task is unknown", taskId}));
    }
    if (runState(taskId) != TaskRunState::Idle) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_tasks_task_busy", "The AI task is already queued or running", taskId}));
    }

    if (started->executionKind == TaskExecutionKind::Agent) {
        const auto agent = agentForTask(*started);
        if (!agent.hasValue()) {
            // The card keeps the reason it could not start, because a task handed to an agent that is gone explains nothing by itself.
            recordAgentRemoved(taskId, started->agentId);
            return QtFuture::makeReadyValueFuture(Result<void>::failure(agent.error()));
        }
        const auto connection = connectionForAgent(agent.value());
        if (!connection.hasValue()) {
            return QtFuture::makeReadyValueFuture(Result<void>::failure(connection.error()));
        }
    }

    if (started->executionKind != TaskExecutionKind::Agent) {
        return enqueueRun(taskId);
    }

    // Play sends the prompt of the task again, because that prompt is the standing instruction a schedule repeats.
    auto loaded = loadConversation(taskId);
    // clang-format off
    const auto prompted = [this, taskId](Result<void> result) {
        if (!result.hasValue()) {
            return QtFuture::makeReadyValueFuture(result);
        }
        const auto* prompt = task(taskId);
        if (prompt == nullptr) {
            return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_tasks_task_unknown", "The AI task is unknown", taskId}));
        }
        auto written = recordConversation(taskId, {buildMessage(taskId, ConversationRole::User, prompt->prompt, {}, {})});
        // clang-format off
        return written.then(m_asyncContext.get(), [this, taskId](Result<void> stored) { return stored.hasValue() ? enqueueRun(taskId) : QtFuture::makeReadyValueFuture(stored); }).unwrap();
        // clang-format on
    };
    // clang-format on
    return loaded.then(m_asyncContext.get(), prompted).unwrap();
}

// The task is queued before its row is written, because a second start asked for while that write is in flight would reach storage as a duplicate.
QFuture<Result<void>> AiPlugin::enqueueRun(const QString& taskId) {
    const AiTask* queued = task(taskId);

    if (queued == nullptr) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_tasks_task_unknown", "The AI task is unknown", taskId}));
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const TaskColumn previousColumn = queued->column;
    m_queue.append(taskId);
    applyScheduledDispatch(taskId, queued->schedule, TaskColumn::Doing, now);
    emit tasksChanged();
    emit taskRunStateChanged(taskId);

    auto future = m_repository->enqueueTask(taskId, now);
    // clang-format off
    return future.then(m_asyncContext.get(), [this, taskId, previousColumn, now](Result<void> result) {
        if (!result.hasValue()) {
            m_queue.removeAll(taskId);
            applyScheduledDispatch(taskId, task(taskId) == nullptr ? std::optional<TaskSchedule>{} : task(taskId)->schedule, previousColumn, now);
            emit tasksChanged();
            emit taskRunStateChanged(taskId);
            return result;
        }
        dispatchQueue();
        return result;
    });
    // clang-format on
}

QFuture<Result<void>> AiPlugin::stopTask(const QString& taskId) {
    if (m_repository == nullptr || task(taskId) == nullptr) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_tasks_task_unknown", "The AI task is unknown", taskId}));
    }
    if (runState(taskId) == TaskRunState::Idle) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"ai_tasks_task_idle", "The AI task is not queued or running", taskId}));
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    auto future = m_repository->cancelTask(taskId, now);
    // clang-format off
    return future.then(m_asyncContext.get(), [this, taskId, now](Result<void> result) {
        if (!result.hasValue()) {
            return result;
        }
        m_queue.removeAll(taskId);
        for (auto& candidate : m_tasks) {
            if (candidate.id == taskId) {
                candidate.column = TaskColumn::Todo;
                candidate.updatedAtUtc = now;
            }
        }
        if (const std::shared_ptr<ActiveExecution> position = m_active.value(taskId); position != nullptr) {
            m_cancelledTaskIds.insert(taskId);
            if (position->client != nullptr) {
                position->client->cancel();
            }
            if (position->runner != nullptr) {
                position->runner->cancel();
            }
            completeExecution(taskId, ExecutionStatus::Cancelled, {}, AgentStopReason::Cancelled);
        }
        emit tasksChanged();
        emit taskRunStateChanged(taskId);
        dispatchQueue();
        return result;
    });
    // clang-format on
}

const AiTask* AiPlugin::task(const QString& taskId) const {
    // clang-format off
    const auto position = std::find_if(m_tasks.constBegin(), m_tasks.constEnd(), [&taskId](const AiTask& candidate) { return candidate.id == taskId; });
    // clang-format on
    return position == m_tasks.constEnd() ? nullptr : &(*position);
}

bool AiPlugin::hasCapacity() const {
    return m_settings.execution.parallelExecutions == 0 || m_active.size() < m_settings.execution.parallelExecutions;
}

Result<void> AiPlugin::reloadState() {
    const auto workspaces = m_repository->workspaces();

    if (!workspaces.hasValue()) {
        return Result<void>::failure(workspaces.error());
    }

    const auto tasks = m_repository->tasks();

    if (!tasks.hasValue()) {
        return Result<void>::failure(tasks.error());
    }

    const auto queued = m_repository->queuedTaskIds();

    if (!queued.hasValue()) {
        return Result<void>::failure(queued.error());
    }

    const auto outcomes = m_repository->lastOutcomes();

    if (!outcomes.hasValue()) {
        return Result<void>::failure(outcomes.error());
    }

    const auto sequences = m_repository->conversationSequences();

    if (!sequences.hasValue()) {
        return Result<void>::failure(sequences.error());
    }

    m_conversationSequences = sequences.value();

    m_workspaces = workspaces.value();
    m_tasks = tasks.value();
    m_queue = queued.value();

    // A card says what happened to its task, so a restart restores the outcome of its newest run and the reason a failure gave.
    for (auto outcome = outcomes.value().constBegin(); outcome != outcomes.value().constEnd(); ++outcome) {
        m_lastStatuses.insert(outcome.key(), outcome.value().status);
        m_lastStopReasons.insert(outcome.key(), outcome.value().stopReason);
        if (outcome.value().status == ExecutionStatus::Failed && !outcome.value().errorMessage.isEmpty()) {
            m_lastErrors.insert(outcome.key(), outcome.value().errorMessage);
        }
    }

    for (const auto& task : m_tasks) {
        if (task.column == TaskColumn::Doing && !m_queue.contains(task.id)) {
            m_queue.append(task.id);
        }
    }

    return Result<void>::success();
}

void AiPlugin::dispatchQueue() {
    if (m_host == nullptr) {
        return;
    }

    const QStringList pending = m_queue;

    for (const auto& taskId : pending) {
        const auto* candidate = task(taskId);
        if (candidate == nullptr || m_active.contains(taskId) || !hasCapacity()) {
            continue;
        }
        startExecution(*candidate);
    }
}

void AiPlugin::startExecution(const AiTask& task) {
    const QString taskId = task.id;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    TaskExecution record;
    record.id = AiPluginHelper::identifier();
    record.taskId = taskId;
    record.status = ExecutionStatus::Running;
    record.startedAtUtc = now;

    auto created = std::make_shared<ActiveExecution>();
    created->record = record;
    m_active.insert(taskId, std::move(created));
    m_queue.removeAll(taskId);
    m_lastErrors.remove(taskId);

    auto started = m_repository->startExecution(record);
    // clang-format off
    started.then(m_asyncContext.get(), [this](Result<void> result) { if (!result.hasValue()) { reportFailure(result.error(), m_host->translate(QStringLiteral("ai.error.execution-save"))); } });
    // clang-format on

    if (task.executionKind == TaskExecutionKind::Command) {
        startCommandExecution(task, record.id);
        return;
    }

    startAgentExecution(task, record.id);
}

void AiPlugin::startCommandExecution(const AiTask& task, const QString& executionId) {
    const QString taskId = task.id;
    auto* runner = new AiCommandRunner(this);
    m_active.value(taskId)->runner = runner;
    m_phases.insert(taskId, ExecutionPhase::Running);
    appendLog(executionId, ExecutionLogLevel::Info, ExecutionLogKind::Started, task.command);

    // clang-format off
    connect(runner, &AiCommandRunner::outputReceived, this, [this, taskId](const QString& text) { const std::shared_ptr<ActiveExecution> position = m_active.value(taskId); if (position != nullptr) { position->record.content.append(text); } });
    connect(runner, &AiCommandRunner::finished, this, [this, taskId, executionId](int exitCode, const QString& output) {
        const std::shared_ptr<ActiveExecution> position = m_active.value(taskId);
        if (position == nullptr) {
            return;
        }
        position->record.content = output;
        position->record.finishReason = QString::number(exitCode);
        appendLog(executionId, ExecutionLogLevel::Debug, ExecutionLogKind::ResponseReceived, output);
        if (exitCode == 0) {
            completeExecution(taskId, ExecutionStatus::Succeeded, {}, AgentStopReason::Answered);
            return;
        }
        const QString message = m_host->translate(QStringLiteral("ai.error.exit-code")).arg(QString::number(exitCode));
        m_lastErrors.insert(taskId, message);
        appendLog(executionId, ExecutionLogLevel::Error, ExecutionLogKind::Failed, message);
        completeExecution(taskId, ExecutionStatus::Failed, message, AgentStopReason::Failed);
    });
    connect(runner, &AiCommandRunner::failed, this, [this, taskId, executionId](const Error& error) {
        const auto translate = [this](const QString& key) { return m_host->translate(key); };
        const QString message = CommandOutput::commandFailureMessage(error, translate);
        appendLog(executionId, ExecutionLogLevel::Error, ExecutionLogKind::Failed, error.detail.isEmpty() ? error.message : QStringLiteral("%1 (%2)").arg(error.message, error.detail));
        m_lastErrors.insert(taskId, message);
        reportFailure(error, message);
        completeExecution(taskId, ExecutionStatus::Failed, message, AgentStopReason::Failed);
    });
    // clang-format on

    runner->start(task.command, task.workdir, task.commandTimeoutSeconds);
    emit taskRunStateChanged(taskId);
}

void AiPlugin::startAgentExecution(const AiTask& task, const QString& executionId) {
    const QString taskId = task.id;
    const auto agent = agentForTask(task);

    if (!agent.hasValue()) {
        const QString reason = m_host->translate(QStringLiteral("ai.error.agent-removed")).arg(agent.error().detail);
        appendLog(executionId, ExecutionLogLevel::Error, ExecutionLogKind::Failed, agent.error().message);
        m_lastErrors.insert(taskId, reason);
        reportFailure(agent.error(), reason);
        completeExecution(taskId, ExecutionStatus::Failed, reason, AgentStopReason::Failed);
        return;
    }

    const auto connection = connectionForAgent(agent.value());

    if (!connection.hasValue()) {
        const QString reason = m_host->translate(QStringLiteral("ai.error.connection-missing")).arg(connection.error().detail);
        appendLog(executionId, ExecutionLogLevel::Error, ExecutionLogKind::Failed, connection.error().message);
        m_lastErrors.insert(taskId, reason);
        reportFailure(connection.error(), reason);
        completeExecution(taskId, ExecutionStatus::Failed, reason, AgentStopReason::Failed);
        return;
    }

    AiChatClient* client = m_clientFactory(m_gate, connection.value()).release();
    client->setParent(this);

    const std::shared_ptr<ActiveExecution> execution = m_active.value(taskId);
    execution->client = client;
    execution->maximumIterations = agent.value().maximumIterations;
    execution->sandboxRoot = task.workdir;
    execution->connection = connection.value();
    m_tools->setTaskContext(task, execution->connection);
    m_phases.insert(taskId, ExecutionPhase::Sending);
    appendLog(executionId, ExecutionLogLevel::Info, ExecutionLogKind::Started, ModelConnections::connectionKey(execution->connection));

    // clang-format off
    connect(client, &AiChatClient::requestSent, this, [this, taskId, executionId](const QString& endpoint, const QString& body) { appendLog(executionId, ExecutionLogLevel::Debug, ExecutionLogKind::RequestSent, QStringLiteral("%1\n%2").arg(endpoint, body)); reportSending(taskId); });
    connect(client, &AiChatClient::throttled, this, [this, taskId, executionId](ThrottleReason reason, qint64 milliseconds) { reportThrottle(taskId, executionId, reason, milliseconds); });
    connect(client, &AiChatClient::contentReceived, this, [this, taskId, executionId](const QString& delta) {
        const std::shared_ptr<ActiveExecution> position = m_active.value(taskId);
        if (position == nullptr) {
            return;
        }
        if (m_phases.value(taskId) != ExecutionPhase::Streaming) {
            m_phases.insert(taskId, ExecutionPhase::Streaming);
            appendLog(executionId, ExecutionLogLevel::Info, ExecutionLogKind::FirstTokenReceived);
            emit taskRunStateChanged(taskId);
        }
        position->record.content.append(delta);
        position->streamed.append(delta);
        emit conversationStreamed(taskId, position->streamed);
    });
    connect(client, &AiChatClient::finished, this, [this, taskId, executionId](const QString& content, const QVector<ToolCall>& calls, ChatUsage usage, const QString& finishReason) {
        const std::shared_ptr<ActiveExecution> position = m_active.value(taskId);
        if (position == nullptr) {
            return;
        }
        position->record.inputTokens += usage.inputTokens;
        position->record.outputTokens += usage.outputTokens;
        position->record.finishReason = finishReason;
        appendLog(executionId, ExecutionLogLevel::Debug, ExecutionLogKind::ResponseReceived, content);
        appendLog(executionId, ExecutionLogLevel::Info, ExecutionLogKind::UsageReported, m_host->translate(QStringLiteral("ai.log.usage")).arg(QString::number(usage.inputTokens), QString::number(usage.outputTokens), finishReason));
        // An answer the provider cut at the output budget is incomplete rather than lost, so what arrived is kept and the reason says the budget ended it.
        if (ChatRequests::truncatedByOutputBudget(finishReason)) {
            if (!content.trimmed().isEmpty()) {
                std::ignore = recordConversation(taskId, {buildMessage(taskId, ConversationRole::Assistant, content, {}, {})});
            }
            completeExecution(taskId, ExecutionStatus::Succeeded, {}, AgentStopReason::OutputBudget);
            return;
        }
        if (calls.isEmpty()) {
            if (!content.trimmed().isEmpty()) {
                std::ignore = recordConversation(taskId, {buildMessage(taskId, ConversationRole::Assistant, content, {}, {})});
            }
            const qint64 delivered = position->deliveredSequence;
            completeExecution(taskId, ExecutionStatus::Succeeded, {}, AgentStopReason::Answered);
            continueWhenAnswerIsPending(taskId, delivered);
            return;
        }
        if (!content.isEmpty()) {
            position->record.content.append(QStringLiteral("\n"));
        }
        handleToolCalls(taskId, content, calls);
    });
    connect(client, &AiChatClient::failed, this, [this, taskId, executionId](const Error& error) {
        // A run cut at the output budget is reported by that budget, because the parsing that failed is only what the cut left behind.
        const QString message = error.code == QStringLiteral("ai_output_truncated") ? m_host->translate(QStringLiteral("ai.error.output-truncated")).arg(QString::number(ModelConnections::outputBudget(m_active.value(taskId) == nullptr ? ModelConnection{} : m_active.value(taskId)->connection))) : error.message;
        // What the service answered is an exchanged payload, so it is opened on demand instead of being packed into the failure line.
        if (!error.detail.isEmpty()) {
            appendLog(executionId, ExecutionLogLevel::Debug, ExecutionLogKind::ResponseReceived, error.detail);
        }
        appendLog(executionId, ExecutionLogLevel::Error, ExecutionLogKind::Failed, message);
        m_lastErrors.insert(taskId, message);
        reportFailure({error.code, message, error.detail}, message);
        completeExecution(taskId, ExecutionStatus::Failed, message, AgentStopReason::Failed);
    });
    // A run reads the workspace again, so a skill or an instruction written while the application is open reaches it.
    m_tools->forgetResources();
    m_tools->discoverResources(task.workdir, [this, taskId, executionId, task, resolvedAgent = agent.value()](const QVector<agent::ResourceDescriptor>& skills) {
        const std::shared_ptr<ActiveExecution> pending = m_active.value(taskId);
        if (pending == nullptr || pending->record.id != executionId) {
            return;
        }
        pending->instructions = instructionMessage(task, resolvedAgent, pending->connection, skills);
        continueAgent(taskId);
    });
    // clang-format on
}

void AiPlugin::continueAgent(const QString& taskId) {
    const std::shared_ptr<ActiveExecution> position = m_active.value(taskId);

    if (position == nullptr) {
        return;
    }

    // A zero limit lets the agent run until it stops asking for tools.
    ++position->iteration;

    if (position->maximumIterations > 0 && position->iteration > position->maximumIterations) {
        completeExecution(taskId, ExecutionStatus::Succeeded, {}, AgentStopReason::IterationLimit);
        return;
    }

    // The conversation is fitted to the window of the selected model before every turn.
    const ModelDescriptor* model = findModelDescriptor(position->connection);
    const std::optional<qint64> limit = ToolContracts::fittingTokenLimit(model == nullptr ? 0 : model->contextWindow, reservedContextTokens(position->connection));
    QVector<qint64> sequences;
    QJsonArray projected = projectConversation(position->instructions, position->connection, m_conversations.value(taskId), position->seenImages, &sequences);

    if (const qsizetype pruned = ToolContracts::pruneToolResults(projected, limit); pruned > 0) {
        appendLog(position->record.id, ExecutionLogLevel::Info, ExecutionLogKind::Compacted, m_host->translate(QStringLiteral("ai.log.pruned")).arg(QString::number(pruned)));
    }

    FittedConversation fitted = ToolContracts::fitConversation(projected, limit);

    if (!fitted.dropped.isEmpty()) {
        appendLog(position->record.id, ExecutionLogLevel::Warning, ExecutionLogKind::Compacted, m_host->translate(QStringLiteral("ai.log.compacted")).arg(QString::number(fitted.dropped.size())));
        const qsizetype lastDropped = fitted.preservedHead + fitted.dropped.size() - 1;
        position->summarizedUntil = lastDropped >= 0 && lastDropped < sequences.size() ? sequences.at(lastDropped) : 0;
        summarizeDroppedTurns(taskId, fitted);
        return;
    }

    // The turn that has no turn after it is told so, because a model that knows it is the last one answers instead of calling another tool.
    if (position->maximumIterations > 0 && position->iteration == position->maximumIterations) {
        fitted.messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), m_host->translate(QStringLiteral("ai.agent.final-turn"))}});
    }

    appendLog(position->record.id, ExecutionLogLevel::Info, ExecutionLogKind::Iteration, QString::number(position->iteration));
    position->streamed.clear();
    position->deliveredSequence = m_conversations.value(taskId).isEmpty() ? 0 : m_conversations.value(taskId).last().sequence;
    m_phases.insert(taskId, ExecutionPhase::Sending);
    emit taskRunStateChanged(taskId);

    // clang-format off
    const auto translate = [this](const QString& key) { return m_host->translate(key); };
    // clang-format on
    position->client->send({position->connection, ModelConnections::connectionAddress(position->connection), shapeForProtocol(position->connection, fitted.messages), declaredTools(position->connection), position->sandboxRoot}, translate);
}

// The answer and the tool declarations occupy the window beside the conversation, so both are reserved before anything is fitted into it.
qint64 AiPlugin::reservedContextTokens(const ModelConnection& connection) const {
    // clang-format off
    const auto translate = [this](const QString& key) { return m_host->translate(key); };
    // clang-format on
    // A command line agent is handed a prompt and runs its own tools, so neither an answer budget nor a tool declaration travels and neither takes room from the window.
    if (ModelConnections::connectionProtocol(connection) == WireProtocol::CommandLine) {
        return 0;
    }

    const qint64 tools = m_tools == nullptr ? 0 : ToolContracts::estimateTokens(ToolContracts::serializeTools(ModelConnections::connectionProtocol(connection), m_tools->schemas(), translate));
    return tools + std::max<qint64>(ModelConnections::outputBudget(connection), 0);
}

// What no longer fits the window is replaced by one summary, so the agent keeps what it learned instead of only what is recent.
void AiPlugin::summarizeDroppedTurns(const QString& taskId, const FittedConversation& fitted) {
    const std::shared_ptr<ActiveExecution> position = m_active.value(taskId);

    if (position == nullptr) {
        return;
    }

    const QString executionId = position->record.id;
    ModelConnection connection = position->connection;
    ModelConnections::setOutputBudget(connection, ProviderCatalog::aiLimits().summaryMaximumTokens);

    const QString instruction = m_host->translate(QStringLiteral("ai.agent.summarize"));
    // What was dropped can be larger than the window itself, so the request that summarizes it is fitted like any other.
    QJsonArray dropped = fitted.dropped;
    const ModelDescriptor* model = findModelDescriptor(position->connection);
    const std::optional<qint64> summaryLimit = ToolContracts::fittingTokenLimit(model == nullptr ? 0 : model->contextWindow, ProviderCatalog::aiLimits().summaryMaximumTokens);
    const qsizetype prunedForSummary = ToolContracts::pruneToolResults(dropped, summaryLimit);

    if (prunedForSummary > 0) {
        appendLog(executionId, ExecutionLogLevel::Info, ExecutionLogKind::Compacted, m_host->translate(QStringLiteral("ai.log.pruned")).arg(QString::number(prunedForSummary)));
    }

    const QString transcript = QString::fromUtf8(QJsonDocument(ToolContracts::fitConversation(dropped, summaryLimit).messages).toJson(QJsonDocument::Compact));
    const QJsonArray request{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), instruction + QStringLiteral("\n\n") + transcript}}};

    auto* client = m_clientFactory(m_gate, connection).release();
    client->setParent(this);
    position->summaryClient = client;
    m_phases.insert(taskId, ExecutionPhase::Compacting);
    emit taskRunStateChanged(taskId);

    // clang-format off
    connect(client, &AiChatClient::throttled, this, [this, taskId, executionId](ThrottleReason reason, qint64 milliseconds) { reportThrottle(taskId, executionId, reason, milliseconds); });
    connect(client, &AiChatClient::finished, this, [this, taskId, executionId, client, fitted](const QString& content, const QVector<ToolCall>&, ChatUsage usage, const QString&) { releaseSummaryClient(taskId, client); recordUsage(taskId, usage); applySummary(taskId, executionId, fitted, content); });
    connect(client, &AiChatClient::failed, this, [this, taskId, executionId, client, fitted](const Error& error) { releaseSummaryClient(taskId, client); appendLog(executionId, ExecutionLogLevel::Warning, ExecutionLogKind::Compacted, error.message); applySummary(taskId, executionId, fitted, {}); });
    const auto translate = [this](const QString& key) { return m_host->translate(key); };
    // clang-format on
    client->send({connection, ModelConnections::connectionAddress(connection), request, {}, {}}, translate);
}

// The wait is over once the request really leaves, so the card goes back to saying what it was doing before it waited.
void AiPlugin::reportSending(const QString& taskId) {
    if (!m_active.contains(taskId) || m_phases.value(taskId) != ExecutionPhase::Throttled) {
        return;
    }

    m_phases.insert(taskId, m_phasesBeforeThrottle.value(taskId, ExecutionPhase::Sending));
    m_phasesBeforeThrottle.remove(taskId);
    emit taskRunStateChanged(taskId);
}

// A run that is waiting says so on its card and in its log, because a card that only says sending explains nothing.
void AiPlugin::reportThrottle(const QString& taskId, const QString& executionId, ThrottleReason reason, qint64 milliseconds) {
    if (!m_active.contains(taskId)) {
        return;
    }

    const QString key = reason == ThrottleReason::Retry ? QStringLiteral("ai.log.retry-delay") : QStringLiteral("ai.log.rate-limit-delay");
    appendLog(executionId, ExecutionLogLevel::Info, ExecutionLogKind::Throttled, m_host->translate(key).arg(QString::number(milliseconds)));

    if (m_phases.value(taskId) != ExecutionPhase::Throttled) {
        m_phasesBeforeThrottle.insert(taskId, m_phases.value(taskId, ExecutionPhase::Sending));
    }

    m_phases.insert(taskId, ExecutionPhase::Throttled);
    emit taskRunStateChanged(taskId);
}

void AiPlugin::releaseSummaryClient(const QString& taskId, AiChatClient* client) {
    if (const std::shared_ptr<ActiveExecution> position = m_active.value(taskId); position != nullptr && position->summaryClient == client) {
        position->summaryClient = nullptr;
    }

    client->deleteLater();
}

// The call that summarises a conversation is part of the run that needed it, so what it spent is counted with everything else the run spent.
void AiPlugin::recordUsage(const QString& taskId, ChatUsage usage) {
    const std::shared_ptr<ActiveExecution> position = m_active.value(taskId);

    if (position == nullptr) {
        return;
    }

    position->record.inputTokens += usage.inputTokens;
    position->record.outputTokens += usage.outputTokens;
}

void AiPlugin::applySummary(const QString& taskId, const QString& executionId, const FittedConversation& fitted, const QString& summary) {
    const std::shared_ptr<ActiveExecution> position = m_active.value(taskId);

    if (position == nullptr || position->record.id != executionId) {
        return;
    }

    QJsonArray messages = fitted.messages;

    if (!summary.trimmed().isEmpty()) {
        const QString text = m_host->translate(QStringLiteral("ai.agent.summary")).arg(summary.trimmed());
        messages.insert(fitted.preservedHead, QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), text}});
        appendLog(executionId, ExecutionLogLevel::Info, ExecutionLogKind::Compacted, summary.trimmed());

        ConversationMessage recorded = buildMessage(taskId, ConversationRole::User, text, {}, {});
        recorded.summarizedUntil = position->summarizedUntil;
        std::ignore = recordConversation(taskId, {recorded});
    }

    appendLog(executionId, ExecutionLogLevel::Info, ExecutionLogKind::Iteration, QString::number(position->iteration));
    m_phases.insert(taskId, ExecutionPhase::Sending);
    emit taskRunStateChanged(taskId);

    // clang-format off
    const auto translate = [this](const QString& key) { return m_host->translate(key); };
    // clang-format on
    position->client->send({position->connection, ModelConnections::connectionAddress(position->connection), shapeForProtocol(position->connection, messages), declaredTools(position->connection), position->sandboxRoot}, translate);
}

void AiPlugin::handleToolCalls(const QString& taskId, const QString& content, const QVector<ToolCall>& calls) {
    const std::shared_ptr<ActiveExecution> position = m_active.value(taskId);

    if (position == nullptr) {
        return;
    }

    const QString executionId = position->record.id;
    m_phases.insert(taskId, ExecutionPhase::CallingTool);
    emit taskRunStateChanged(taskId);
    std::ignore = recordConversation(taskId, {buildMessage(taskId, ConversationRole::Assistant, content, AiPluginHelper::toolCallsDocument(calls), {})});
    position->toolCalls.clear();

    for (const auto& call : calls) {
        const QString arguments = call.unreadableArguments.isEmpty() ? QString::fromUtf8(QJsonDocument(call.arguments).toJson(QJsonDocument::Compact)) : call.unreadableArguments;
        const QString signature = call.name + arguments;
        appendLog(executionId, ExecutionLogLevel::Info, ExecutionLogKind::ToolCalled, QStringLiteral("%1 %2").arg(call.name, arguments));

        if (++position->toolCallSignatures[signature] > ProviderCatalog::aiLimits().repeatedToolCallLimit) {
            // Repeating one call is the symptom, and what that call kept answering is the reason, so the log carries it.
            const QString reason = position->lastToolFailures.value(call.name);
            const QString message = reason.isEmpty() ? m_host->translate(QStringLiteral("ai.error.tool-repeated")).arg(call.name) : m_host->translate(QStringLiteral("ai.error.tool-repeated-reason")).arg(call.name, reason);
            appendLog(executionId, ExecutionLogLevel::Warning, ExecutionLogKind::ToolReturned, message);
            completeExecution(taskId, ExecutionStatus::Succeeded, {}, AgentStopReason::ToolRepetition);
            return;
        }

        position->toolCalls.append({call, m_tools->accessOf(call, position->sandboxRoot), false, false, {}});
    }

    dispatchPendingTools(taskId, executionId);
}

// A call starts as soon as nothing running can reach what it touches, so reads of one turn still run together while two writes to one file take their turn.
void AiPlugin::dispatchPendingTools(const QString& taskId, const QString& executionId) {
    bool dispatched = false;

    for (;;) {
        const std::shared_ptr<ActiveExecution> position = m_active.value(taskId);
        if (position == nullptr || position->record.id != executionId) {
            break;
        }

        int candidate = -1;
        for (int index = 0; index < position->toolCalls.size() && candidate < 0; ++index) {
            if (position->toolCalls.at(index).started) {
                continue;
            }
            bool blocked = false;
            for (const auto& other : position->toolCalls) {
                if (other.started && !other.finished && ToolAccessRules::toolAccessesConflict(position->toolCalls.at(index).access, other.access)) {
                    blocked = true;
                    break;
                }
            }
            if (!blocked) {
                candidate = index;
            }
        }
        if (candidate < 0) {
            break;
        }

        dispatched = true;
        position->toolCalls[candidate].started = true;
        const ToolCall call = position->toolCalls.at(candidate).call;
        const QString sandboxRoot = position->sandboxRoot;
        // A call the protocol left unreadable is answered to the model, because a model told what arrived writes the call again instead of losing the run.
        if (!call.unreadableArguments.isEmpty()) {
            completeToolCall(taskId, executionId, call.name, {call.id, m_host->translate(QStringLiteral("ai.error.tool-arguments-unreadable")).arg(call.name, call.unreadableArguments), true});
            continue;
        }
        if (const int deadlineMs = m_tools->deadlineMsFor(call); deadlineMs > 0) {
            auto* deadline = new QTimer(this);
            deadline->setSingleShot(true);
            position->toolCalls[candidate].deadline = deadline;
            // clang-format off
            connect(deadline, &QTimer::timeout, this, [this, taskId, executionId, call]() { completeToolCall(taskId, executionId, call.name, {call.id, m_host->translate(QStringLiteral("ai.error.tool-deadline")).arg(call.name), true}); });
            // clang-format on
            deadline->start(deadlineMs);
        }
        // clang-format off
        m_tools->invoke(call, sandboxRoot, [this, taskId, executionId, name = call.name](ToolResult result) { completeToolCall(taskId, executionId, name, std::move(result)); });
        // clang-format on
    }

    // The card names the tools of the turn, so it is told whenever the set of running ones changes.
    if (dispatched) {
        emit taskRunStateChanged(taskId);
    }
}

// A tool that answers after its run was stopped belongs to that run, so its result never joins the one the card started next.
QVector<ToolSchema> AiPlugin::declaredTools(const ModelConnection& connection) const {
    const ProviderDescriptor* provider = ProviderCatalog::findProvider(connection.providerId);

    return provider != nullptr && provider->protocol == WireProtocol::CommandLine ? QVector<ToolSchema>{} : m_tools->schemas();
}

void AiPlugin::completeToolCall(const QString& taskId, const QString& executionId, const QString& name, ToolResult result) {
    const std::shared_ptr<ActiveExecution> position = m_active.value(taskId);

    if (position == nullptr || position->record.id != executionId) {
        return;
    }

    PendingToolCall* pending = nullptr;

    for (auto& candidate : position->toolCalls) {
        if (candidate.started && !candidate.finished && candidate.call.id == result.callId) {
            pending = &candidate;
            break;
        }
    }
    // An answer nobody is waiting for changes nothing, because the call it carries was already answered by the turn it belonged to.
    if (pending == nullptr) {
        return;
    }

    appendLog(executionId, result.failed ? ExecutionLogLevel::Warning : ExecutionLogLevel::Info, ExecutionLogKind::ToolReturned, QStringLiteral("%1 %2").arg(name, result.text));

    if (!result.imageData.isEmpty()) {
        position->seenImages.insert(result.callId, result);
    }

    if (result.failed) {
        position->lastToolFailures.insert(name, result.text);
    } else {
        position->lastToolFailures.remove(name);
    }

    pending->finished = true;
    pending->result = std::move(result);

    // The completion can be reached from the deadline that is emitting, so its destruction is deferred.
    if (pending->deadline != nullptr) {
        pending->deadline->stop();
        pending->deadline->deleteLater();
        pending->deadline = nullptr;
    }

    QVector<ToolResult> completed;

    for (const auto& answeredCall : position->toolCalls) {
        if (!answeredCall.finished) {
            dispatchPendingTools(taskId, executionId);
            return;
        }
        completed.append(answeredCall.result);
    }

    // The results are answered in the order the model asked for them, so a turn reads the same however its calls were scheduled.
    QVector<ConversationMessage> answered;

    for (const auto& completedCall : completed) {
        ConversationMessage message = buildMessage(taskId, ConversationRole::Tool, completedCall.text, {}, completedCall.callId);
        message.imageData = completedCall.imageData;
        message.imageMediaType = completedCall.imageMediaType;
        answered.append(message);
    }

    std::ignore = recordConversation(taskId, answered);
    position->toolCalls.clear();
    continueAgent(taskId);
}

const QVector<agent::mcp::McpServerDescriptor>& AiPlugin::mcpServers() const {
    return m_settings.mcpServers;
}

// Storage holds only what the user changed, so an untouched credential resolves to the reference its service declares.
SearchSettings AiPlugin::effectiveSearchSettings() const {
    SearchSettings effective = m_settings.search;

    if (effective.apiKey.isEmpty()) {
        effective.apiKey = TaskContracts::declaredSearchSettings(effective.provider).apiKey;
    }

    return effective;
}

QFuture<Result<void>> AiPlugin::saveSearchSettings(const SearchSettings& settings) {
    AiSettings next = m_settings;
    next.search = settings;
    return persistSettings(std::move(next));
}

ToolPresentation AiPlugin::toolPresentation(const QString& toolName, const QJsonObject& arguments) const {
    return m_tools == nullptr ? ToolPresentation{toolName, {}} : m_tools->presentation(toolName, arguments);
}

int AiPlugin::mcpToolCount(const QString& serverId) const {
    auto* client = m_mcpClients.value(serverId);
    return client == nullptr ? 0 : static_cast<int>(client->tools().size());
}

QFuture<Result<void>> AiPlugin::saveMcpServers(const QVector<agent::mcp::McpServerDescriptor>& servers) {
    AiSettings next = m_settings;
    next.mcpServers = servers;
    auto future = persistSettings(std::move(next));
    // clang-format off
    return future.then(m_asyncContext.get(), [this](Result<void> result) { if (result.hasValue()) { restartMcpClients(); } return result; });
    // clang-format on
}

// A configured server is connected once and its tools join the catalog every agent receives.
void AiPlugin::restartMcpClients() {
    for (auto* client : m_mcpClients) {
        client->disconnect(this);
        client->stop();
        client->deleteLater();
    }

    m_mcpClients.clear();

    if (m_tools != nullptr) {
        m_tools->setMcpClients({});
    }

    for (const auto& server : m_settings.mcpServers) {
        agent::mcp::McpServerDescriptor configured = server;
        configured.startTimeoutMs = ProviderCatalog::aiLimits().serverStartTimeoutMs;
        auto* client = new agent::mcp::McpClient(configured, this);
        // clang-format off
        client->setSamplingHandler([this](const QJsonObject& parameters, int maximumTokens, agent::mcp::McpReply reply) { runSampling(parameters, maximumTokens, reply); });
        connect(client, &agent::mcp::McpClient::toolsChanged, this, [this]() { if (m_tools != nullptr) { m_tools->setMcpClients(m_mcpClients.values()); } });
        connect(client, &agent::mcp::McpClient::failed, this, [this](const Error& error) { reportFailure(error, m_host->translate(QStringLiteral("ai.error.server-failed"))); });
        connect(client, &agent::mcp::McpClient::progressReported, this, [this, server](const QString& token, double progress, double total, const QString& message) { m_host->log(LogLevel::Info, QStringLiteral("mcp"), message.isEmpty() ? token : message, {{QStringLiteral("server"), server.id}, {QStringLiteral("progress"), progress}, {QStringLiteral("total"), total}}); });
        // clang-format on
        m_mcpClients.insert(server.id, client);
        client->start();
    }
}

void AiPlugin::runSampling(const QJsonObject& parameters, int maximumTokens, agent::mcp::McpReply reply) {
    const auto selected = defaultConnection();

    if (!selected.has_value()) {
        reply(Result<QJsonObject>::failure({"ai_provider_unconfigured", "No AI provider is configured", {}}));
        return;
    }

    ModelConnection connection = selected.value();

    if (maximumTokens > 0) {
        ModelConnections::setOutputBudget(connection, maximumTokens);
    }

    auto* client = m_clientFactory(m_gate, connection).release();
    client->setParent(this);
    // clang-format off
    connect(client, &AiChatClient::finished, this, [client, reply, modelId = connection.modelId](const QString& content, const QVector<ToolCall>&, ChatUsage, const QString&) { reply(Result<QJsonObject>::success({{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("model"), modelId}, {QStringLiteral("content"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), content}}}})); client->deleteLater(); });
    connect(client, &AiChatClient::failed, this, [client, reply](const Error& error) { reply(Result<QJsonObject>::failure(error)); client->deleteLater(); });
    const auto translate = [this](const QString& key) { return m_host->translate(key); };
    // clang-format on
    client->send({connection, ModelConnections::connectionAddress(connection), parameters.value(QStringLiteral("messages")).toArray(), {}, {}}, translate);
}

void AiPlugin::refreshToolConfiguration() {
    if (m_tools == nullptr) {
        return;
    }

    m_tools->setSpeechConfiguration(effectiveSpeechSettings(), {});
    m_tools->setSearchConfiguration(effectiveSearchSettings(), TaskContracts::searchAddress(m_settings.search));
    const auto selected = defaultConnection();

    if (!selected.has_value()) {
        return;
    }

    m_tools->setMediaConfiguration(selected.value(), ModelConnections::connectionAddress(selected.value()));
}

// An agent that does not know the machine it runs on answers with the wrong date, the wrong language and the wrong paths.
QString AiPlugin::environmentSection() const {
    const QDateTime now = QDateTime::currentDateTime();
    const QLocale locale = QLocale::system();
    QStringList lines{m_host->translate(QStringLiteral("ai.agent.environment"))};
    const QString user = qEnvironmentVariable("USER", qEnvironmentVariable("USERNAME"));

    if (!user.isEmpty()) {
        lines.append(QStringLiteral("- user: %1").arg(user));
    }

    lines.append(QStringLiteral("- home directory: %1").arg(QDir::homePath()));
    lines.append(QStringLiteral("- local time: %1 (%2)").arg(now.toString(Qt::ISODate), QString::fromUtf8(now.timeZone().id())));
    lines.append(QStringLiteral("- utc time: %1").arg(persistence::StoredValues::storedTimestamp(now)));
    lines.append(QStringLiteral("- locale: %1").arg(locale.name()));
    lines.append(QStringLiteral("- language: %1").arg(QLocale::languageToString(locale.language())));
    lines.append(QStringLiteral("- operating system: %1").arg(QSysInfo::prettyProductName()));
    return lines.join(QLatin1Char('\n'));
}

// The data block is what the agent template asks for with one tag, so the instructions stay the ones the user wrote.
QString AiPlugin::systemPromptData(const AiTask& task, const QVector<agent::ResourceDescriptor>& skills) const {
    QStringList sections;
    sections.append(task.workdir.isEmpty() ? m_host->translate(QStringLiteral("ai.agent.no-workdir")) : m_host->translate(QStringLiteral("ai.agent.workdir")).arg(task.workdir));
    sections.append(environmentSection());

    const QStringList contextFiles = contextInstructions(skills);

    for (const auto& instructions : contextFiles) {
        sections.append(instructions);
    }

    const QString catalog = skillCatalog(skills);

    if (!catalog.isEmpty()) {
        sections.append(catalog);
    }

    return sections.join(QStringLiteral("\n\n"));
}

// A skill is disclosed progressively, so only its name and description are offered and the body is loaded by a tool.
QString AiPlugin::skillCatalog(const QVector<agent::ResourceDescriptor>& resources) const {
    const QVector<agent::ResourceDescriptor> skills = agent::AgentResourceCatalog::ofKind(resources, agent::ResourceKind::Skill);

    if (skills.isEmpty()) {
        return {};
    }

    QStringList catalog{m_host->translate(QStringLiteral("ai.agent.skills"))};

    for (const auto& skill : skills) {
        catalog.append(QStringLiteral("- %1: %2").arg(skill.name, skill.description));
    }

    return catalog.join(QStringLiteral("\n"));
}

// The servers an agent may call are the ones connected right now, because a server that is not ready answers nothing.
QString AiPlugin::serverCatalog() const {
    QStringList names;

    for (auto client = m_mcpClients.constBegin(); client != m_mcpClients.constEnd(); ++client) {
        if (client.value() != nullptr && client.value()->ready()) {
            names.append(client.key());
        }
    }

    names.sort();

    return names.isEmpty() ? m_host->translate(QStringLiteral("ai.capability.servers-none")) : m_host->translate(QStringLiteral("ai.capability.servers")).arg(names.join(QStringLiteral(", ")));
}

QString AiPlugin::renderedSystemPrompt(const AiTask& task, const AiAgent& agent, const QVector<agent::ResourceDescriptor>& skills) const {
    const QDateTime now = QDateTime::currentDateTime();
    const QLocale locale = QLocale::system();
    QStringList toolNames;

    for (const auto& schema : m_tools == nullptr ? QVector<ToolSchema>{} : m_tools->schemas()) {
        toolNames.append(schema.name);
    }

    QHash<QString, QString> values;
    values.insert(QStringLiteral("SYSTEM_PROMPT_DATA"), systemPromptData(task, skills));
    values.insert(QStringLiteral("AGENT_NAME"), agent.name);
    values.insert(QStringLiteral("AGENT_DESCRIPTION"), agent.description);
    values.insert(QStringLiteral("TASK_TITLE"), task.title);
    values.insert(QStringLiteral("TASK_DESCRIPTION"), task.description);
    values.insert(QStringLiteral("TASK_PROMPT"), task.prompt);
    values.insert(QStringLiteral("TASK_WORKDIR"), task.workdir);
    values.insert(QStringLiteral("TASK_ISSUE_URL"), task.issueUrl);
    values.insert(QStringLiteral("DATE_TIME"), locale.toString(now, QLocale::LongFormat));
    values.insert(QStringLiteral("DATE_TIME_UTC"), now.toUTC().toString(Qt::ISODate));
    values.insert(QStringLiteral("TIME_ZONE"), QString::fromUtf8(now.timeZone().id()));
    values.insert(QStringLiteral("LOCALE"), locale.name());
    values.insert(QStringLiteral("LANGUAGE"), QLocale::languageToString(locale.language()));
    values.insert(QStringLiteral("OPERATING_SYSTEM"), QSysInfo::prettyProductName());
    values.insert(QStringLiteral("USER_NAME"), qEnvironmentVariable("USER", qEnvironmentVariable("USERNAME")));
    values.insert(QStringLiteral("HOME_DIRECTORY"), QDir::homePath());
    values.insert(QStringLiteral("TOOLS"), toolNames.join(QStringLiteral(", ")));
    values.insert(QStringLiteral("SKILLS"), skillCatalog(skills));
    values.insert(QStringLiteral("CONTEXT_FILES"), contextInstructions(skills).join(QStringLiteral("\n\n")));
    // A capability tag answers what this run really has, so the agent never promises what is not there.
    const ModelConnection connection = connectionForAgent(agent).hasValue() ? connectionForAgent(agent).value() : ModelConnection{};
    const ProviderDescriptor* provider = ProviderCatalog::findProvider(connection.providerId);
    const QSet<ModelTrait> traits = provider == nullptr ? QSet<ModelTrait>{} : ProviderCatalog::modelTraits(*provider, connection.modelId);
    const ModelDescriptor* model = provider == nullptr ? nullptr : ProviderCatalog::findModel(*provider, connection.modelId);
    QStringList traitNames;

    for (const auto trait : traits) {
        traitNames.append(ProviderCatalog::modelTraitIdentifier(trait));
    }

    traitNames.sort();
    values.insert(QStringLiteral("MODEL"), ModelConnections::connectionKey(connection));
    values.insert(QStringLiteral("MODEL_TRAITS"), traitNames.join(QStringLiteral(", ")));
    values.insert(QStringLiteral("VISION"), m_host->translate(traits.contains(ModelTrait::Vision) ? QStringLiteral("ai.capability.vision-yes") : QStringLiteral("ai.capability.vision-no")));
    // Only the sentence that names a service takes one, because a sentence saying there is none has nothing to name.
    const bool searchConfigured = !m_settings.search.apiKey.isEmpty() || !m_settings.search.instanceUrl.isEmpty();
    const bool speechConfigured = !m_settings.speech.apiKey.isEmpty();
    values.insert(QStringLiteral("SEARCH"), searchConfigured ? m_host->translate(QStringLiteral("ai.capability.search-yes")).arg(TaskContracts::searchProviderIdentifier(m_settings.search.provider)) : m_host->translate(QStringLiteral("ai.capability.search-no")));
    values.insert(QStringLiteral("SPEECH"), speechConfigured ? m_host->translate(QStringLiteral("ai.capability.speech-yes")).arg(m_settings.speech.providerId) : m_host->translate(QStringLiteral("ai.capability.speech-no")));
    values.insert(QStringLiteral("SERVERS"), serverCatalog());
    values.insert(QStringLiteral("CONTEXT_WINDOW"), QString::number(model == nullptr ? 0 : model->contextWindow));
    values.insert(QStringLiteral("OUTPUT_BUDGET"), QString::number(ModelConnections::outputBudget(connection)));

    return AgentPrompts::renderPrompt(agent.systemPrompt, values);
}

// A model that declares no system role receives the instructions as the first user message, which is what the market does.
QJsonObject AiPlugin::instructionMessage(const AiTask& task, const AiAgent& agent, const ModelConnection& connection, const QVector<agent::ResourceDescriptor>& skills) const {
    const ProviderDescriptor* provider = ProviderCatalog::findProvider(connection.providerId);
    const QSet<ModelTrait> traits = provider == nullptr ? QSet<ModelTrait>{} : ProviderCatalog::modelTraits(*provider, connection.modelId);
    const QString role = traits.contains(ModelTrait::SystemPrompt) ? QStringLiteral("system") : QStringLiteral("user");

    return {{QStringLiteral("role"), role}, {QStringLiteral("content"), renderedSystemPrompt(task, agent, skills)}};
}

// The conversation is stored in the shape the product owns and projected into the shape the protocol names, so the same dialogue survives a change of provider.
QJsonArray AiPlugin::projectConversation(const QJsonObject& instructions, const ModelConnection& connection, const QVector<ConversationMessage>& conversation, const QHash<QString, ToolResult>& images, QVector<qint64>* sequences) const {
    const WireProtocol protocol = ModelConnections::connectionProtocol(connection);
    const ProviderDescriptor* provider = ProviderCatalog::findProvider(connection.providerId);
    const bool sees = provider != nullptr && ProviderCatalog::modelTraits(*provider, connection.modelId).contains(ModelTrait::Vision);
    QJsonArray messages{instructions};
    QVector<ToolResult> pendingResults;
    qint64 summarized = 0;

    for (const auto& message : conversation) {
        summarized = std::max(summarized, message.summarizedUntil);
    }

    if (sequences != nullptr) {
        sequences->append(0);
    }

    // clang-format off
    const auto flushResults = [&messages, &pendingResults, protocol]() {
        if (pendingResults.isEmpty()) {
            return;
        }
        for (const auto& message : ToolContracts::serializeToolResults(protocol, pendingResults)) {
            messages.append(message);
        }
        pendingResults.clear();
    };
    // clang-format on

    for (const auto& message : conversation) {
        // A turn a summary already replaced is not sent again, so the conversation is summarized once and not on every turn.
        if (message.sequence <= summarized) {
            continue;
        }
        if (message.role != ConversationRole::Tool) {
            flushResults();
        }

        switch (message.role) {
        case ConversationRole::User:
            messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), message.content}});
            break;
        case ConversationRole::Assistant:
            messages.append(ToolContracts::serializeAssistantTurn(protocol, message.content, AiPluginHelper::toolCallsFromDocument(message.toolCalls)));
            break;
        case ConversationRole::Tool: {
            const ToolResult seen = images.value(message.toolCallId);
            const QByteArray bytes = message.imageData.isEmpty() ? seen.imageData : message.imageData;
            const QByteArray mediaType = message.imageMediaType.isEmpty() ? seen.imageMediaType : message.imageMediaType;
            // A model that does not read an image is told the picture was not sent, because a result that silently loses it reads as an empty answer.
            const bool carriesPicture = !bytes.isEmpty() && sees;
            const QString text = bytes.isEmpty() || sees ? message.content : message.content + QStringLiteral("\n") + m_host->translate(QStringLiteral("ai.tool.picture-not-sent"));
            pendingResults.append({message.toolCallId, text, false, carriesPicture ? bytes : QByteArray{}, carriesPicture ? mediaType : QByteArray{}});
            break;
        }
        }

        // Each emitted message is answered by the conversation turn that produced it, so a dropped one is named back.
        if (sequences != nullptr) {
            while (sequences->size() < messages.size()) {
                sequences->append(message.sequence);
            }
        }
    }

    const qint64 newest = conversation.isEmpty() ? 0 : conversation.last().sequence;
    flushResults();

    if (sequences != nullptr) {
        while (sequences->size() < messages.size()) {
            sequences->append(newest);
        }
    }

    return messages;
}

// What the protocol demands is satisfied after the conversation is fitted, because dropping a turn can leave two turns of one role beside each other.
QJsonArray AiPlugin::shapeForProtocol(const ModelConnection& connection, const QJsonArray& messages) {
    return ToolContracts::enforceProtocolShape(ModelConnections::connectionProtocol(connection), messages);
}

// The published context files are always-on instructions the catalog already read, so the prompt opens no file of its own.
QStringList AiPlugin::contextInstructions(const QVector<agent::ResourceDescriptor>& resources) const {
    QStringList collected;

    for (const auto& resource : agent::AgentResourceCatalog::ofKind(resources, agent::ResourceKind::Context)) {
        if (!resource.content.isEmpty()) {
            collected.append(m_host->translate(QStringLiteral("ai.agent.context-file")).arg(resource.name, resource.content));
        }
    }

    return collected;
}

// The conversation is fitted to the model the run itself declares, because the selection a later card starts from is not the one this run is speaking to.
const ModelDescriptor* AiPlugin::findModelDescriptor(const ModelConnection& connection) const {
    const ProviderDescriptor* descriptor = ProviderCatalog::findProvider(connection.providerId);
    return descriptor == nullptr ? nullptr : ProviderCatalog::findModel(*descriptor, connection.modelId);
}

void AiPlugin::completeExecution(const QString& taskId, ExecutionStatus status, const QString& errorMessage, AgentStopReason stopReason) {
    const std::shared_ptr<ActiveExecution> position = m_active.value(taskId);

    if (position == nullptr || m_repository == nullptr) {
        return;
    }

    const bool cancelled = m_cancelledTaskIds.remove(taskId);
    TaskExecution record = position->record;
    // A run is priced by what it really spoke to, so the connection it ran on is recorded rather than resolved later from one that may have moved.
    record.providerId = position->connection.providerId;
    record.modelId = position->connection.modelId;
    m_phases.remove(taskId);
    m_phasesBeforeThrottle.remove(taskId);

    if (cancelled) {
        appendLog(record.id, ExecutionLogLevel::Warning, ExecutionLogKind::Cancelled);
    } else if (status == ExecutionStatus::Succeeded) {
        appendLog(record.id, stopReason == AgentStopReason::Answered ? ExecutionLogLevel::Info : ExecutionLogLevel::Warning, ExecutionLogKind::Succeeded, stopReason == AgentStopReason::Answered ? QString{} : m_host->translate(AiPluginHelper::stopReasonKey(stopReason)));
    }
    // A deadline outlives the run it was watching, so every one of them is released with the execution it belonged to.
    QStringList unfinishedCalls;

    for (const auto& pending : position->toolCalls) {
        if (pending.deadline != nullptr) {
            pending.deadline->stop();
            pending.deadline->deleteLater();
        }
        if (pending.started && !pending.finished) {
            unfinishedCalls.append(pending.call.id);
        }
    }

    AiChatClient* client = position->client;
    AiChatClient* summaryClient = position->summaryClient;
    AiCommandRunner* runner = position->runner;
    record.status = cancelled ? ExecutionStatus::Cancelled : status;
    record.stopReason = cancelled ? AgentStopReason::Cancelled : stopReason;
    record.finishedAtUtc = QDateTime::currentDateTimeUtc();
    record.errorMessage = errorMessage;
    m_active.remove(taskId);
    m_logSequences.remove(record.id);
    m_lastStatuses.insert(taskId, record.status);
    m_lastStopReasons.insert(taskId, record.stopReason);

    // The completion can be reached from a signal the worker itself is emitting, so its destruction is deferred.
    if (client != nullptr) {
        client->disconnect(this);
        client->deleteLater();
    }

    // A summary still in flight belongs to the run that asked for it, so stopping the run stops it too.
    if (summaryClient != nullptr) {
        summaryClient->disconnect(this);
        summaryClient->cancel();
        summaryClient->deleteLater();
    }

    // A command still running belongs to the run as well, and it is stopped once the run is no longer active so its answer reaches nothing.
    for (const auto& callId : unfinishedCalls) {
        if (m_tools != nullptr) {
            m_tools->cancel(callId);
        }
    }

    if (runner != nullptr) {
        runner->disconnect(this);
        runner->deleteLater();
    }

    auto persisted = m_repository->finishExecution(record);
    // clang-format off
    persisted.then(m_asyncContext.get(), [this](Result<void> result) { if (!result.hasValue()) { reportFailure(result.error(), m_host->translate(QStringLiteral("ai.error.execution-save"))); } });
    // clang-format on

    const TaskColumn target = record.status == ExecutionStatus::Succeeded ? TaskColumn::Done : TaskColumn::Todo;
    const QDateTime now = record.finishedAtUtc;
    auto future = m_repository->completeTask(taskId, target, now);
    // clang-format off
    future.then(m_asyncContext.get(), [this, taskId, target, now](Result<void> result) {
        if (!result.hasValue()) {
            reportFailure(result.error(), m_host->translate(QStringLiteral("ai.error.task-save")));
            return;
        }
        m_queue.removeAll(taskId);
        for (auto& candidate : m_tasks) {
            if (candidate.id == taskId) {
                candidate.column = target;
                candidate.updatedAtUtc = now;
            }
        }
        emit tasksChanged();
        emit taskRunStateChanged(taskId);
        dispatchQueue();
    });
    // clang-format on
}

void AiPlugin::appendLog(const QString& executionId, ExecutionLogLevel level, ExecutionLogKind kind, const QString& detail) {
    if (m_repository == nullptr) {
        return;
    }

    ExecutionLogEntry entry;
    entry.id = AiPluginHelper::identifier();
    entry.executionId = executionId;
    entry.sequence = ++m_logSequences[executionId];
    entry.timestampUtc = QDateTime::currentDateTimeUtc();
    entry.level = level;
    entry.kind = kind;
    entry.detail = detail;
    auto future = m_repository->appendExecutionLog(entry);
    // clang-format off
    future.then(m_asyncContext.get(), [this](Result<void> result) { if (!result.hasValue()) { reportFailure(result.error(), m_host->translate(QStringLiteral("ai.error.log-save"))); } });
    // clang-format on
    emit executionActivity(taskOfExecution(executionId));
}

QString AiPlugin::taskOfExecution(const QString& executionId) const {
    for (auto execution = m_active.constBegin(); execution != m_active.constEnd(); ++execution) {
        if (execution.value() != nullptr && execution.value()->record.id == executionId) {
            return execution.key();
        }
    }

    return {};
}

// The error carries the diagnostic that belongs in the log, and the message is the sentence the user reads.
void AiPlugin::reportFailure(const Error& error, const QString& message) {
    if (m_host == nullptr) {
        return;
    }

    m_host->log(LogLevel::Error, QStringLiteral("ai.tasks"), error.message, {{QStringLiteral("code"), error.code}, {QStringLiteral("detail"), error.detail}});
    m_host->notify(m_host->translate(QStringLiteral("ai.error.title")), message, AlertSeverity::Error);
}

// A due occurrence leaves the schedule before its row is written, because a scheduler that wakes again while that write is in flight would dispatch it once more.
void AiPlugin::processSchedules() {
    if (m_repository == nullptr) {
        return;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    QStringList due;

    for (const auto& candidate : m_tasks) {
        if (candidate.schedule.has_value() && candidate.schedule->enabled && candidate.schedule->nextRunAtUtc <= now && runState(candidate.id) == TaskRunState::Idle) {
            due.append(candidate.id);
        }
    }

    for (const auto& taskId : due) {
        const AiTask* candidate = task(taskId);
        if (candidate == nullptr || !candidate->schedule.has_value()) {
            continue;
        }

        const auto advanced = AiPluginHelper::advanceSchedule(candidate->schedule.value(), now);
        if (!advanced.hasValue()) {
            reportFailure(advanced.error(), m_host->translate(QStringLiteral("ai.error.schedule-save")));
            continue;
        }

        const TaskSchedule previousSchedule = candidate->schedule.value();
        const TaskColumn previousColumn = candidate->column;
        const TaskSchedule schedule = advanced.value();
        m_queue.append(taskId);
        applyScheduledDispatch(taskId, schedule, TaskColumn::Doing, now);
        emit tasksChanged();
        emit taskRunStateChanged(taskId);

        auto future = m_repository->enqueueTask(taskId, now, schedule);
        // clang-format off
        future.then(m_asyncContext.get(), [this, taskId, previousSchedule, previousColumn, now](Result<void> result) {
            if (!result.hasValue()) {
                m_queue.removeAll(taskId);
                applyScheduledDispatch(taskId, previousSchedule, previousColumn, now);
                emit tasksChanged();
                emit taskRunStateChanged(taskId);
                reportFailure(result.error(), m_host->translate(QStringLiteral("ai.error.schedule-save")));
                return;
            }
            dispatchQueue();
        });
        // clang-format on
    }

    armScheduleTimer();
}

void AiPlugin::applyScheduledDispatch(const QString& taskId, const std::optional<TaskSchedule>& schedule, TaskColumn column, const QDateTime& now) {
    for (auto& stored : m_tasks) {
        if (stored.id == taskId) {
            stored.schedule = schedule;
            stored.column = column;
            stored.updatedAtUtc = now;
        }
    }
}

void AiPlugin::armScheduleTimer() {
    m_scheduleTimer.stop();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    std::chrono::milliseconds wakeup = std::chrono::milliseconds{ProviderCatalog::aiLimits().scheduleWakeupMs};

    for (const auto& candidate : m_tasks) {
        if (!candidate.schedule.has_value() || !candidate.schedule->enabled) {
            continue;
        }
        const qint64 remaining = std::max<qint64>(0, now.msecsTo(candidate.schedule->nextRunAtUtc));
        wakeup = std::min(wakeup, std::chrono::milliseconds(remaining));
    }

    m_scheduleTimer.setInterval(wakeup);
    m_scheduleTimer.start();
}

} // namespace workpane::plugins::ai
