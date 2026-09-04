#include "AiCliChatClient.h"
#include "AiConversationView.h"
#include "AiTasksView.h"
#include "AiTestSupport.h"

#include "TestTranslations.h"
#include "persistence/StoredValues.h"
#include "ui/Components.h"

#include <QScrollArea>
#include <QScrollBar>

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

namespace workpane::plugins::ai {

TEST(CronExpressionTest, ParsesPOSIXFieldsAndCalculatesOccurrencesInTheSelectedTimeZone) {
    const auto weekdays = CronExpression::parse(QStringLiteral("0 9 * * 1-5"));
    ASSERT_TRUE(weekdays.hasValue());
    const QDateTime friday = QDateTime(QDate(2026, 8, 14), QTime(9, 0), QTimeZone::utc()).toUTC();
    const auto next = weekdays.value().nextAfter(friday, QTimeZone::utc());
    ASSERT_TRUE(next.hasValue());
    EXPECT_EQ(next.value(), QDateTime(QDate(2026, 8, 17), QTime(9, 0), QTimeZone::utc()).toUTC());

    // Both zero and seven name Sunday, so the day after a Saturday is the occurrence and the Monday after it is not.
    const auto sunday = CronExpression::parse(QStringLiteral("30 12 * * 7"));
    ASSERT_TRUE(sunday.hasValue());
    const auto firstSunday = sunday.value().nextAfter(QDateTime(QDate(2026, 8, 15), QTime(0, 0), QTimeZone::utc()).toUTC(), QTimeZone::utc());
    ASSERT_TRUE(firstSunday.hasValue());
    EXPECT_EQ(firstSunday.value(), QDateTime(QDate(2026, 8, 16), QTime(12, 30), QTimeZone::utc()).toUTC());
    const auto followingSunday = sunday.value().nextAfter(firstSunday.value(), QTimeZone::utc());
    ASSERT_TRUE(followingSunday.hasValue());
    EXPECT_EQ(followingSunday.value(), QDateTime(QDate(2026, 8, 23), QTime(12, 30), QTimeZone::utc()).toUTC());
}

TEST(CronExpressionTest, AnswersTheWallClockTheCalendarSkipsAndTheOneItRepeats) {
    const QTimeZone saoPaulo(QByteArrayLiteral("America/Sao_Paulo"));
    const QTimeZone newYork(QByteArrayLiteral("America/New_York"));
    ASSERT_TRUE(saoPaulo.isValid());
    ASSERT_TRUE(newYork.isValid());

    // The clock of New York moves from 02:00 to 03:00 on the eighth of March 2026, so 02:30 is a wall clock that day does not carry.
    const auto daily = CronExpression::parse(QStringLiteral("30 2 * * *"));
    ASSERT_TRUE(daily.hasValue());
    const QDateTime beforeGap = QDateTime(QDate(2026, 3, 7), QTime(12, 0), QTimeZone::utc()).toUTC();
    const auto afterGap = daily.value().nextAfter(beforeGap, newYork);
    ASSERT_TRUE(afterGap.hasValue());
    EXPECT_EQ(afterGap.value().toTimeZone(newYork).date(), QDate(2026, 3, 8));
    EXPECT_EQ(afterGap.value().toTimeZone(newYork).time(), QTime(3, 0));

    // The clock of New York repeats 01:00 to 02:00 on the first of November 2026, so 01:30 happens twice and the job runs on the first of them.
    const auto repeated = CronExpression::parse(QStringLiteral("30 1 * * *"));
    ASSERT_TRUE(repeated.hasValue());
    const QDateTime beforeRepeat = QDateTime(QDate(2026, 10, 31), QTime(12, 0), QTimeZone::utc()).toUTC();
    const auto firstOccurrence = repeated.value().nextAfter(beforeRepeat, newYork);
    ASSERT_TRUE(firstOccurrence.hasValue());
    EXPECT_EQ(firstOccurrence.value().toTimeZone(newYork).date(), QDate(2026, 11, 1));
    EXPECT_EQ(firstOccurrence.value().toTimeZone(newYork).time(), QTime(1, 30));
    EXPECT_EQ(firstOccurrence.value(), QDateTime(QDate(2026, 11, 1), QTime(5, 30), QTimeZone::utc()).toUTC());

    // The occurrence after it is the next day rather than the second turn of the same wall clock.
    const auto following = repeated.value().nextAfter(firstOccurrence.value(), newYork);
    ASSERT_TRUE(following.hasValue());
    EXPECT_EQ(following.value().toTimeZone(newYork).date(), QDate(2026, 11, 2));

    // An expression that matches once a year is answered without walking every minute of that year.
    const auto yearly = CronExpression::parse(QStringLiteral("0 0 29 2 *"));
    ASSERT_TRUE(yearly.hasValue());
    QElapsedTimer spent;
    spent.start();
    const auto leapDay = yearly.value().nextAfter(QDateTime(QDate(2026, 3, 1), QTime(0, 0), QTimeZone::utc()).toUTC(), saoPaulo);
    ASSERT_TRUE(leapDay.hasValue());
    EXPECT_EQ(leapDay.value().toTimeZone(saoPaulo).date(), QDate(2028, 2, 29));
    EXPECT_LT(spent.elapsed(), 500);
}

TEST(CronExpressionTest, AppliesPOSIXDayMatchingAndRejectsExtensionsOrMalformedFields) {
    // Both day fields are restricted, so the day matches when either of them does and the month still has to.
    const auto expression = CronExpression::parse(QStringLiteral("0 0 1 1 0"));
    ASSERT_TRUE(expression.hasValue());
    const auto firstOfJanuary = expression.value().nextAfter(QDateTime(QDate(2025, 12, 31), QTime(0, 0), QTimeZone::utc()).toUTC(), QTimeZone::utc());
    ASSERT_TRUE(firstOfJanuary.hasValue());
    EXPECT_EQ(firstOfJanuary.value(), QDateTime(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::utc()).toUTC());
    const auto firstSundayOfJanuary = expression.value().nextAfter(firstOfJanuary.value(), QTimeZone::utc());
    ASSERT_TRUE(firstSundayOfJanuary.hasValue());
    EXPECT_EQ(firstSundayOfJanuary.value(), QDateTime(QDate(2026, 1, 4), QTime(0, 0), QTimeZone::utc()).toUTC());
    const auto nextYear = expression.value().nextAfter(QDateTime(QDate(2026, 1, 25), QTime(0, 0), QTimeZone::utc()).toUTC(), QTimeZone::utc());
    ASSERT_TRUE(nextYear.hasValue());
    EXPECT_EQ(nextYear.value(), QDateTime(QDate(2027, 1, 1), QTime(0, 0), QTimeZone::utc()).toUTC());
    EXPECT_EQ(CronExpression::parse(QStringLiteral("*/5 * * * *")).error().code, QStringLiteral("ai_tasks_cron_value_invalid"));
    EXPECT_EQ(CronExpression::parse(QStringLiteral("0 0 * *")).error().code, QStringLiteral("ai_tasks_cron_field_count_invalid"));
    EXPECT_EQ(CronExpression::parse(QStringLiteral("0 0 31-1 * *")).error().code, QStringLiteral("ai_tasks_cron_range_invalid"));
    EXPECT_EQ(CronExpression::parse(QStringLiteral("60 * * * *")).error().code, QStringLiteral("ai_tasks_cron_value_invalid"));
}

// A reader upgrading the product keeps the workspaces and tasks they already have, because the version that records what a run spoke to only adds columns.
TEST(AiTaskRepositoryTest, KeepsTheWorkspacesAndTasksAReaderAlreadyHasWhenTheSchemaGainsItsSecondVersion) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    persistence::StateStore store(directory.filePath(QStringLiteral("workpane.sqlite3")));
    ASSERT_TRUE(store.initialize().hasValue());

    QVector<persistence::DatabaseMigration> declared;
    {
        test::TestPluginHost collecting;
        AiTaskRepository reading(collecting);
        ASSERT_TRUE(reading.initialize().hasValue());
        declared = collecting.appliedMigrations;
    }

    ASSERT_EQ(declared.size(), 2);

    // The database as the previous version of the product left it, carrying a workspace, a task and a run.
    ASSERT_TRUE(store.migratePluginDatabase(QStringLiteral("ai"), {declared.first()}).hasValue());
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString stamp = persistence::StoredValues::storedTimestamp(now);
    ASSERT_TRUE(store.executePluginDatabase(QStringLiteral("ai"), QStringLiteral("INSERT INTO ai_tasks_workspaces(id, name, position, active, created_at_utc, updated_at_utc) VALUES(?, ?, ?, ?, ?, ?)"), {QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, 1, stamp, stamp}).hasValue());
    ASSERT_TRUE(store.executePluginDatabase(QStringLiteral("ai"), QStringLiteral("INSERT INTO ai_tasks_tasks(id, workspace_id, title, description, prompt, issue_url, agent_id, execution_kind, workdir, command, command_timeout_seconds, column_name, position, created_at_utc, updated_at_utc) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"), {QStringLiteral("task-1"), QStringLiteral("workspace-1"), QStringLiteral("Ship it"), persistence::StoredValues::storedText({}), QStringLiteral("do the thing"), persistence::StoredValues::storedText({}), QStringLiteral("agent-1"), QStringLiteral("agent"), persistence::StoredValues::storedText({}), persistence::StoredValues::storedText({}), 0, QStringLiteral("todo"), 0, stamp, stamp}).hasValue());
    ASSERT_TRUE(store.executePluginDatabase(QStringLiteral("ai"), QStringLiteral("INSERT INTO ai_tasks_executions(id, task_id, status, started_at_utc, finished_at_utc, input_tokens, output_tokens, finish_reason, error_message, content, stop_reason) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"), {QStringLiteral("execution-1"), QStringLiteral("task-1"), QStringLiteral("succeeded"), stamp, stamp, 10, 20, QStringLiteral("stop"), persistence::StoredValues::storedText({}), QStringLiteral("done"), QStringLiteral("answered")}).hasValue());

    // The product opens against that database and the version it declares now reaches two.
    test::TestPluginHost host;
    host.useDatabase(store, QStringLiteral("ai"));
    AiTaskRepository repository(host);
    ASSERT_TRUE(repository.initialize().hasValue());
    EXPECT_EQ(store.pluginSchemaVersion(QStringLiteral("ai")).value(), 2);

    const auto workspaces = store.queryPluginDatabase(QStringLiteral("ai"), QStringLiteral("SELECT id, name FROM ai_tasks_workspaces"), {});
    ASSERT_TRUE(workspaces.hasValue()) << workspaces.error().message.toStdString();
    ASSERT_EQ(workspaces.value().size(), 1);
    EXPECT_EQ(workspaces.value().first().value(QStringLiteral("name")).toString(), QStringLiteral("Product"));

    const auto tasks = store.queryPluginDatabase(QStringLiteral("ai"), QStringLiteral("SELECT id, prompt FROM ai_tasks_tasks"), {});
    ASSERT_TRUE(tasks.hasValue());
    ASSERT_EQ(tasks.value().size(), 1);
    EXPECT_EQ(tasks.value().first().value(QStringLiteral("prompt")).toString(), QStringLiteral("do the thing"));

    // The run recorded before the change is still there, and the columns the change added are empty for it.
    const auto runs = store.queryPluginDatabase(QStringLiteral("ai"), QStringLiteral("SELECT id, content, provider_id, model_id FROM ai_tasks_executions"), {});
    ASSERT_TRUE(runs.hasValue());
    ASSERT_EQ(runs.value().size(), 1);
    EXPECT_EQ(runs.value().first().value(QStringLiteral("content")).toString(), QStringLiteral("done"));
    EXPECT_TRUE(runs.value().first().value(QStringLiteral("provider_id")).toString().isEmpty());
}

// A column the writer fills and the reader never selects is data the reader loses, so every field of a task travels through real SQL and back.
TEST(AiTaskRepositoryTest, CarriesEveryFieldOfATaskThroughTheDatabaseAndBack) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    persistence::StateStore store(directory.filePath(QStringLiteral("workpane.sqlite3")));
    ASSERT_TRUE(store.initialize().hasValue());

    test::TestPluginHost host;
    host.useDatabase(store, QStringLiteral("ai"));
    AiTaskRepository repository(host);
    ASSERT_TRUE(repository.initialize().hasValue());

    const QDateTime created = QDateTime::currentDateTimeUtc().addSecs(-3600);
    const QDateTime updated = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, created, updated};
    ASSERT_TRUE(test::TestFutures::awaitFuture(repository.createWorkspace(workspace)).hasValue());

    TaskSchedule schedule;
    schedule.kind = ScheduleKind::Cron;
    schedule.enabled = true;
    schedule.cronExpression = QStringLiteral("30 4 1 1 0");
    schedule.timeZoneId = QByteArrayLiteral("America/Sao_Paulo");
    schedule.nextRunAtUtc = updated.addDays(1);
    schedule.lastTriggeredAtUtc = created;

    AiTask written;
    written.id = QStringLiteral("task-1");
    written.workspaceId = workspace.id;
    written.title = QStringLiteral("Ship the report");
    written.description = QStringLiteral("Everything the reader asked for");
    written.prompt = QStringLiteral("Write the report and file it");
    written.issueUrl = QStringLiteral("https://issues.example.com/42");
    written.agentId = AiTestsHelper::testAgent().id;
    written.executionKind = TaskExecutionKind::Agent;
    written.workdir = QStringLiteral("/tmp/project");
    written.commandTimeoutSeconds = 900;
    written.column = TaskColumn::Review;
    written.position = 3;
    written.createdAtUtc = created;
    written.updatedAtUtc = updated;
    written.schedule = schedule;

    ASSERT_TRUE(test::TestFutures::awaitFuture(repository.saveTask(written)).hasValue());

    const auto tasks = repository.tasks();
    ASSERT_TRUE(tasks.hasValue());
    ASSERT_EQ(tasks.value().size(), 1);
    const AiTask read = tasks.value().first();

    EXPECT_EQ(read.id, written.id);
    EXPECT_EQ(read.workspaceId, written.workspaceId);
    EXPECT_EQ(read.title, written.title);
    EXPECT_EQ(read.description, written.description);
    EXPECT_EQ(read.prompt, written.prompt);
    EXPECT_EQ(read.issueUrl, written.issueUrl);
    EXPECT_EQ(read.agentId, written.agentId);
    EXPECT_EQ(read.executionKind, written.executionKind);
    EXPECT_EQ(read.workdir, written.workdir);
    EXPECT_EQ(read.command, written.command);
    EXPECT_EQ(read.commandTimeoutSeconds, written.commandTimeoutSeconds);
    EXPECT_EQ(read.column, written.column);
    EXPECT_EQ(read.position, written.position);
    EXPECT_EQ(read.createdAtUtc.toMSecsSinceEpoch(), written.createdAtUtc.toMSecsSinceEpoch());
    EXPECT_EQ(read.updatedAtUtc.toMSecsSinceEpoch(), written.updatedAtUtc.toMSecsSinceEpoch());

    ASSERT_TRUE(read.schedule.has_value());
    EXPECT_EQ(read.schedule->kind, schedule.kind);
    EXPECT_EQ(read.schedule->enabled, schedule.enabled);
    EXPECT_EQ(read.schedule->cronExpression, schedule.cronExpression);
    EXPECT_EQ(read.schedule->timeZoneId, schedule.timeZoneId);
    EXPECT_EQ(read.schedule->nextRunAtUtc.toMSecsSinceEpoch(), schedule.nextRunAtUtc.toMSecsSinceEpoch());
    EXPECT_EQ(read.schedule->lastTriggeredAtUtc.toMSecsSinceEpoch(), schedule.lastTriggeredAtUtc.toMSecsSinceEpoch());
}

TEST(AiTaskRepositoryTest, DeclaresConsecutiveMigrationsAndRoundTripsWorkspacesTasksAndQueue) {
    test::TestPluginHost host;
    AiTaskRepository repository(host);
    ASSERT_TRUE(repository.initialize().hasValue());
    ASSERT_EQ(host.appliedMigrations.size(), 2);

    // A version that follows the creating one evolves the schema instead of rewriting it, because what a task recorded must survive the change.
    for (int index = 0; index < host.appliedMigrations.size(); ++index) {
        EXPECT_EQ(host.appliedMigrations.at(index).version, index + 1);
    }

    for (const auto& statement : host.appliedMigrations.last().statements) {
        EXPECT_TRUE(statement.startsWith(QStringLiteral("ALTER TABLE ai_tasks_"))) << statement.toStdString();
    }

    for (const auto& statement : host.appliedMigrations.first().statements) {
        EXPECT_TRUE(statement.startsWith(QStringLiteral("CREATE "))) << statement.toStdString();
    }

    const AiWorkspace workspace = AiTestsHelper::validWorkspace(0, true);
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {task.id});

    const auto workspaces = repository.workspaces();
    ASSERT_TRUE(workspaces.hasValue());
    ASSERT_EQ(workspaces.value().size(), 1);
    EXPECT_EQ(workspaces.value().first().name, workspace.name);
    const auto tasks = repository.tasks();
    ASSERT_TRUE(tasks.hasValue());
    ASSERT_EQ(tasks.value().size(), 1);
    EXPECT_EQ(tasks.value().first().prompt, task.prompt);
    EXPECT_EQ(tasks.value().first().column, TaskColumn::Todo);
    const auto queued = repository.queuedTaskIds();
    ASSERT_TRUE(queued.hasValue());
    EXPECT_EQ(queued.value(), QStringList{task.id});

    EXPECT_TRUE(test::TestFutures::awaitFuture(repository.createWorkspace(workspace)).hasValue());
    EXPECT_TRUE(test::TestFutures::awaitFuture(repository.saveTask(task)).hasValue());
    EXPECT_TRUE(test::TestFutures::awaitFuture(repository.enqueueTask(task.id, QDateTime::currentDateTimeUtc())).hasValue());
    EXPECT_TRUE(test::TestFutures::awaitFuture(repository.completeTask(task.id, TaskColumn::Done, QDateTime::currentDateTimeUtc())).hasValue());
    AiSettings settings;
    settings.connections.append(AiTestsHelper::testConnection());
    settings.defaultConnectionKey = ModelConnections::connectionKey(AiTestsHelper::testConnection());
    settings.execution.parallelExecutions = 2;
    EXPECT_TRUE(test::TestFutures::awaitFuture(repository.saveSettings(settings)).hasValue());
    ASSERT_EQ(host.savedSettings.size(), 1);

    host.settingsDocument = host.savedSettings.first();
    const AiSettings restored = repository.settings();
    ASSERT_EQ(restored.connections.size(), 1);
    EXPECT_EQ(restored.connections.first().providerId, QStringLiteral("openai"));
    EXPECT_EQ(restored.execution.parallelExecutions, 2);
    EXPECT_EQ(restored.defaultConnectionKey, QStringLiteral("openai/gpt-4o"));

    TaskExecution execution;
    execution.id = QStringLiteral("execution-1");
    execution.taskId = task.id;
    execution.startedAtUtc = QDateTime::currentDateTimeUtc();
    EXPECT_TRUE(test::TestFutures::awaitFuture(repository.startExecution(execution)).hasValue());
    execution.status = ExecutionStatus::Succeeded;
    execution.finishedAtUtc = execution.startedAtUtc.addSecs(2);
    execution.inputTokens = 12;
    execution.outputTokens = 34;
    execution.content = QStringLiteral("answer");
    EXPECT_TRUE(test::TestFutures::awaitFuture(repository.finishExecution(execution)).hasValue());

    ExecutionLogEntry entry{QStringLiteral("log-1"), execution.id, 1, QDateTime::currentDateTimeUtc(), ExecutionLogLevel::Info, ExecutionLogKind::Started, QStringLiteral("openai / gpt-4o")};
    EXPECT_TRUE(test::TestFutures::awaitFuture(repository.appendExecutionLog(entry)).hasValue());

    execution.finishedAtUtc = execution.startedAtUtc.addSecs(-2);
    EXPECT_EQ(test::TestFutures::awaitFuture(repository.finishExecution(execution)).error().code, QStringLiteral("ai_tasks_execution_invalid"));
    entry.sequence = -1;
    EXPECT_EQ(test::TestFutures::awaitFuture(repository.appendExecutionLog(entry)).error().code, QStringLiteral("ai_tasks_execution_invalid"));
}

TEST(AiTaskRepositoryTest, RejectsInvalidWorkspacesTasksAndSchedules) {
    test::TestPluginHost host;
    AiTaskRepository repository(host);

    AiWorkspace workspace = AiTestsHelper::validWorkspace(0, true);
    workspace.name = QStringLiteral("  ");
    EXPECT_EQ(test::TestFutures::awaitFuture(repository.createWorkspace(workspace)).error().code, QStringLiteral("ai_tasks_workspace_invalid"));

    AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), QStringLiteral("workspace-0"));
    task.prompt = QStringLiteral("   ");
    EXPECT_EQ(test::TestFutures::awaitFuture(repository.saveTask(task)).error().code, QStringLiteral("ai_tasks_task_invalid"));
    task = AiTestsHelper::makeTask(QStringLiteral("task-1"), QStringLiteral("workspace-0"));
    task.title = QStringLiteral("  ");
    EXPECT_EQ(test::TestFutures::awaitFuture(repository.saveTask(task)).error().code, QStringLiteral("ai_tasks_task_invalid"));

    task = AiTestsHelper::makeTask(QStringLiteral("task-1"), QStringLiteral("workspace-0"));
    TaskSchedule schedule;
    schedule.kind = ScheduleKind::Interval;
    schedule.intervalSeconds = 30;
    schedule.timeZoneId = QTimeZone::systemTimeZoneId();
    schedule.nextRunAtUtc = QDateTime::currentDateTimeUtc().addSecs(60);
    task.schedule = schedule;
    EXPECT_EQ(test::TestFutures::awaitFuture(repository.saveTask(task)).error().code, QStringLiteral("ai_tasks_schedule_invalid"));

    EXPECT_EQ(AiTaskRepository::parseColumn(QStringLiteral("archived")).error().code, QStringLiteral("ai_tasks_column_invalid"));
    EXPECT_EQ(AiTaskRepository::columns().size(), 5);
    EXPECT_EQ(AiTaskRepository::columnName(TaskColumn::Blocked), QStringLiteral("blocked"));
    EXPECT_EQ(AiTaskRepository::columnName(TaskColumn::Review), QStringLiteral("review"));
}

TEST(AiPluginTest, PublishesCompleteMetadataAndRejectsUnknownRequests) {
    test::TestPluginHost host;
    AiTestsHelper::installAiRows(host, {AiTestsHelper::validWorkspace(0, true)}, {}, {});
    AiPlugin plugin;
    EXPECT_EQ(plugin.id(), QStringLiteral("ai"));
    EXPECT_EQ(plugin.dependencies(), QStringList{QStringLiteral("logs")});
    EXPECT_EQ(plugin.databaseSchemaVersion(), 2);
    EXPECT_EQ(plugin.navigationItems(host.theme()).size(), 1);
    EXPECT_EQ(plugin.navigationItems(host.theme()).at(0).id, QStringLiteral("tasks"));
    EXPECT_FALSE(plugin.styleSheet(host.theme()).isEmpty());
    const auto catalog = plugin.translations();
    ASSERT_TRUE(catalog.contains(QStringLiteral("en")));
    ASSERT_TRUE(catalog.contains(QStringLiteral("pt")));
    EXPECT_TRUE(catalog.value(QStringLiteral("en")).contains(QStringLiteral("ai.column.blocked")));
    EXPECT_TRUE(catalog.value(QStringLiteral("en")).contains(QStringLiteral("ai.column.review")));

    ASSERT_TRUE(plugin.initialize(host).hasValue());
    EXPECT_EQ(plugin.initialize(host).error().code, QStringLiteral("ai_tasks_already_initialized"));
    std::optional<Result<QJsonObject>> response;
    // clang-format off
    plugin.handleRequest(QStringLiteral("test"), QStringLiteral("ai.unknown"), {}, [&response](Result<QJsonObject> result) { response = std::move(result); });
    // clang-format on
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->error().code, QStringLiteral("plugin_message_topic_unknown"));
    plugin.shutdown();
}

TEST(AiPluginTest, ManagesWorkspacesAndKeepsExactlyOneActiveWorkspace) {
    test::TestPluginHost host;
    const AiWorkspace workspace = AiTestsHelper::validWorkspace(0, true);
    AiTestsHelper::installAiRows(host, {workspace}, {}, {});
    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_EQ(plugin.workspaces().size(), 1);

    const auto created = test::TestFutures::awaitFuture(plugin.createWorkspace(QStringLiteral("Product")));
    ASSERT_TRUE(created.hasValue());
    ASSERT_EQ(plugin.workspaces().size(), 2);
    EXPECT_FALSE(plugin.workspaces().at(0).active);
    EXPECT_TRUE(plugin.workspaces().at(1).active);
    EXPECT_EQ(plugin.workspaces().at(1).name, QStringLiteral("Product"));

    EXPECT_TRUE(test::TestFutures::awaitFuture(plugin.renameWorkspace(created.value(), QStringLiteral("Platform"))).hasValue());
    EXPECT_EQ(plugin.workspaces().at(1).name, QStringLiteral("Platform"));
    EXPECT_TRUE(test::TestFutures::awaitFuture(plugin.activateWorkspace(workspace.id)).hasValue());
    EXPECT_TRUE(plugin.workspaces().at(0).active);
    EXPECT_EQ(test::TestFutures::awaitFuture(plugin.createWorkspace(QStringLiteral("   "))).error().code, QStringLiteral("ai_tasks_workspace_invalid"));
    plugin.shutdown();
}

TEST(AiPluginTest, StartsWithoutAnyWorkspaceAndOffersTheEmptyStateUntilOneIsCreated) {
    test::TestPluginHost host;
    AiTestsHelper::installAiRows(host, {}, {}, {});
    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    EXPECT_TRUE(plugin.workspaces().isEmpty());
    EXPECT_TRUE(host.databaseTransactions.isEmpty());

    std::unique_ptr<QWidget> view(plugin.createNavigationView(QStringLiteral("tasks"), nullptr));
    ASSERT_NE(view, nullptr);
    view->resize(900, 600);
    view->show();
    auto* workspaces = view->findChild<ui::TabBar*>(QStringLiteral("aiWorkspaces"));
    auto* workspaceSeparator = workspaces->parentWidget()->findChild<QWidget*>(QStringLiteral("sharedDivider"));
    ASSERT_NE(workspaces, nullptr);
    ASSERT_NE(workspaceSeparator, nullptr);
    EXPECT_EQ(workspaces->count(), 0);
    EXPECT_FALSE(workspaces->isVisible());
    EXPECT_FALSE(workspaceSeparator->isVisible());
    auto* emptyState = view->findChild<QLabel*>(QStringLiteral("emptyState"));
    ASSERT_NE(emptyState, nullptr);
    EXPECT_TRUE(emptyState->isVisible());

    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.createWorkspace(QStringLiteral("Product"))).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([workspaces]() { return workspaces->count() == 1 && workspaces->isVisible(); }));
    // clang-format on
    EXPECT_FALSE(emptyState->isVisible());

    // The strip is followed by exactly one divider, and the board starts immediately below it.
    EXPECT_TRUE(workspaceSeparator->isVisible());
    EXPECT_EQ(workspaceSeparator->height(), 1);
    EXPECT_EQ(workspaceSeparator->mapTo(view.get(), QPoint(0, 0)).y(), workspaces->mapTo(view.get(), QPoint(0, 0)).y() + workspaces->height());

    view.reset();
    plugin.shutdown();
}

TEST(AiProviderCatalogTest, ReachesEveryModalityOfEveryProviderThroughOneResolver) {
    const ProviderDescriptor* openai = ProviderCatalog::findProvider(QStringLiteral("openai"));
    const ProviderDescriptor* anthropic = ProviderCatalog::findProvider(QStringLiteral("anthropic"));
    ASSERT_NE(openai, nullptr);
    ASSERT_NE(anthropic, nullptr);

    // A provider that answers a modality says so as data, and one that does not is refused rather than reached at a guessed path.
    EXPECT_EQ(ModelConnections::resolveEndpoint(openai->id, {}, ModelEndpoint::Image).value().url, QStringLiteral("https://api.openai.com/v1/images/generations"));
    EXPECT_EQ(ModelConnections::resolveEndpoint(openai->id, {}, ModelEndpoint::Speech).value().url, QStringLiteral("https://api.openai.com/v1/audio/speech"));
    EXPECT_FALSE(ModelConnections::resolveEndpoint(anthropic->id, {}, ModelEndpoint::Image).has_value());
    EXPECT_FALSE(ModelConnections::resolveEndpoint(anthropic->id, {}, ModelEndpoint::Speech).has_value());
    EXPECT_FALSE(ModelConnections::resolveEndpoint(QStringLiteral("nobody-declares-this"), {}, ModelEndpoint::Chat).has_value());

    // The caller owns which address a connection speaks to, so a self-hosted one reaches the same modality on its own.
    EXPECT_EQ(ModelConnections::resolveEndpoint(openai->id, QStringLiteral("http://127.0.0.1:9000"), ModelEndpoint::Chat).value().url, QStringLiteral("http://127.0.0.1:9000/chat/completions"));

    // The credential and the headers travel with the endpoint, so no caller assembles them a second time.
    EXPECT_EQ(ModelConnections::resolveEndpoint(openai->id, {}, ModelEndpoint::Image).value().apiKeyVariable, openai->apiKeyVariable);
    EXPECT_EQ(ModelConnections::resolveEndpoint(anthropic->id, {}, ModelEndpoint::Chat).value().httpHeaders, anthropic->httpHeaders);

    // Every provider reached over a wire answers chat, and a command line agent is invoked rather than reached.
    const QVector<const ProviderDescriptor*> chatting = ModelConnections::providersAnswering(ModelEndpoint::Chat);
    EXPECT_FALSE(chatting.isEmpty());

    // A conversation is held either at a declared endpoint or by invoking a program, and a media service holds none.
    for (const auto& provider : ProviderCatalog::providerCatalog()) {
        const bool answers = provider.endpoints.contains(ModelEndpoint::Chat) || provider.protocol == WireProtocol::CommandLine;
        EXPECT_EQ(answers, chatting.contains(&provider)) << provider.id.toStdString();
    }

    for (const auto* provider : ModelConnections::providersAnswering(ModelEndpoint::Speech)) {
        const EndpointDescriptor speech = provider->endpoints.value(ModelEndpoint::Speech);
        EXPECT_TRUE(speech.voices.isEmpty() || speech.voices.contains(speech.defaultVoice)) << provider->id.toStdString();
    }
}

TEST(AiProviderCatalogTest, DeclaresEveryProviderWithConsistentDescriptors) {
    const auto& catalog = ProviderCatalog::providerCatalog();
    ASSERT_FALSE(catalog.isEmpty());

    QSet<QString> identifiers;

    for (const auto& provider : catalog) {
        EXPECT_FALSE(provider.id.isEmpty());
        EXPECT_TRUE(provider.titleKey.startsWith(QStringLiteral("ai.provider.")));

        // A command line agent is invoked rather than requested, so it declares a program instead of an address and takes no parameter.
        if (provider.protocol == WireProtocol::CommandLine) {
            EXPECT_TRUE(provider.baseUrl.isEmpty()) << provider.id.toStdString();
            EXPECT_TRUE(provider.parameters.isEmpty()) << provider.id.toStdString();
            EXPECT_FALSE(provider.requiresApiKey) << provider.id.toStdString();
            EXPECT_FALSE(provider.commandLine.program.isEmpty()) << provider.id.toStdString();
            EXPECT_TRUE(provider.commandLine.arguments.contains(QString::fromLatin1(commandLinePromptPlaceholder))) << provider.id.toStdString();
        } else {
            EXPECT_FALSE(provider.baseUrl.isEmpty()) << provider.id.toStdString();
            EXPECT_EQ(provider.parameters.isEmpty(), !provider.endpoints.contains(ModelEndpoint::Chat)) << provider.id.toStdString();
            EXPECT_TRUE(provider.commandLine.program.isEmpty()) << provider.id.toStdString();
        }

        EXPECT_FALSE(identifiers.contains(provider.id)) << provider.id.toStdString();
        identifiers.insert(provider.id);

        for (const auto& parameter : provider.parameters) {
            EXPECT_FALSE(parameter.id.isEmpty());
            EXPECT_FALSE(parameter.field.isEmpty());
            if (parameter.type == ParameterType::Enumeration) {
                EXPECT_FALSE(parameter.options.isEmpty());
            } else {
                EXPECT_LT(parameter.minimum, parameter.maximum);
            }
        }
        for (const auto& model : provider.models) {
            EXPECT_FALSE(model.id.isEmpty());
            EXPECT_FALSE(model.displayName.isEmpty());
            EXPECT_GT(model.contextWindow, 0);
        }
    }

    EXPECT_NE(ProviderCatalog::findProvider(QStringLiteral("anthropic")), nullptr);
    EXPECT_NE(ProviderCatalog::findProvider(QStringLiteral("openai")), nullptr);
    EXPECT_NE(ProviderCatalog::findProvider(QStringLiteral("ollama")), nullptr);
    EXPECT_EQ(ProviderCatalog::findProvider(QStringLiteral("nonexistent")), nullptr);
}

// A template that lost a tag in one language would tell the model different facts depending on the interface language.
TEST(AiPromptTemplateTest, CarriesTheSameTagsInEveryLanguage) {
    const TranslationEntries english = translations::AiCatalog::english();
    const TranslationEntries portuguese = translations::AiCatalog::portuguese();
    ASSERT_FALSE(ProviderCatalog::promptTemplates().isEmpty());

    for (const auto& identifier : ProviderCatalog::promptTemplates()) {
        const QString key = QStringLiteral("ai.template.%1-body").arg(identifier);
        ASSERT_TRUE(english.contains(key)) << identifier.toStdString();
        ASSERT_TRUE(portuguese.contains(key)) << identifier.toStdString();

        const QStringList missing = AgentPrompts::unknownPromptTags(english.value(key)) + AgentPrompts::unknownPromptTags(portuguese.value(key));
        EXPECT_TRUE(missing.isEmpty()) << identifier.toStdString() << " names a tag nobody declares";

        QStringList inEnglish;
        QStringList inPortuguese;

        for (const auto& tag : AgentPrompts::promptTags()) {
            const QString mark = QStringLiteral("{{%1}}").arg(tag.name);
            if (english.value(key).contains(mark)) {
                inEnglish.append(tag.name);
            }
            if (portuguese.value(key).contains(mark)) {
                inPortuguese.append(tag.name);
            }
        }

        EXPECT_FALSE(inEnglish.isEmpty()) << identifier.toStdString() << " carries no capability tag";
        EXPECT_EQ(inEnglish, inPortuguese) << identifier.toStdString() << " declares different tags in each language";
    }
}

TEST(AiProviderCatalogTest, RejectsEveryMalformedCatalogFileInsteadOfLoadingPartOfIt) {
    QFile providersFile(QStringLiteral(":/workpane/ai/assets/providers.json"));
    ASSERT_TRUE(providersFile.open(QIODevice::ReadOnly));
    const QJsonObject shipped = QJsonDocument::fromJson(providersFile.readAll()).object();
    const QByteArray models = QJsonDocument(QJsonObject{{QStringLiteral("providers"), QJsonObject{}}}).toJson(QJsonDocument::Compact);

    // clang-format off
    const auto document = [](const QJsonObject& value) { return QJsonDocument(value).toJson(QJsonDocument::Compact); };
    const auto onlyOpenAi = [&shipped](const QJsonObject& replacement) {
        QJsonObject copy = shipped;
        copy.insert(QStringLiteral("providers"), QJsonArray{replacement});
        return copy;
    };
    // clang-format on

    const QJsonArray declaredProviders = shipped.value(QStringLiteral("providers")).toArray();
    // clang-format off
    const auto declaredProvider = [&declaredProviders](const QString& id) { for (const auto& entry : declaredProviders) { if (entry.toObject().value(QStringLiteral("id")).toString() == id) { return entry.toObject(); } } return QJsonObject{}; };
    // clang-format on
    const QJsonObject openai = declaredProvider(QStringLiteral("openai"));
    ASSERT_EQ(openai.value(QStringLiteral("id")).toString(), QStringLiteral("openai"));
    ASSERT_TRUE(ProviderCatalog::loadAiCatalog(document(onlyOpenAi(openai)), models).hasValue());

    // Every way of declaring an endpoint wrongly is refused, because a modality reached at a guessed path answers nobody.
    // clang-format off
    const auto withEndpoints = [&](const QJsonObject& endpoints) {
        QJsonObject copy = openai;
        copy.insert(QStringLiteral("endpoints"), endpoints);
        return document(onlyOpenAi(copy));
    };
    // clang-format on
    const QJsonObject chat{{QStringLiteral("path"), QStringLiteral("/chat/completions")}};
    EXPECT_TRUE(ProviderCatalog::loadAiCatalog(withEndpoints(QJsonObject{{QStringLiteral("chat"), chat}}), models).hasValue());
    EXPECT_FALSE(ProviderCatalog::loadAiCatalog(withEndpoints(QJsonObject{{QStringLiteral("chat"), QJsonObject{}}}), models).hasValue());
    EXPECT_FALSE(ProviderCatalog::loadAiCatalog(withEndpoints(QJsonObject{{QStringLiteral("chat"), QJsonObject{{QStringLiteral("path"), QStringLiteral("chat/completions")}}}}), models).hasValue());
    EXPECT_FALSE(ProviderCatalog::loadAiCatalog(withEndpoints(QJsonObject{{QStringLiteral("nobody"), chat}}), models).hasValue());
    EXPECT_FALSE(ProviderCatalog::loadAiCatalog(withEndpoints(QJsonObject{{QStringLiteral("chat"), QJsonObject{{QStringLiteral("path"), QStringLiteral("/x")}, {QStringLiteral("unknown"), true}}}}), models).hasValue());
    EXPECT_FALSE(ProviderCatalog::loadAiCatalog(withEndpoints(QJsonObject{{QStringLiteral("chat"), QJsonObject{{QStringLiteral("path"), QStringLiteral("/x")}, {QStringLiteral("voices"), QJsonArray{QStringLiteral("alloy")}}}}}), models).hasValue());
    EXPECT_FALSE(ProviderCatalog::loadAiCatalog(withEndpoints(QJsonObject{{QStringLiteral("speech"), QJsonObject{{QStringLiteral("path"), QStringLiteral("/x")}, {QStringLiteral("voices"), QJsonArray{QStringLiteral("alloy")}}, {QStringLiteral("defaultVoice"), QStringLiteral("nobody")}}}}), models).hasValue());
    EXPECT_FALSE(ProviderCatalog::loadAiCatalog(withEndpoints(QJsonObject{{QStringLiteral("chat"), QJsonArray{}}}), models).hasValue());

    // A speaking endpoint offers a closed voice set or an account catalog, never neither and never both, and always says how it is credentialed.
    // clang-format off
    const auto speaking = [](const QJsonObject& extra, const QString& removed = {}) { QJsonObject endpoint{{QStringLiteral("path"), QStringLiteral("/speak")}, {QStringLiteral("authHeader"), QStringLiteral("x-key")}, {QStringLiteral("textField"), QStringLiteral("text")}}; for (auto entry = extra.constBegin(); entry != extra.constEnd(); ++entry) { endpoint.insert(entry.key(), entry.value()); } endpoint.remove(removed); return QJsonObject{{QStringLiteral("chat"), QJsonObject{{QStringLiteral("path"), QStringLiteral("/chat/completions")}}}, {QStringLiteral("speech"), endpoint}}; };
    // clang-format on
    const QJsonObject closedSet{{QStringLiteral("voices"), QJsonArray{QStringLiteral("alloy")}}, {QStringLiteral("defaultVoice"), QStringLiteral("alloy")}};
    EXPECT_TRUE(ProviderCatalog::loadAiCatalog(withEndpoints(speaking(closedSet)), models).hasValue());
    EXPECT_TRUE(ProviderCatalog::loadAiCatalog(withEndpoints(speaking(QJsonObject{{QStringLiteral("voiceCatalogPath"), QStringLiteral("/voices")}})), models).hasValue());
    EXPECT_FALSE(ProviderCatalog::loadAiCatalog(withEndpoints(speaking({})), models).hasValue());
    EXPECT_FALSE(ProviderCatalog::loadAiCatalog(withEndpoints(speaking(QJsonObject{{QStringLiteral("voices"), QJsonArray{QStringLiteral("alloy")}}, {QStringLiteral("defaultVoice"), QStringLiteral("alloy")}, {QStringLiteral("voiceCatalogPath"), QStringLiteral("/voices")}})), models).hasValue());
    EXPECT_FALSE(ProviderCatalog::loadAiCatalog(withEndpoints(speaking(QJsonObject{{QStringLiteral("voiceCatalogPath"), QStringLiteral("/voices")}}, QStringLiteral("authHeader"))), models).hasValue());
    EXPECT_FALSE(ProviderCatalog::loadAiCatalog(withEndpoints(speaking(QJsonObject{{QStringLiteral("voiceCatalogPath"), QStringLiteral("/voices")}}, QStringLiteral("textField"))), models).hasValue());

    // A provider that holds no conversation declares no protocol, no model, no trait and no parameter, and one that answers nothing at all is refused.
    // clang-format off
    const auto media = [&openai, &document, &onlyOpenAi](const QString& removed, const QJsonObject& added) { QJsonObject copy = openai; copy.remove(QStringLiteral("protocol")); copy.remove(QStringLiteral("preferredModels")); copy.remove(QStringLiteral("userDefinedTraits")); copy.remove(QStringLiteral("parameters")); copy.insert(QStringLiteral("endpoints"), QJsonObject{{QStringLiteral("image"), QJsonObject{{QStringLiteral("path"), QStringLiteral("/images")}, {QStringLiteral("authHeader"), QStringLiteral("authorization")}, {QStringLiteral("textField"), QStringLiteral("prompt")}}}}); copy.remove(removed); for (auto entry = added.constBegin(); entry != added.constEnd(); ++entry) { copy.insert(entry.key(), entry.value()); } return document(onlyOpenAi(copy)); };
    // clang-format on
    EXPECT_TRUE(ProviderCatalog::loadAiCatalog(media({}, {}), models).hasValue());
    EXPECT_FALSE(ProviderCatalog::loadAiCatalog(media({}, QJsonObject{{QStringLiteral("preferredModels"), QJsonArray{}}}), models).hasValue());
    EXPECT_FALSE(ProviderCatalog::loadAiCatalog(media({}, QJsonObject{{QStringLiteral("userDefinedTraits"), QJsonArray{}}}), models).hasValue());
    EXPECT_FALSE(ProviderCatalog::loadAiCatalog(media({}, QJsonObject{{QStringLiteral("parameters"), QJsonArray{}}}), models).hasValue());
    EXPECT_FALSE(ProviderCatalog::loadAiCatalog(media(QStringLiteral("endpoints"), {}), models).hasValue());
    EXPECT_FALSE(ProviderCatalog::loadAiCatalog(media({}, QJsonObject{{QStringLiteral("protocol"), QStringLiteral("openai-compatible")}}), models).hasValue());

    // An endpoint answering one request declares how that request is built, and a conversation endpoint declares none of it because its client writes the body.
    // clang-format off
    const auto imaging = [](const QJsonObject& extra, const QString& removed = {}) { QJsonObject endpoint{{QStringLiteral("path"), QStringLiteral("/images")}, {QStringLiteral("authHeader"), QStringLiteral("authorization")}, {QStringLiteral("textField"), QStringLiteral("prompt")}}; for (auto entry = extra.constBegin(); entry != extra.constEnd(); ++entry) { endpoint.insert(entry.key(), entry.value()); } endpoint.remove(removed); return QJsonObject{{QStringLiteral("chat"), QJsonObject{{QStringLiteral("path"), QStringLiteral("/chat/completions")}}}, {QStringLiteral("image"), endpoint}}; };
    // clang-format on
    EXPECT_TRUE(ProviderCatalog::loadAiCatalog(withEndpoints(imaging(QJsonObject{{QStringLiteral("body"), QJsonObject{{QStringLiteral("response_format"), QStringLiteral("b64_json")}}}})), models).hasValue());
    EXPECT_FALSE(ProviderCatalog::loadAiCatalog(withEndpoints(imaging({}, QStringLiteral("authHeader"))), models).hasValue());
    EXPECT_FALSE(ProviderCatalog::loadAiCatalog(withEndpoints(imaging({}, QStringLiteral("textField"))), models).hasValue());
    EXPECT_FALSE(ProviderCatalog::loadAiCatalog(withEndpoints(QJsonObject{{QStringLiteral("chat"), QJsonObject{{QStringLiteral("path"), QStringLiteral("/chat")}, {QStringLiteral("authHeader"), QStringLiteral("authorization")}, {QStringLiteral("textField"), QStringLiteral("prompt")}}}}), models).hasValue());
    EXPECT_FALSE(ProviderCatalog::loadAiCatalog(withEndpoints(QJsonObject{{QStringLiteral("chat"), QJsonObject{{QStringLiteral("path"), QStringLiteral("/chat")}, {QStringLiteral("body"), QJsonObject{{QStringLiteral("stream"), true}}}}}}), models).hasValue());

    EXPECT_EQ(ProviderCatalog::loadAiCatalog(QByteArrayLiteral(""), models).error().code, QStringLiteral("ai_catalog_invalid"));
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(QByteArrayLiteral("{"), models).error().code, QStringLiteral("ai_catalog_invalid"));
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(QByteArrayLiteral("[]"), models).error().code, QStringLiteral("ai_catalog_invalid"));

    QJsonObject withoutLimits = shipped;
    withoutLimits.remove(QStringLiteral("limits"));
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(withoutLimits), models).error().code, QStringLiteral("ai_catalog_invalid"));

    // A prompt template is a stable lowercase identifier naming the keys that carry its text, and every other shape rejects the catalog.
    QJsonObject templates = shipped;
    templates.remove(QStringLiteral("promptTemplates"));
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(templates), models).error().code, QStringLiteral("ai_catalog_invalid"));

    // clang-format off
    const auto withTemplates = [&shipped](const QJsonValue& value) {
        QJsonObject copy = shipped;
        copy.insert(QStringLiteral("promptTemplates"), value);
        return copy;
    };
    // clang-format on

    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(withTemplates(QJsonValue(QStringLiteral("website")))), models).error().code, QStringLiteral("ai_catalog_invalid"));
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(withTemplates(QJsonArray{})), models).error().code, QStringLiteral("ai_catalog_invalid"));
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(withTemplates(QJsonArray{7})), models).error().code, QStringLiteral("ai_catalog_invalid"));
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(withTemplates(QJsonArray{QStringLiteral("Website")})), models).error().code, QStringLiteral("ai_catalog_invalid"));
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(withTemplates(QJsonArray{QStringLiteral("web site")})), models).error().code, QStringLiteral("ai_catalog_invalid"));
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(withTemplates(QJsonArray{QStringLiteral("-website")})), models).error().code, QStringLiteral("ai_catalog_invalid"));
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(withTemplates(QJsonArray{QStringLiteral("website"), QStringLiteral("website")})), models).error().code, QStringLiteral("ai_catalog_invalid"));
    EXPECT_TRUE(ProviderCatalog::loadAiCatalog(document(withTemplates(QJsonArray{QStringLiteral("website")})), models).hasValue());

    QJsonObject brokenLimits = shipped;
    QJsonObject limits = brokenLimits.value(QStringLiteral("limits")).toObject();
    limits.insert(QStringLiteral("repeatedToolCallLimit"), 0);
    brokenLimits.insert(QStringLiteral("limits"), limits);
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(brokenLimits), models).error().code, QStringLiteral("ai_catalog_invalid"));

    QJsonObject unknownField = openai;
    unknownField.insert(QStringLiteral("nonexistent"), 1);
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(onlyOpenAi(unknownField)), models).error().code, QStringLiteral("ai_catalog_invalid"));

    QJsonObject unknownProtocol = openai;
    unknownProtocol.insert(QStringLiteral("protocol"), QStringLiteral("grpc"));
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(onlyOpenAi(unknownProtocol)), models).error().code, QStringLiteral("ai_catalog_invalid"));

    QJsonObject withoutAddress = openai;
    withoutAddress.insert(QStringLiteral("baseUrl"), QStringLiteral("api.openai.com"));
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(onlyOpenAi(withoutAddress)), models).error().code, QStringLiteral("ai_catalog_invalid"));

    QJsonObject withoutVariable = openai;
    withoutVariable.insert(QStringLiteral("apiKeyVariable"), QString{});
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(onlyOpenAi(withoutVariable)), models).error().code, QStringLiteral("ai_catalog_invalid"));

    QJsonObject unknownTrait = openai;
    unknownTrait.insert(QStringLiteral("userDefinedTraits"), QJsonArray{QStringLiteral("telepathy")});
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(onlyOpenAi(unknownTrait)), models).error().code, QStringLiteral("ai_catalog_invalid"));

    QJsonObject withoutToolCalling = openai;
    withoutToolCalling.insert(QStringLiteral("userDefinedTraits"), QJsonArray{QStringLiteral("sampling")});
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(onlyOpenAi(withoutToolCalling)), models).error().code, QStringLiteral("ai_catalog_invalid"));

    QJsonObject bothKinds = openai;
    bothKinds.insert(QStringLiteral("userDefinedTraits"), QJsonArray{QStringLiteral("sampling"), QStringLiteral("reasoning"), QStringLiteral("function-calling")});
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(onlyOpenAi(bothKinds)), models).error().code, QStringLiteral("ai_catalog_invalid"));

    QJsonObject emptyFieldPath = openai;
    QJsonArray parameters = emptyFieldPath.value(QStringLiteral("parameters")).toArray();
    QJsonObject first = parameters.at(0).toObject();
    first.insert(QStringLiteral("field"), QStringLiteral("output..effort"));
    parameters.replace(0, first);
    emptyFieldPath.insert(QStringLiteral("parameters"), parameters);
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(onlyOpenAi(emptyFieldPath)), models).error().code, QStringLiteral("ai_catalog_invalid"));

    QJsonObject outOfBounds = openai;
    QJsonArray boundsParameters = outOfBounds.value(QStringLiteral("parameters")).toArray();
    QJsonObject bounded = boundsParameters.at(0).toObject();
    bounded.insert(QStringLiteral("default"), -1);
    boundsParameters.replace(0, bounded);
    outOfBounds.insert(QStringLiteral("parameters"), boundsParameters);
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(onlyOpenAi(outOfBounds)), models).error().code, QStringLiteral("ai_catalog_invalid"));

    // Two parameters sharing one identifier are accepted only when a model reaches exactly one of them.
    QJsonObject collidingParameters = openai;
    QJsonArray colliding = collidingParameters.value(QStringLiteral("parameters")).toArray();
    QJsonObject repeated = colliding.at(0).toObject();
    repeated.insert(QStringLiteral("trait"), colliding.at(1).toObject().value(QStringLiteral("trait")));
    colliding.replace(0, repeated);
    collidingParameters.insert(QStringLiteral("parameters"), colliding);
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(onlyOpenAi(collidingParameters)), models).error().code, QStringLiteral("ai_catalog_invalid"));

    QJsonObject unknownType = openai;
    QJsonArray typedParameters = unknownType.value(QStringLiteral("parameters")).toArray();
    QJsonObject typed = typedParameters.at(0).toObject();
    typed.insert(QStringLiteral("type"), QStringLiteral("boolean"));
    typedParameters.replace(0, typed);
    unknownType.insert(QStringLiteral("parameters"), typedParameters);
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(onlyOpenAi(unknownType)), models).error().code, QStringLiteral("ai_catalog_invalid"));

    QJsonObject enumerationWithoutOptions = openai;
    QJsonArray enumerationParameters = enumerationWithoutOptions.value(QStringLiteral("parameters")).toArray();
    enumerationParameters.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("mode")}, {QStringLiteral("title"), QStringLiteral("ai.parameter.mode")}, {QStringLiteral("type"), QStringLiteral("enumeration")}, {QStringLiteral("field"), QStringLiteral("mode")}, {QStringLiteral("default"), QStringLiteral("fast")}});
    enumerationWithoutOptions.insert(QStringLiteral("parameters"), enumerationParameters);
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(onlyOpenAi(enumerationWithoutOptions)), models).error().code, QStringLiteral("ai_catalog_invalid"));

    QJsonObject duplicated = shipped;
    duplicated.insert(QStringLiteral("providers"), QJsonArray{openai, openai});
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(duplicated), models).error().code, QStringLiteral("ai_catalog_invalid"));

    QJsonObject empty = shipped;
    empty.insert(QStringLiteral("providers"), QJsonArray{});
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(empty), models).error().code, QStringLiteral("ai_catalog_invalid"));

    // The model file is validated the same way, so an unknown provider or a model without tool calling rejects the catalog.
    const QByteArray foreignModels = QJsonDocument(QJsonObject{{QStringLiteral("providers"), QJsonObject{{QStringLiteral("absent"), QJsonArray{}}}}}).toJson(QJsonDocument::Compact);
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(onlyOpenAi(openai)), foreignModels).error().code, QStringLiteral("ai_catalog_invalid"));

    const QJsonObject blindModel{{QStringLiteral("id"), QStringLiteral("m1")}, {QStringLiteral("context"), 1000}, {QStringLiteral("output"), 100}, {QStringLiteral("traits"), QJsonArray{QStringLiteral("sampling")}}};
    const QByteArray withoutTools = QJsonDocument(QJsonObject{{QStringLiteral("providers"), QJsonObject{{QStringLiteral("openai"), QJsonArray{blindModel}}}}}).toJson(QJsonDocument::Compact);
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(onlyOpenAi(openai)), withoutTools).error().code, QStringLiteral("ai_catalog_invalid"));

    const QJsonObject bothTraits{{QStringLiteral("id"), QStringLiteral("m2")}, {QStringLiteral("context"), 1000}, {QStringLiteral("output"), 100}, {QStringLiteral("traits"), QJsonArray{QStringLiteral("sampling"), QStringLiteral("reasoning"), QStringLiteral("function-calling")}}};
    const QByteArray contradictory = QJsonDocument(QJsonObject{{QStringLiteral("providers"), QJsonObject{{QStringLiteral("openai"), QJsonArray{bothTraits}}}}}).toJson(QJsonDocument::Compact);
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(onlyOpenAi(openai)), contradictory).error().code, QStringLiteral("ai_catalog_invalid"));

    const QByteArray unknownModelField = QJsonDocument(QJsonObject{{QStringLiteral("providers"), QJsonObject{{QStringLiteral("openai"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("m3")}, {QStringLiteral("cost"), 1}}}}}}}).toJson(QJsonDocument::Compact);
    EXPECT_EQ(ProviderCatalog::loadAiCatalog(document(onlyOpenAi(openai)), unknownModelField).error().code, QStringLiteral("ai_catalog_invalid"));
}

// The values a request is written with, which is what every connection opens at.
constexpr int declaredOutputBudgetTokens = 8192;
constexpr double declaredTopP = 0.95;
constexpr double declaredTemperature = 1.0;

TEST(AiProviderCatalogTest, OpensEveryConnectionAtTheValuesARequestIsWrittenWith) {
    for (const auto& provider : ProviderCatalog::providerCatalog()) {
        for (const auto& parameter : provider.parameters) {
            if (parameter.id == QStringLiteral("temperature")) {
                EXPECT_DOUBLE_EQ(parameter.defaultValue.toDouble(), declaredTemperature) << provider.id.toStdString();
            }
            if (parameter.id == QStringLiteral("topP")) {
                EXPECT_DOUBLE_EQ(parameter.defaultValue.toDouble(), declaredTopP) << provider.id.toStdString();
            }
            if (parameter.id == QStringLiteral("maxOutputTokens")) {
                EXPECT_EQ(parameter.defaultValue.toInteger(), declaredOutputBudgetTokens) << provider.id.toStdString();
            }
        }
    }

    // A model that answers less than that keeps its own maximum, so no connection opens outside the range its model accepts.
    const ProviderDescriptor* openai = ProviderCatalog::findProvider(QStringLiteral("openai"));
    ASSERT_NE(openai, nullptr);

    for (const auto& model : openai->models) {
        const ModelConnection connection = ModelConnections::declaredConnection(*openai, model.id);
        const qint64 budget = connection.parameters.value(QStringLiteral("maxOutputTokens")).toInteger(-1);
        EXPECT_EQ(budget, std::min<qint64>(declaredOutputBudgetTokens, model.maximumOutputTokens)) << model.id.toStdString();
        EXPECT_TRUE(ModelConnections::validateConnection(connection).hasValue()) << model.id.toStdString();
    }
}

TEST(AiProviderCatalogTest, DeclaresTheOutputBudgetTheConversationFitterReadsByName) {
    const ProviderDescriptor* openai = ProviderCatalog::findProvider(QStringLiteral("openai"));
    ASSERT_NE(openai, nullptr);

    const auto sampling = ProviderCatalog::outputBudgetParameter(*openai, QStringLiteral("gpt-4o"));
    ASSERT_TRUE(sampling.has_value());
    EXPECT_EQ(sampling->field, QStringLiteral("max_tokens"));

    const auto reasoning = ProviderCatalog::outputBudgetParameter(*openai, QStringLiteral("o3-mini"));
    ASSERT_TRUE(reasoning.has_value());
    EXPECT_EQ(reasoning->field, QStringLiteral("max_completion_tokens"));

    // The declared maximum of the model bounds the budget, and a borrowed budget is clamped instead of becoming invalid.
    const ModelDescriptor* model = ProviderCatalog::findModel(*openai, QStringLiteral("gpt-4o"));
    ASSERT_NE(model, nullptr);
    ModelConnection connection = AiTestsHelper::testConnection();

    // A zero asks for everything the model allows, so the maximum that model declares is what reaches the service.
    ASSERT_TRUE(sampling->modelMaximumWhenZero);
    connection.parameters.insert(QStringLiteral("maxOutputTokens"), 0);
    ASSERT_TRUE(ModelConnections::validateConnection(connection).hasValue());
    EXPECT_EQ(ModelConnections::outputBudget(connection), model->maximumOutputTokens);

    // clang-format off
    const QJsonObject wide = ChatRequests::buildRequestBody(*openai, {connection, {}, QJsonArray{}, {}}, [](const QString& key) { return key; });
    // clang-format on
    EXPECT_EQ(wide.value(QStringLiteral("max_tokens")).toInteger(0), model->maximumOutputTokens);

    // The Anthropic API requires that field, so a zero must never reach it as an absence.
    const ProviderDescriptor* anthropic = ProviderCatalog::findProvider(QStringLiteral("anthropic"));
    ASSERT_NE(anthropic, nullptr);
    const QString claude = QStringLiteral("claude-opus-5");
    const ModelDescriptor* opus = ProviderCatalog::findModel(*anthropic, claude);
    ASSERT_NE(opus, nullptr);
    ModelConnection anthropicConnection = ModelConnections::declaredConnection(*anthropic, claude);
    const auto anthropicBudget = ProviderCatalog::outputBudgetParameter(*anthropic, claude);
    ASSERT_TRUE(anthropicBudget.has_value());
    EXPECT_EQ(anthropicConnection.parameters.value(QStringLiteral("maxOutputTokens")).toInteger(-1), anthropicBudget->defaultValue.toInteger());
    anthropicConnection.parameters.insert(QStringLiteral("maxOutputTokens"), 0);
    // clang-format off
    const QJsonObject anthropicBody = ChatRequests::buildRequestBody(*anthropic, {anthropicConnection, {}, QJsonArray{}, {}}, [](const QString& key) { return key; });
    // clang-format on
    EXPECT_EQ(anthropicBody.value(QStringLiteral("max_tokens")).toInteger(0), opus->maximumOutputTokens);

    connection.parameters.insert(QStringLiteral("maxOutputTokens"), 2048);
    EXPECT_EQ(ModelConnections::outputBudget(connection), 2048);
    // clang-format off
    const QJsonObject bounded = ChatRequests::buildRequestBody(*openai, {connection, {}, QJsonArray{}, {}}, [](const QString& key) { return key; });
    // clang-format on
    EXPECT_EQ(bounded.value(QStringLiteral("max_tokens")).toInteger(0), 2048);

    ModelConnections::setOutputBudget(connection, model->maximumOutputTokens + 1000);
    EXPECT_EQ(ModelConnections::outputBudget(connection), model->maximumOutputTokens);
    EXPECT_TRUE(ModelConnections::validateConnection(connection).hasValue());

    // A model the catalog does not declare opens at the declared budget and has no maximum to ask for, so a zero typed by hand is refused.
    ModelConnection unknown = connection;
    unknown.modelId = QStringLiteral("z-ai/glm-5.2");
    unknown.parameters = ProviderCatalog::defaultParameters(*openai, unknown.modelId);
    EXPECT_EQ(unknown.parameters.value(QStringLiteral("maxOutputTokens")).toInteger(-1), declaredOutputBudgetTokens);
    EXPECT_TRUE(ModelConnections::validateConnection(unknown).hasValue());
    unknown.parameters.insert(QStringLiteral("maxOutputTokens"), 0);
    EXPECT_EQ(ModelConnections::validateConnection(unknown).error().code, QStringLiteral("ai_output_budget_unknown"));

    unknown.parameters.insert(QStringLiteral("maxOutputTokens"), 32000);
    ASSERT_TRUE(ModelConnections::validateConnection(unknown).hasValue());
    // clang-format off
    const QJsonObject typed = ChatRequests::buildRequestBody(*openai, {unknown, {}, QJsonArray{}, {}}, [](const QString& key) { return key; });
    // clang-format on
    EXPECT_EQ(typed.value(QStringLiteral("max_tokens")).toInteger(0), 32000);

    EXPECT_EQ(ModelConnections::connectionProtocol(connection), WireProtocol::OpenAiCompatible);
    EXPECT_EQ(ModelConnections::connectionProtocol(ModelConnections::declaredConnection(*ProviderCatalog::findProvider(QStringLiteral("anthropic")), QStringLiteral("claude-opus-5"))), WireProtocol::Anthropic);
}

TEST(AiProviderCatalogTest, LoadsEveryModelDeclaredByTheCatalogFile) {
    ASSERT_TRUE(ProviderCatalog::aiCatalogError().hasValue()) << ProviderCatalog::aiCatalogError().error().message.toStdString() << " " << ProviderCatalog::aiCatalogError().error().detail.toStdString();

    QFile file(QStringLiteral(":/workpane/ai/assets/models.json"));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QJsonObject declared = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("providers")).toObject();
    ASSERT_FALSE(declared.isEmpty());

    // Every provider the file names exists, and every model it declares reached the catalog with its own window.
    for (auto entry = declared.constBegin(); entry != declared.constEnd(); ++entry) {
        const ProviderDescriptor* provider = ProviderCatalog::findProvider(entry.key());
        ASSERT_NE(provider, nullptr) << entry.key().toStdString();
        for (const auto& value : entry.value().toArray()) {
            const QJsonObject model = value.toObject();
            const ModelDescriptor* found = ProviderCatalog::findModel(*provider, model.value(QStringLiteral("id")).toString());
            ASSERT_NE(found, nullptr) << entry.key().toStdString() << " / " << model.value(QStringLiteral("id")).toString().toStdString();
            EXPECT_EQ(found->contextWindow, model.value(QStringLiteral("context")).toInt());
            EXPECT_EQ(found->maximumOutputTokens, model.value(QStringLiteral("output")).toInt());
            EXPECT_EQ(found->traits.contains(ModelTrait::FunctionCalling), model.value(QStringLiteral("traits")).toArray().contains(QStringLiteral("function-calling")));
            EXPECT_FALSE(found->displayName.isEmpty());
            // A price the file publishes reaches the catalog, and one it does not publish stays absent rather than reading as free.
            EXPECT_EQ(found->inputCostPerToken.has_value(), model.contains(QStringLiteral("inputCost")));
            EXPECT_EQ(found->outputCostPerToken.has_value(), model.contains(QStringLiteral("outputCost")));
            if (found->inputCostPerToken.has_value()) {
                EXPECT_DOUBLE_EQ(found->inputCostPerToken.value(), model.value(QStringLiteral("inputCost")).toDouble());
            }
        }
    }

    // What a run of that many tokens cost is answered from the price the catalog carries, and a model nobody priced answers nothing.
    const ProviderDescriptor* openai = ProviderCatalog::findProvider(QStringLiteral("openai"));
    ASSERT_NE(openai, nullptr);
    const ModelDescriptor* priced = ProviderCatalog::findModel(*openai, QStringLiteral("gpt-4o"));
    ASSERT_NE(priced, nullptr);
    ASSERT_TRUE(priced->inputCostPerToken.has_value());
    const auto spent = ProviderCatalog::runCost(QStringLiteral("openai"), QStringLiteral("gpt-4o"), 1000, 500);
    ASSERT_TRUE(spent.has_value());
    EXPECT_DOUBLE_EQ(spent.value(), 1000.0 * priced->inputCostPerToken.value() + 500.0 * priced->outputCostPerToken.value());
    EXPECT_FALSE(ProviderCatalog::runCost(QStringLiteral("openai"), QStringLiteral("a-model-nobody-declares"), 10, 10).has_value());

    // A command line agent answers with the same model the service does, so its window and its output bound are the ones that model publishes.
    for (const auto& descriptor : ProviderCatalog::providerCatalog()) {
        if (descriptor.protocol != WireProtocol::CommandLine) {
            continue;
        }
        for (const auto& model : descriptor.models) {
            EXPECT_GT(model.contextWindow, 0) << descriptor.id.toStdString() << " / " << model.id.toStdString();
            EXPECT_GT(model.maximumOutputTokens, 0) << descriptor.id.toStdString() << " / " << model.id.toStdString();
        }
    }

    // A command line agent is invoked rather than billed, so no run of one reports a cost.
    for (const auto& descriptor : ProviderCatalog::providerCatalog()) {
        if (descriptor.protocol != WireProtocol::CommandLine) {
            continue;
        }
        for (const auto& model : descriptor.models) {
            EXPECT_FALSE(ProviderCatalog::runCost(descriptor.id, model.id, 1000, 500).has_value()) << descriptor.id.toStdString() << " / " << model.id.toStdString();
        }
    }
    EXPECT_FALSE(ProviderCatalog::runCost(QStringLiteral("a-provider-nobody-declares"), QStringLiteral("gpt-4o"), 10, 10).has_value());

    // The tunable limits come from the catalog as well, each inside the range that keeps it sane.
    EXPECT_GT(ProviderCatalog::aiLimits().repeatedToolCallLimit, 0);
    EXPECT_GT(ProviderCatalog::aiLimits().summaryMaximumTokens, 0);
    EXPECT_GT(ProviderCatalog::aiLimits().toolDeadlineMs, 0);
    EXPECT_GT(ProviderCatalog::aiLimits().requestTimeoutMs, 0);
    EXPECT_GT(ProviderCatalog::aiLimits().discoveryTimeoutMs, 0);
    EXPECT_GT(ProviderCatalog::aiLimits().serverStartTimeoutMs, 0);
    EXPECT_GT(ProviderCatalog::aiLimits().scheduleWakeupMs, 0);
    EXPECT_GT(ProviderCatalog::aiLimits().maximumAgentIterations, 0);
    EXPECT_GT(ProviderCatalog::aiLimits().maximumCommandTimeoutSeconds, 0);
    EXPECT_GT(ProviderCatalog::aiLimits().maximumParallelExecutions, 0);
    EXPECT_GT(ProviderCatalog::aiLimits().maximumSamplingTokens, 0);

    const ProviderDescriptor* openAi = ProviderCatalog::findProvider(QStringLiteral("openai"));
    ASSERT_NE(openAi, nullptr);
    const ModelDescriptor* recent = ProviderCatalog::findModel(*openAi, QStringLiteral("gpt-5"));
    ASSERT_NE(recent, nullptr);
    EXPECT_TRUE(recent->traits.contains(ModelTrait::Reasoning));
    EXPECT_TRUE(recent->traits.contains(ModelTrait::Vision));
    EXPECT_GT(recent->contextWindow, 200000);

    // Nothing reached over a wire is offered that cannot be run, while a command line agent runs its own tools and declares none.
    for (const auto& provider : ProviderCatalog::providerCatalog()) {
        const bool overAWire = provider.protocol != WireProtocol::CommandLine;

        for (const auto& model : provider.models) {
            EXPECT_EQ(model.traits.contains(ModelTrait::FunctionCalling), overAWire) << provider.id.toStdString() << " / " << model.id.toStdString();
        }

        for (const auto& preferred : provider.preferredModels) {
            const ModelDescriptor* model = ProviderCatalog::findModel(provider, preferred);
            ASSERT_NE(model, nullptr) << provider.id.toStdString() << " / " << preferred.toStdString();
            EXPECT_EQ(model->traits.contains(ModelTrait::FunctionCalling), overAWire) << provider.id.toStdString() << " / " << preferred.toStdString();
        }
    }

    // A self-hosted endpoint accepts the tools field for whatever model it serves, and seeing an image stays with the model.
    const ProviderDescriptor* selfHosted = ProviderCatalog::findProvider(QStringLiteral("ollama"));
    ASSERT_NE(selfHosted, nullptr);
    EXPECT_TRUE(ProviderCatalog::modelTraits(*selfHosted, QStringLiteral("llama3.1")).contains(ModelTrait::FunctionCalling));
    EXPECT_FALSE(ProviderCatalog::modelTraits(*selfHosted, QStringLiteral("llama3.1")).contains(ModelTrait::Vision));
}

TEST(AiProviderCatalogTest, SelectsParametersFromTheTraitsOfTheChosenModel) {
    const ProviderDescriptor* openai = ProviderCatalog::findProvider(QStringLiteral("openai"));
    ASSERT_NE(openai, nullptr);

    QStringList sampling;

    for (const auto& parameter : ProviderCatalog::applicableParameters(*openai, QStringLiteral("gpt-4o"))) {
        sampling.append(parameter.id);
    }

    EXPECT_TRUE(sampling.contains(QStringLiteral("temperature")));
    EXPECT_FALSE(sampling.contains(QStringLiteral("reasoningEffort")));

    QStringList reasoning;

    for (const auto& parameter : ProviderCatalog::applicableParameters(*openai, QStringLiteral("o3-mini"))) {
        reasoning.append(parameter.id);
    }

    EXPECT_TRUE(reasoning.contains(QStringLiteral("reasoningEffort")));
    EXPECT_FALSE(reasoning.contains(QStringLiteral("temperature")));
    EXPECT_TRUE(reasoning.contains(QStringLiteral("maxOutputTokens")));

    // A model outside the catalog keeps the trait set its provider declares instead of an inferred capability.
    EXPECT_EQ(ProviderCatalog::modelTraits(*openai, QStringLiteral("some-future-model")), openai->userDefinedModelTraits);
}

TEST(AiModelConnectionTest, ValidatesParametersAgainstTypeBoundsAndDeclaredFields) {
    const ProviderDescriptor* openai = ProviderCatalog::findProvider(QStringLiteral("openai"));
    ASSERT_NE(openai, nullptr);
    const QString model = QStringLiteral("gpt-4o");

    QJsonObject valid = ProviderCatalog::defaultParameters(*openai, model);
    ASSERT_TRUE(ModelConnections::validateParameters(*openai, model, valid).hasValue());

    QJsonObject aboveMaximum = valid;
    aboveMaximum.insert(QStringLiteral("temperature"), 9.0);
    EXPECT_EQ(ModelConnections::validateParameters(*openai, model, aboveMaximum).error().code, QStringLiteral("ai_parameter_invalid"));

    QJsonObject fractionalInteger = valid;
    fractionalInteger.insert(QStringLiteral("maxOutputTokens"), 12.5);
    EXPECT_EQ(ModelConnections::validateParameters(*openai, model, fractionalInteger).error().code, QStringLiteral("ai_parameter_invalid"));

    QJsonObject unknownField = valid;
    unknownField.insert(QStringLiteral("nonexistent"), 1);
    EXPECT_EQ(ModelConnections::validateParameters(*openai, model, unknownField).error().code, QStringLiteral("ai_parameter_unknown"));

    QJsonObject missingField = valid;
    missingField.remove(QStringLiteral("temperature"));
    EXPECT_EQ(ModelConnections::validateParameters(*openai, model, missingField).error().code, QStringLiteral("ai_parameter_missing"));

    // A sampling field is not declared for a reasoning model, so offering it is rejected instead of silently dropped.
    QJsonObject reasoningWithTemperature = ProviderCatalog::defaultParameters(*openai, QStringLiteral("o3-mini"));
    reasoningWithTemperature.insert(QStringLiteral("temperature"), 0.5);
    EXPECT_EQ(ModelConnections::validateParameters(*openai, QStringLiteral("o3-mini"), reasoningWithTemperature).error().code, QStringLiteral("ai_parameter_unknown"));

    const ProviderDescriptor* anthropic = ProviderCatalog::findProvider(QStringLiteral("anthropic"));
    ASSERT_NE(anthropic, nullptr);
    QJsonObject anthropicSampling = ProviderCatalog::defaultParameters(*anthropic, QStringLiteral("claude-3-haiku-20240307"));
    anthropicSampling.insert(QStringLiteral("temperature"), 1.5);
    EXPECT_EQ(ModelConnections::validateParameters(*anthropic, QStringLiteral("claude-3-haiku-20240307"), anthropicSampling).error().code, QStringLiteral("ai_parameter_invalid"));
}

TEST(AiSecretTest, ResolvesLiteralSecretsAndEnvironmentReferences) {
    EXPECT_FALSE(Secrets::isEnvironmentReference(QStringLiteral("sk-literal-value")));
    EXPECT_TRUE(Secrets::isEnvironmentReference(QStringLiteral("{env.WORKPANE_TEST_KEY}")));
    EXPECT_FALSE(Secrets::isEnvironmentReference(QStringLiteral("{env.}")));
    EXPECT_FALSE(Secrets::isEnvironmentReference(QStringLiteral("prefix {env.NAME}")));
    EXPECT_EQ(Secrets::environmentReferenceName(QStringLiteral("{env.WORKPANE_TEST_KEY}")), QStringLiteral("WORKPANE_TEST_KEY"));

    EXPECT_EQ(Secrets::resolveSecret(QStringLiteral("sk-literal-value")).value(), QStringLiteral("sk-literal-value"));
    EXPECT_EQ(Secrets::resolveSecret(QStringLiteral("{env.WORKPANE_ABSENT_KEY}")).error().code, QStringLiteral("ai_secret_environment_missing"));

    qputenv("WORKPANE_TEST_KEY", QByteArrayLiteral("resolved-secret"));
    EXPECT_EQ(Secrets::resolveSecret(QStringLiteral("{env.WORKPANE_TEST_KEY}")).value(), QStringLiteral("resolved-secret"));
    qunsetenv("WORKPANE_TEST_KEY");
}

TEST(AiModelConnectionTest, RejectsInvalidConnections) {
    const ProviderDescriptor* openai = ProviderCatalog::findProvider(QStringLiteral("openai"));
    ASSERT_NE(openai, nullptr);
    const ModelConnection connection = AiTestsHelper::testConnection();
    ASSERT_TRUE(ModelConnections::validateConnection(connection).hasValue());
    EXPECT_EQ(ModelConnections::connectionKey(connection), QStringLiteral("openai/gpt-4o"));
    EXPECT_EQ(ModelConnections::connectionLabel(connection), QStringLiteral("openai/gpt-4o"));

    ModelConnection named = connection;
    named.displayName = QStringLiteral("Fast reviewer");
    EXPECT_EQ(ModelConnections::connectionLabel(named), QStringLiteral("Fast reviewer"));

    ModelConnection unknownProvider = connection;
    unknownProvider.providerId = QStringLiteral("nonexistent");
    EXPECT_EQ(ModelConnections::validateConnection(unknownProvider).error().code, QStringLiteral("ai_provider_unknown"));

    ModelConnection emptyModel = connection;
    emptyModel.modelId = QStringLiteral("   ");
    EXPECT_EQ(ModelConnections::validateConnection(emptyModel).error().code, QStringLiteral("ai_model_invalid"));

    ModelConnection withoutKey = connection;
    withoutKey.apiKey.clear();
    EXPECT_EQ(ModelConnections::validateConnection(withoutKey).error().code, QStringLiteral("ai_api_key_missing"));

    ModelConnection withAddress = connection;
    withAddress.address = QStringLiteral("http://localhost:1234/v1");
    EXPECT_EQ(ModelConnections::validateConnection(withAddress).error().code, QStringLiteral("ai_address_not_configurable"));

    ModelConnection withExtra = connection;
    withExtra.extraParameters = QJsonObject{{QStringLiteral("seed"), 42}};
    EXPECT_TRUE(ModelConnections::validateConnection(withExtra).hasValue());

    ModelConnection emptyExtraKey = connection;
    emptyExtraKey.extraParameters = QJsonObject{{QStringLiteral("  "), 1}};
    EXPECT_EQ(ModelConnections::validateConnection(emptyExtraKey).error().code, QStringLiteral("ai_extra_parameter_invalid"));

    // A local runtime declares that it needs no credential and owns its own address.
    const ProviderDescriptor* ollama = ProviderCatalog::findProvider(QStringLiteral("ollama"));
    ASSERT_NE(ollama, nullptr);
    const QString localModel = QStringLiteral("llama3.1");
    ModelConnection local = ModelConnections::declaredConnection(*ollama, localModel);
    EXPECT_TRUE(local.apiKey.isEmpty());
    EXPECT_EQ(local.address, ollama->baseUrl);
    EXPECT_TRUE(ModelConnections::validateConnection(local).hasValue());
    EXPECT_EQ(ModelConnections::connectionAddress(local), ollama->baseUrl);

    ModelConnection movedLocal = local;
    movedLocal.address = QStringLiteral("http://10.0.0.4:11434/v1");
    ASSERT_TRUE(ModelConnections::validateConnection(movedLocal).hasValue());
    EXPECT_EQ(ModelConnections::connectionAddress(movedLocal), QStringLiteral("http://10.0.0.4:11434/v1"));

    ModelConnection withoutAddress = local;
    withoutAddress.address.clear();
    EXPECT_EQ(ModelConnections::validateConnection(withoutAddress).error().code, QStringLiteral("ai_address_invalid"));

    ModelConnection notAnAddress = local;
    notAnAddress.address = QStringLiteral("ftp://localhost");
    EXPECT_EQ(ModelConnections::validateConnection(notAnAddress).error().code, QStringLiteral("ai_address_invalid"));
}

TEST(AiModelConnectionTest, RejectsTwoConnectionsSharingOneKey) {
    QVector<ModelConnection> connections{AiTestsHelper::testConnection(), AiTestsHelper::testConnection()};
    connections[1].displayName = QStringLiteral("Second");
    EXPECT_EQ(ModelConnections::validateConnectionSet(connections).error().code, QStringLiteral("ai_connection_duplicate"));

    connections[1].modelId = QStringLiteral("gpt-4o-mini");
    connections[1].parameters = ProviderCatalog::defaultParameters(*ProviderCatalog::findProvider(QStringLiteral("openai")), QStringLiteral("gpt-4o-mini"));
    EXPECT_TRUE(ModelConnections::validateConnectionSet(connections).hasValue());
    EXPECT_NE(ModelConnections::findConnection(connections, QStringLiteral("openai/gpt-4o-mini")), nullptr);
    EXPECT_EQ(ModelConnections::findConnection(connections, QStringLiteral("openai/absent")), nullptr);
}

TEST(AiChatClientTest, BuildsTheBodyEachWireProtocolAndModelKindRequires) {
    const ProviderDescriptor* openai = ProviderCatalog::findProvider(QStringLiteral("openai"));
    const ProviderDescriptor* anthropic = ProviderCatalog::findProvider(QStringLiteral("anthropic"));
    ASSERT_NE(openai, nullptr);
    ASSERT_NE(anthropic, nullptr);

    // The budget is set explicitly, because the declared default leaves it to the service and the field out of the body.
    ModelConnection sampling{openai->id, QStringLiteral("gpt-4o"), {}, QStringLiteral("sk"), {}, ProviderCatalog::defaultParameters(*openai, QStringLiteral("gpt-4o")), {}};
    sampling.parameters.insert(QStringLiteral("maxOutputTokens"), 4096);
    // clang-format off
    const QJsonObject samplingBody = ChatRequests::buildRequestBody(*openai, {sampling, {}, QJsonArray{}, {}}, [](const QString& key) { return key; });
    // clang-format on
    EXPECT_EQ(samplingBody.value(QStringLiteral("model")).toString(), QStringLiteral("gpt-4o"));
    EXPECT_TRUE(samplingBody.contains(QStringLiteral("max_tokens")));
    EXPECT_TRUE(samplingBody.contains(QStringLiteral("temperature")));
    EXPECT_FALSE(samplingBody.contains(QStringLiteral("max_completion_tokens")));
    EXPECT_TRUE(samplingBody.value(QStringLiteral("stream")).toBool());

    ModelConnection reasoning{openai->id, QStringLiteral("o3-mini"), {}, QStringLiteral("sk"), {}, ProviderCatalog::defaultParameters(*openai, QStringLiteral("o3-mini")), {}};
    reasoning.parameters.insert(QStringLiteral("maxOutputTokens"), 4096);
    // clang-format off
    const QJsonObject reasoningBody = ChatRequests::buildRequestBody(*openai, {reasoning, {}, QJsonArray{}, {}}, [](const QString& key) { return key; });
    // clang-format on
    EXPECT_TRUE(reasoningBody.contains(QStringLiteral("max_completion_tokens")));
    EXPECT_TRUE(reasoningBody.contains(QStringLiteral("reasoning_effort")));
    EXPECT_FALSE(reasoningBody.contains(QStringLiteral("temperature")));
    EXPECT_FALSE(reasoningBody.contains(QStringLiteral("max_tokens")));

    const QString reasoningModel = QStringLiteral("claude-sonnet-5");
    ModelConnection anthropicReasoning{anthropic->id, reasoningModel, {}, QStringLiteral("sk"), {}, ProviderCatalog::defaultParameters(*anthropic, reasoningModel), {}};
    anthropicReasoning.parameters.insert(QStringLiteral("maxOutputTokens"), 4096);
    // clang-format off
    const QJsonObject anthropicReasoningBody = ChatRequests::buildRequestBody(*anthropic, {anthropicReasoning, {}, QJsonArray{}, {}}, [](const QString& key) { return key; });
    // clang-format on

    // The model controls its thinking through the output configuration and rejects the older thinking block.
    EXPECT_FALSE(anthropicReasoningBody.contains(QStringLiteral("thinking")));
    ASSERT_TRUE(anthropicReasoningBody.value(QStringLiteral("output_config")).isObject());
    EXPECT_EQ(anthropicReasoningBody.value(QStringLiteral("output_config")).toObject().value(QStringLiteral("effort")).toString(), QStringLiteral("medium"));
    EXPECT_TRUE(anthropicReasoningBody.contains(QStringLiteral("max_tokens")));
    EXPECT_FALSE(anthropicReasoningBody.contains(QStringLiteral("temperature")));

    // Every modality is reached through one resolver, so the path a provider answers on is data rather than a branch in the client.
    ASSERT_TRUE(ModelConnections::resolveEndpoint(openai->id, {}, ModelEndpoint::Chat).has_value());
    EXPECT_EQ(ModelConnections::resolveEndpoint(openai->id, {}, ModelEndpoint::Chat).value().url, QStringLiteral("https://api.openai.com/v1/chat/completions"));
    ASSERT_TRUE(ModelConnections::resolveEndpoint(anthropic->id, {}, ModelEndpoint::Chat).has_value());
    EXPECT_EQ(ModelConnections::resolveEndpoint(anthropic->id, {}, ModelEndpoint::Chat).value().url, QStringLiteral("https://api.anthropic.com/v1/messages"));
}

TEST(AiChatClientTest, MergesTheExtraParametersOverTheDeclaredFields) {
    const ProviderDescriptor* openai = ProviderCatalog::findProvider(QStringLiteral("openai"));
    const ProviderDescriptor* anthropic = ProviderCatalog::findProvider(QStringLiteral("anthropic"));
    ASSERT_NE(openai, nullptr);
    ASSERT_NE(anthropic, nullptr);

    ModelConnection connection{openai->id, QStringLiteral("gpt-4o"), {}, QStringLiteral("sk"), {}, ProviderCatalog::defaultParameters(*openai, QStringLiteral("gpt-4o")), {}};
    connection.extraParameters = QJsonObject{{QStringLiteral("seed"), 42}, {QStringLiteral("stop"), QJsonArray{QStringLiteral("END")}}, {QStringLiteral("temperature"), 0.25}, {QStringLiteral("top_p"), QJsonValue::Null}, {QStringLiteral("logit_bias.42"), -100}, {QStringLiteral("strict"), true}, {QStringLiteral("user"), QStringLiteral("paulo")}};
    // clang-format off
    const QJsonObject body = ChatRequests::buildRequestBody(*openai, {connection, {}, QJsonArray{}, {}}, [](const QString& key) { return key; });
    // clang-format on

    EXPECT_EQ(body.value(QStringLiteral("seed")).toInt(), 42);
    EXPECT_EQ(body.value(QStringLiteral("stop")).toArray().first().toString(), QStringLiteral("END"));
    EXPECT_DOUBLE_EQ(body.value(QStringLiteral("temperature")).toDouble(), 0.25);
    EXPECT_FALSE(body.contains(QStringLiteral("top_p")));
    EXPECT_EQ(body.value(QStringLiteral("logit_bias")).toObject().value(QStringLiteral("42")).toInt(), -100);
    EXPECT_TRUE(body.value(QStringLiteral("strict")).toBool());
    EXPECT_EQ(body.value(QStringLiteral("user")).toString(), QStringLiteral("paulo"));

    // Removing the only field of a nested object leaves no empty object behind it.
    const QString reasoningModel = QStringLiteral("claude-sonnet-5");
    ModelConnection anthropicConnection{anthropic->id, reasoningModel, {}, QStringLiteral("sk"), {}, ProviderCatalog::defaultParameters(*anthropic, reasoningModel), {}};
    anthropicConnection.extraParameters = QJsonObject{{QStringLiteral("output_config.effort"), QJsonValue::Null}};
    // clang-format off
    const QJsonObject trimmed = ChatRequests::buildRequestBody(*anthropic, {anthropicConnection, {}, QJsonArray{}, {}}, [](const QString& key) { return key; });
    // clang-format on
    EXPECT_FALSE(trimmed.contains(QStringLiteral("output_config")));
}

TEST(AiChatClientTest, LiftsTheInstructionsOutOfTheAnthropicConversation) {
    const ProviderDescriptor* anthropic = ProviderCatalog::findProvider(QStringLiteral("anthropic"));
    ASSERT_NE(anthropic, nullptr);
    const QString model = QStringLiteral("claude-3-haiku-20240307");
    const ModelConnection connection{anthropic->id, model, {}, QStringLiteral("sk"), {}, ProviderCatalog::defaultParameters(*anthropic, model), {}};
    const QJsonArray messages{QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), QStringLiteral("Be brief")}}, QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("Hello")}}};

    // clang-format off
    const QJsonObject body = ChatRequests::buildRequestBody(*anthropic, {connection, {}, messages, {}}, [](const QString& key) { return key; });
    // clang-format on
    EXPECT_EQ(body.value(QStringLiteral("system")).toString(), QStringLiteral("Be brief"));
    ASSERT_EQ(body.value(QStringLiteral("messages")).toArray().size(), 1);
    EXPECT_EQ(body.value(QStringLiteral("messages")).toArray().first().toObject().value(QStringLiteral("role")).toString(), QStringLiteral("user"));
    EXPECT_FALSE(body.contains(QStringLiteral("stream_options")));
}

TEST(AiPluginTest, RunsTasksThroughTheProviderApiAndRecordsTheExecution) {
    test::TestPluginHost host;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    FakeChatClient* client = nullptr;
    // clang-format off
    AiPlugin plugin([&client](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); client = created.get(); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(plugin.defaultConnection().has_value());
    EXPECT_EQ(plugin.runState(task.id), TaskRunState::Idle);
    EXPECT_FALSE(plugin.hasLastExecution(task.id));

    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return client != nullptr && client->sendCalls == 1; }));
    // clang-format on
    EXPECT_EQ(plugin.runState(task.id), TaskRunState::Running);
    EXPECT_EQ(client->sentMessages.last().toObject().value(QStringLiteral("content")).toString(), task.prompt);
    EXPECT_EQ(client->sentConnection.modelId, QStringLiteral("gpt-4o"));

    client->deliver(QStringLiteral("answer"), {11, 22}, QStringLiteral("stop"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on
    EXPECT_TRUE(plugin.hasLastExecution(task.id));
    EXPECT_EQ(plugin.lastExecutionStatus(task.id), ExecutionStatus::Succeeded);
    EXPECT_EQ(plugin.tasks().first().column, TaskColumn::Done);
    plugin.shutdown();
}

TEST(AiPluginTest, ReturnsFailedTasksToTodoAndKeepsTheProviderError) {
    test::TestPluginHost host;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    FakeChatClient* client = nullptr;
    // clang-format off
    AiPlugin plugin([&client](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); client = created.get(); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return client != nullptr && client->sendCalls == 1; }));
    // clang-format on

    client->fail({"ai_request_failed", "The provider rejected the request", QStringLiteral("HTTP 401")});
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on
    EXPECT_EQ(plugin.lastExecutionStatus(task.id), ExecutionStatus::Failed);
    EXPECT_EQ(plugin.tasks().first().column, TaskColumn::Todo);
    EXPECT_EQ(plugin.lastError(task.id), QStringLiteral("The provider rejected the request"));
    plugin.shutdown();
}

TEST(AiPluginTest, StopsARunningTaskAndCancelsItsRequest) {
    test::TestPluginHost host;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    FakeChatClient* client = nullptr;
    // clang-format off
    AiPlugin plugin([&client](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); client = created.get(); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return client != nullptr && client->sendCalls == 1; }));
    // clang-format on

    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.stopTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on
    EXPECT_EQ(client->cancelCalls, 1);
    EXPECT_EQ(plugin.lastExecutionStatus(task.id), ExecutionStatus::Cancelled);

    // A stopped task always returns to the first column even when the provider reported no failure.
    EXPECT_EQ(plugin.tasks().first().column, TaskColumn::Todo);
    plugin.shutdown();
}

TEST(AiPluginTest, KeepsTasksQueuedUntilTheParallelLimitFreesCapacity) {
    test::TestPluginHost host;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask first = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTask second = AiTestsHelper::makeTask(QStringLiteral("task-2"), workspace.id);
    second.position = 1;
    AiTestsHelper::installAiRows(host, {workspace}, {first, second}, {});

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.saveConnections({AiTestsHelper::testConnection()}, ModelConnections::connectionKey(AiTestsHelper::testConnection()))).hasValue());
    EXPECT_EQ(plugin.parallelExecutions(), 1);

    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(first.id)).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(second.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.size() == 1; }));
    // clang-format on
    EXPECT_EQ(plugin.runState(first.id), TaskRunState::Running);
    EXPECT_EQ(plugin.runState(second.id), TaskRunState::Waiting);

    clients.first()->deliver(QStringLiteral("done"), {1, 2}, QStringLiteral("stop"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.size() == 2 && plugin.runState(second.id) == TaskRunState::Running; }));
    // clang-format on
    plugin.shutdown();
}

TEST(AiPluginTest, DispatchesADueScheduleAndKeepsSayingWhenItRan) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};

    AiTask scheduled = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    TaskSchedule schedule;
    schedule.kind = ScheduleKind::Once;
    schedule.enabled = true;
    schedule.onceAtUtc = now.addSecs(-60);
    schedule.nextRunAtUtc = schedule.onceAtUtc;
    schedule.timeZoneId = QTimeZone::systemTimeZoneId();
    scheduled.schedule = schedule;
    AiTestsHelper::installAiRows(host, {workspace}, {scheduled}, {});

    FakeChatClient* client = nullptr;
    // clang-format off
    AiPlugin plugin([&client](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); client = created.get(); return created; });
    // clang-format on
    const auto started = plugin.initialize(host);
    ASSERT_TRUE(started.hasValue()) << qPrintable(started.error().code) << " " << qPrintable(started.error().detail);

    // A schedule that is already due dispatches its task without anyone pressing play.
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return client != nullptr; }));
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(scheduled.id) == TaskRunState::Running; }));
    // clang-format on
    client->deliver(QStringLiteral("done"), {1, 2}, QStringLiteral("stop"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(scheduled.id) == TaskRunState::Idle; }));
    // clang-format on

    // The schedule stops repeating but keeps the date it was given and the moment it ran.
    // clang-format off
    const auto stored = std::find_if(plugin.tasks().cbegin(), plugin.tasks().cend(), [&scheduled](const AiTask& candidate) { return candidate.id == scheduled.id; });
    // clang-format on
    ASSERT_NE(stored, plugin.tasks().cend());
    ASSERT_TRUE(stored->schedule.has_value());
    EXPECT_FALSE(stored->schedule->enabled);
    EXPECT_EQ(stored->schedule->onceAtUtc, schedule.onceAtUtc);
    EXPECT_TRUE(stored->schedule->lastTriggeredAtUtc.isValid());

    std::unique_ptr<QWidget> view(plugin.createNavigationView(QStringLiteral("tasks"), nullptr));
    ASSERT_NE(view, nullptr);
    view->resize(1100, 700);
    view->show();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { for (auto* label : view->findChildren<QLabel*>()) { if (label->text().startsWith(QStringLiteral("Ran on"))) { return true; } } return false; }));
    // clang-format on
    view.reset();
    plugin.shutdown();
}

TEST(AiPluginTest, RunsEachTaskOnTheProviderAndModelItDeclares) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};

    ModelConnection mini = AiTestsHelper::testConnection();
    mini.modelId = QStringLiteral("gpt-4o-mini");
    mini.parameters = ProviderCatalog::defaultParameters(*ProviderCatalog::findProvider(mini.providerId), mini.modelId);
    ModelConnection anthropic = ModelConnections::declaredConnection(*ProviderCatalog::findProvider(QStringLiteral("anthropic")), QStringLiteral("claude-opus-5"));
    anthropic.apiKey = QStringLiteral("sk-ant");
    anthropic.displayName = QStringLiteral("Opus reviewer");

    AiAgent miniAgent = AiTestsHelper::testAgent();
    miniAgent.connectionKey = ModelConnections::connectionKey(mini);
    AiAgent anthropicAgent = AiTestsHelper::testAgent();
    anthropicAgent.id = QStringLiteral("opus-reviewer");
    anthropicAgent.connectionKey = ModelConnections::connectionKey(anthropic);

    AiTask onAnotherModel = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    onAnotherModel.agentId = miniAgent.id;
    AiTask onAnotherProvider = AiTestsHelper::makeTask(QStringLiteral("task-2"), workspace.id);
    onAnotherProvider.position = 1;
    onAnotherProvider.agentId = anthropicAgent.id;
    AiTestsHelper::installAiRows(host, {workspace}, {onAnotherModel, onAnotherProvider}, {}, {mini, anthropic}, {miniAgent, anthropicAgent});

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    // A task runs on the connection the agent it was handed to names, with the credential that connection carries.
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(onAnotherModel.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.size() == 1; }));
    // clang-format on
    EXPECT_EQ(clients.first()->sentConnection.providerId, QStringLiteral("openai"));
    EXPECT_EQ(clients.first()->sentConnection.modelId, QStringLiteral("gpt-4o-mini"));
    EXPECT_EQ(clients.first()->sentConnection.apiKey, AiTestsHelper::testConnection().apiKey);
    clients.first()->deliver(QStringLiteral("done"), {1, 2}, QStringLiteral("stop"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(onAnotherModel.id) == TaskRunState::Idle; }));
    // clang-format on

    // A provider descriptor opens with the credential reference it officially documents.
    EXPECT_EQ(ModelConnections::declaredConnection(*ProviderCatalog::findProvider(QStringLiteral("anthropic")), QStringLiteral("claude-opus-5")).apiKey, QStringLiteral("{env.ANTHROPIC_API_KEY}"));
    EXPECT_EQ(plugin.connections().size(), 2);

    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(onAnotherProvider.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.size() == 2; }));
    // clang-format on
    EXPECT_EQ(clients.at(1)->sentConnection.providerId, QStringLiteral("anthropic"));
    EXPECT_EQ(clients.at(1)->sentConnection.modelId, QStringLiteral("claude-opus-5"));
    EXPECT_EQ(clients.at(1)->sentConnection.apiKey, QStringLiteral("sk-ant"));
    clients.at(1)->deliver(QStringLiteral("done"), {1, 2}, QStringLiteral("stop"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(onAnotherProvider.id) == TaskRunState::Idle; }));
    // clang-format on
    plugin.shutdown();
}

TEST(AiPluginTest, SummarizesTheTurnsThatNoLongerFitInsteadOfLosingThem) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.saveConnections({AiTestsHelper::testConnection()}, ModelConnections::connectionKey(AiTestsHelper::testConnection()))).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.size() == 1; }));
    // clang-format on

    // The agent turns are filled until the conversation no longer fits the window the model declares.
    FakeChatClient* agent = clients.first();
    const QString filler(60000, QLatin1Char('x'));
    // Each turn asks for a different file, because an agent repeating the same call is stopped as making no progress.
    const qsizetype limit = ToolContracts::fittingTokenLimit(ProviderCatalog::findModel(*ProviderCatalog::findProvider(QStringLiteral("openai")), QStringLiteral("gpt-4o"))->contextWindow, 0).value();
    int turns = 0;

    while (clients.size() == 1 && turns < 40) {
        ++turns;
        agent->deliverToolCalls({{QStringLiteral("call-%1").arg(turns), QStringLiteral("write_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("out-%1.txt").arg(turns)}, {QStringLiteral("content"), filler}}}});
        // clang-format off
        ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return agent->sendCalls == turns + 1 || clients.size() == 2; }));
        // clang-format on
    }

    // A second client is created for the summary, and the agent conversation is not sent again until it answers.
    ASSERT_EQ(clients.size(), 2);
    const int sendsBeforeSummary = agent->sendCalls;
    FakeChatClient* summarizer = clients.at(1);
    EXPECT_TRUE(summarizer->sentTools.isEmpty());
    ASSERT_EQ(summarizer->sentMessages.size(), 1);
    EXPECT_TRUE(summarizer->sentMessages.first().toObject().value(QStringLiteral("content")).toString().startsWith(host.translate(QStringLiteral("ai.agent.summarize"))));

    summarizer->deliver(QStringLiteral("The agent wrote the report and validated it"), {10, 20}, QStringLiteral("stop"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return agent->sendCalls == sendsBeforeSummary + 1; }));
    // clang-format on

    // The compacted conversation keeps the instructions, the task and the summary of everything dropped.
    ASSERT_GE(agent->sentMessages.size(), 3);
    const QString compacted = QString::fromUtf8(QJsonDocument(agent->sentMessages).toJson(QJsonDocument::Compact));
    EXPECT_FALSE(compacted.contains(QStringLiteral("call-1")));
    EXPECT_TRUE(summarizer->sentMessages.first().toObject().value(QStringLiteral("content")).toString().contains(QStringLiteral("call-1")));
    EXPECT_LE(ToolContracts::estimateTokens(agent->sentMessages), limit);
    EXPECT_EQ(agent->sentMessages.first().toObject().value(QStringLiteral("role")).toString(), QStringLiteral("system"));
    EXPECT_TRUE(agent->sentMessages.at(2).toObject().value(QStringLiteral("content")).toString().contains(QStringLiteral("The agent wrote the report and validated it")));

    agent->deliver(QStringLiteral("done"), {1, 2}, QStringLiteral("stop"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on

    // The call that summarised the conversation is part of the run, so what it spent is counted with everything else the run spent.
    ASSERT_FALSE(host.databaseExecutions.isEmpty());
    qint64 recordedInput = 0;

    for (const auto& executed : host.databaseExecutions) {
        if (executed.value(QStringLiteral("statement")).toString().startsWith(QStringLiteral("UPDATE ai_tasks_executions"))) {
            recordedInput = executed.value(QStringLiteral("bindings")).toList().at(2).toLongLong();
        }
    }

    EXPECT_GE(recordedInput, 11) << recordedInput;

    // The summary joins the conversation as the turn that replaces the ones it summarized, so the next run never summarizes them again.
    const QVector<ConversationMessage> conversation = plugin.conversation(task.id);
    // clang-format off
    const auto summary = std::find_if(conversation.constBegin(), conversation.constEnd(), [](const ConversationMessage& message) { return message.summarizedUntil > 0; });
    // clang-format on
    ASSERT_NE(summary, conversation.constEnd());
    EXPECT_EQ(summary->role, ConversationRole::User);
    EXPECT_TRUE(summary->content.contains(QStringLiteral("The agent wrote the report and validated it")));
    EXPECT_LT(summary->summarizedUntil, summary->sequence);

    // The stored conversation keeps every turn the summary replaced, because the history of a task is its memory and only the reader removes it.
    ASSERT_FALSE(conversation.isEmpty());
    EXPECT_LE(conversation.first().sequence, summary->summarizedUntil);
    // clang-format off
    const auto replaced = std::count_if(conversation.constBegin(), conversation.constEnd(), [summary](const ConversationMessage& message) { return message.sequence <= summary->summarizedUntil; });
    // clang-format on
    EXPECT_GT(replaced, 0);
    plugin.shutdown();
}

// A write that answers after a newer one commits nothing, otherwise a later failure rolls the reader back to a setting they already changed.
TEST(AiPluginTest, KeepsTheExecutionSettingWrittenLastWhenTwoSavesAnswerOutOfOrder) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    AiTestsHelper::installAiRows(host, {}, {}, {});
    QVector<std::shared_ptr<QPromise<Result<void>>>> held;
    // clang-format off
    host.settingsFutureHandler = [&held](const QJsonObject&) {
        auto pending = std::make_shared<QPromise<Result<void>>>();
        pending->start();
        held.append(pending);
        return pending->future();
    };
    // clang-format on

    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ExecutionSettings settings = plugin.executionSettings();
    settings.maximumIterations = 12;
    [[maybe_unused]] auto older = plugin.saveExecutionSettings(settings);
    settings.maximumIterations = 20;
    [[maybe_unused]] auto newer = plugin.saveExecutionSettings(settings);
    ASSERT_EQ(held.size(), 2);

    // The newer write answers first and the older one after it.
    held.constLast()->addResult(Result<void>::success());
    held.constLast()->finish();
    held.constFirst()->addResult(Result<void>::success());
    held.constFirst()->finish();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&plugin]() { return plugin.executionSettings().maximumIterations == 20; }));
    // clang-format on

    // A write that fails now rolls back to what really reached storage rather than to the value the older answer carried.
    held.clear();
    settings.maximumIterations = 30;
    [[maybe_unused]] auto failing = plugin.saveExecutionSettings(settings);
    ASSERT_EQ(held.size(), 1);
    held.constFirst()->addResult(Result<void>::failure({"ai_settings", "no", {}}));
    held.constFirst()->finish();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&plugin]() { return plugin.executionSettings().maximumIterations != 30; }));
    // clang-format on
    EXPECT_EQ(plugin.executionSettings().maximumIterations, 20);
    plugin.shutdown();
}

TEST(AiPluginTest, RunsATaskOnItsOwnConnectionEvenWhenTheDefaultMoves) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const ProviderDescriptor* provider = ProviderCatalog::findProvider(QStringLiteral("openai"));
    ASSERT_NE(provider, nullptr);

    ModelConnection precise = AiTestsHelper::testConnection();
    precise.parameters.insert(QStringLiteral("temperature"), 0.2);
    ModelConnection creative = AiTestsHelper::testConnection();
    creative.modelId = QStringLiteral("gpt-4o-mini");
    creative.parameters = ProviderCatalog::defaultParameters(*provider, creative.modelId);
    creative.parameters.insert(QStringLiteral("temperature"), 1.4);

    AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    task.agentId = AiTestsHelper::testAgent().id;
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {}, {precise, creative});

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    // The default moves to another connection, and the task keeps running on the one its key names.
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.saveConnections({precise, creative}, ModelConnections::connectionKey(creative))).hasValue());

    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.size() == 1; }));
    // clang-format on
    EXPECT_DOUBLE_EQ(clients.first()->sentConnection.parameters.value(QStringLiteral("temperature")).toDouble(), 0.2);

    clients.first()->deliver(QStringLiteral("done"), {1, 2}, QStringLiteral("stop"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on
    plugin.shutdown();
}

TEST(AiPluginTest, KeepsTwoEditsOfOneTurnFromLandingOnTheSameFileTogether) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString path = QDir(root.path()).filePath(QStringLiteral("notes.txt"));

    // The read never answers on its own, so the test sees exactly how many edits the turn started.
    QVector<std::shared_ptr<QPromise<Result<QByteArray>>>> reads;
    QString stored = QStringLiteral("alpha\nbeta\n");
    // clang-format off
    host.readFileHandler = [&reads](const QString& requested, qint64) { if (!requested.endsWith(QStringLiteral("notes.txt"))) { return QtFuture::makeReadyValueFuture(Result<QByteArray>::failure({"absent", "The case answers only the file it is about", requested})); } auto promise = std::make_shared<QPromise<Result<QByteArray>>>(); promise->start(); reads.append(promise); return promise->future(); };
    host.writeFileHandler = [&stored](const QString&, const QByteArray& content) { stored = QString::fromUtf8(content); return QtFuture::makeReadyValueFuture(Result<void>::success()); };
    // clang-format on

    AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    task.workdir = QDir(root.path()).absolutePath();
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.saveConnections({AiTestsHelper::testConnection()}, ModelConnections::connectionKey(AiTestsHelper::testConnection()))).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.size() == 1; }));
    // clang-format on

    FakeChatClient* agent = clients.first();
    const int sendsBeforeTools = agent->sendCalls;
    agent->deliverToolCalls({{QStringLiteral("call-1"), QStringLiteral("edit_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("notes.txt")}, {QStringLiteral("old_text"), QStringLiteral("alpha")}, {QStringLiteral("new_text"), QStringLiteral("first")}}}, {QStringLiteral("call-2"), QStringLiteral("edit_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("notes.txt")}, {QStringLiteral("old_text"), QStringLiteral("beta")}, {QStringLiteral("new_text"), QStringLiteral("second")}}}});

    // Both edits reach the same file, so the second one waits instead of reading the text the first one is about to replace.
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return reads.size() == 1; }));
    ASSERT_FALSE(test::TestFutures::waitUntil([&]() { return reads.size() > 1; }, 800));
    // clang-format on

    reads.first()->addResult(Result<QByteArray>::success(stored.toUtf8()));
    reads.first()->finish();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return reads.size() == 2; }));
    // clang-format on
    EXPECT_EQ(stored, QStringLiteral("first\nbeta\n"));

    reads.at(1)->addResult(Result<QByteArray>::success(stored.toUtf8()));
    reads.at(1)->finish();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return agent->sendCalls == sendsBeforeTools + 1; }));
    // clang-format on
    EXPECT_EQ(stored, QStringLiteral("first\nsecond\n"));

    // The results answer in the order the model asked for them, whatever order they finished in.
    const QString sent = QString::fromUtf8(QJsonDocument(agent->sentMessages).toJson(QJsonDocument::Compact));
    EXPECT_LT(sent.indexOf(QStringLiteral("call-1")), sent.lastIndexOf(QStringLiteral("call-2")));

    agent->deliver(QStringLiteral("done"), {1, 2}, QStringLiteral("stop"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on
    plugin.shutdown();
}

TEST(AiPluginTest, DiscardsAToolResultThatBelongsToARunTheCardAlreadyStopped) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    task.workdir = QDir(root.path()).absolutePath();
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    // The write never answers on its own, so the test decides exactly when the stopped run reports back.
    QVector<std::shared_ptr<QPromise<Result<void>>>> writes;
    // clang-format off
    host.writeFileHandler = [&writes](const QString&, const QByteArray&) { auto promise = std::make_shared<QPromise<Result<void>>>(); promise->start(); writes.append(promise); return promise->future(); };
    QVector<FakeChatClient*> clients;
    AiPlugin plugin([&clients](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.saveConnections({AiTestsHelper::testConnection()}, ModelConnections::connectionKey(AiTestsHelper::testConnection()))).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.size() == 1; }));
    // clang-format on

    clients.first()->deliverToolCalls({{QStringLiteral("call-1"), QStringLiteral("write_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("report.txt")}, {QStringLiteral("content"), QStringLiteral("first run")}}}});
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return writes.size() == 1; }));
    // clang-format on

    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.stopTask(task.id)).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.size() == 2; }));
    // clang-format on
    FakeChatClient* second = clients.at(1);
    const int sendsBeforeStaleResult = second->sendCalls;

    writes.first()->addResult(Result<void>::success());
    writes.first()->finish();
    // clang-format off
    ASSERT_FALSE(test::TestFutures::waitUntil([&]() { return second->sendCalls != sendsBeforeStaleResult; }, 800));
    // clang-format on
    EXPECT_EQ(second->sendCalls, sendsBeforeStaleResult);
    EXPECT_EQ(plugin.runState(task.id), TaskRunState::Running);

    second->deliver(QStringLiteral("done"), {1, 2}, QStringLiteral("stop"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on
    plugin.shutdown();
}

TEST(AiPluginTest, FitsTheConversationToTheModelTheRunDeclaresAndNotToALaterSelection) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});
    filesystem::FileSystemService files;
    host.useFileSystem(files);

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.saveConnections({AiTestsHelper::testConnection()}, ModelConnections::connectionKey(AiTestsHelper::testConnection()))).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.size() == 1; }));
    // clang-format on

    // The default moves to a model with a wider window while the run is already speaking to the narrow one.
    const ProviderDescriptor* provider = ProviderCatalog::findProvider(QStringLiteral("openai"));
    const QString widerModel = QStringLiteral("o3-mini");
    ModelConnection wider = AiTestsHelper::testConnection();
    wider.modelId = widerModel;
    wider.parameters = ProviderCatalog::defaultParameters(*provider, widerModel);
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.saveConnections({AiTestsHelper::testConnection(), wider}, ModelConnections::connectionKey(wider))).hasValue());

    FakeChatClient* agent = clients.first();
    const QString filler(60000, QLatin1Char('x'));
    const qint64 runLimit = ToolContracts::fittingTokenLimit(ProviderCatalog::findModel(*provider, QStringLiteral("gpt-4o"))->contextWindow, 0).value();
    const qint64 widerLimit = ToolContracts::fittingTokenLimit(ProviderCatalog::findModel(*provider, widerModel)->contextWindow, 0).value();
    int turns = 0;

    while (clients.size() == 1 && turns < 40) {
        ++turns;
        agent->deliverToolCalls({{QStringLiteral("call-%1").arg(turns), QStringLiteral("write_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("out-%1.txt").arg(turns)}, {QStringLiteral("content"), filler}}}});
        // clang-format off
        ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return agent->sendCalls == turns + 1 || clients.size() == 2; }));
        // clang-format on
    }

    ASSERT_EQ(clients.size(), 2);
    EXPECT_GT(widerLimit, runLimit);
    EXPECT_LT(ToolContracts::estimateTokens(clients.at(1)->sentMessages), widerLimit);
    plugin.shutdown();
}

// A card dropped into Doing starts the task, so a task whose agent is gone says that rather than reading as a card that could not be saved.
TEST(AiTasksViewTest, SaysTheAgentIsGoneWhenACardWithNoneIsDroppedIntoDoing) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask orphan = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    orphan.agentId = QStringLiteral("an-agent-nobody-configured");
    AiTestsHelper::installAiRows(host, {workspace}, {orphan}, {});

    // clang-format off
    AiPlugin plugin([](AiRequestGate&, const ModelConnection&) { return std::make_unique<FakeChatClient>(); });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    std::unique_ptr<QWidget> view(plugin.createNavigationView(QStringLiteral("tasks"), nullptr));
    ASSERT_NE(view, nullptr);
    view->show();

    const qsizetype told = host.notifications.size();
    const auto moved = test::TestFutures::awaitFuture(plugin.moveTask(orphan.id, TaskColumn::Doing));
    ASSERT_FALSE(moved.hasValue());
    EXPECT_EQ(moved.error().code, QStringLiteral("ai_agent_unknown"));

    // The card keeps which agent is missing rather than reading as a card that could not be written.
    EXPECT_EQ(plugin.lastError(orphan.id), host.translate(QStringLiteral("ai.error.agent-removed")).arg(orphan.agentId));
    EXPECT_NE(plugin.lastError(orphan.id), host.translate(QStringLiteral("ai.error.task-save")));
    EXPECT_EQ(host.notifications.size(), told);
}

TEST(AiTasksViewTest, OffersTheWorkingDirectoryToTheEditorAndTheWebServerOnlyWhenTheTaskDeclaresOne) {
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    host.availableCapabilities = {QString::fromLatin1(plugins::openFolderCapability), QString::fromLatin1(plugins::serveFolderCapability)};
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask located = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    located.workdir = workdir.path();
    const AiTask homeless = AiTestsHelper::makeTask(QStringLiteral("task-2"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {located, homeless}, {});

    // clang-format off
    AiPlugin plugin([](AiRequestGate&, const ModelConnection&) { return std::make_unique<FakeChatClient>(); });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    std::unique_ptr<QWidget> view(plugin.createNavigationView(QStringLiteral("tasks"), nullptr));
    ASSERT_NE(view, nullptr);
    view->resize(1100, 700);
    view->show();

    // A task without a working directory has no folder to offer, so neither action exists on its card.
    const auto openButtons = view->findChildren<QToolButton*>(QStringLiteral("aiTaskOpenWorkdir"));
    const auto serveButtons = view->findChildren<QToolButton*>(QStringLiteral("aiTaskServeWorkdir"));
    ASSERT_EQ(openButtons.size(), 2);
    ASSERT_EQ(serveButtons.size(), 2);
    // clang-format off
    const auto shown = [](const QList<QToolButton*>& buttons) { return std::count_if(buttons.cbegin(), buttons.cend(), [](const QToolButton* button) { return !button->isHidden(); }); };
    // clang-format on
    EXPECT_EQ(shown(openButtons), 1);
    EXPECT_EQ(shown(serveButtons), 1);

    QToolButton* openFolder = nullptr;
    QToolButton* serveFolder = nullptr;

    for (auto* button : openButtons) {
        if (!button->isHidden()) {
            openFolder = button;
        }
    }

    for (auto* button : serveButtons) {
        if (!button->isHidden()) {
            serveFolder = button;
        }
    }

    ASSERT_NE(openFolder, nullptr);
    ASSERT_NE(serveFolder, nullptr);

    // The board asks for a capability rather than for a plugin, so each action is one asynchronous invocation carrying the folder.
    openFolder->click();
    serveFolder->click();
    ASSERT_EQ(host.capabilityInvocations.size(), 2);
    EXPECT_EQ(host.capabilityInvocations.at(0).name, QString::fromLatin1(plugins::openFolderCapability));
    EXPECT_EQ(host.capabilityInvocations.at(0).payload.value(QStringLiteral("path")).toString(), workdir.path());
    EXPECT_EQ(host.capabilityInvocations.at(1).name, QString::fromLatin1(plugins::serveFolderCapability));
    EXPECT_EQ(host.capabilityInvocations.at(1).payload.value(QStringLiteral("path")).toString(), workdir.path());

    plugin.shutdown();
}

TEST(AiTasksViewTest, StopsTheScheduleFromTheCardAndTakesTheActionAwayWithIt) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask scheduled = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    TaskSchedule schedule;
    schedule.kind = ScheduleKind::Interval;
    schedule.intervalSeconds = 3600;
    schedule.timeZoneId = QByteArrayLiteral("UTC");
    schedule.nextRunAtUtc = now.addSecs(3600);
    scheduled.schedule = schedule;
    const AiTask plain = AiTestsHelper::makeTask(QStringLiteral("task-2"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {scheduled, plain}, {});

    // clang-format off
    AiPlugin plugin([](AiRequestGate&, const ModelConnection&) { return std::make_unique<FakeChatClient>(); });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    std::unique_ptr<QWidget> view(plugin.createNavigationView(QStringLiteral("tasks"), nullptr));
    ASSERT_NE(view, nullptr);
    view->resize(1100, 700);
    view->show();

    // Only the task that carries a schedule offers the action that stops it.
    const auto cards = view->findChildren<QToolButton*>(QStringLiteral("aiTaskSchedule"));
    ASSERT_EQ(cards.size(), 2);
    // clang-format off
    const auto visible = std::count_if(cards.cbegin(), cards.cend(), [](const QToolButton* button) { return button->isVisible() || !button->isHidden(); });
    // clang-format on
    EXPECT_EQ(visible, 1);

    QToolButton* stopSchedule = nullptr;

    for (auto* button : cards) {
        if (!button->isHidden()) {
            stopSchedule = button;
        }
    }

    ASSERT_NE(stopSchedule, nullptr);
    ASSERT_TRUE(stopSchedule->isEnabled());

    // A rejected confirmation keeps the schedule exactly as it was.
    host.confirmation = false;
    stopSchedule->click();
    // clang-format off
    ASSERT_FALSE(test::TestFutures::waitUntil([&]() { return !plugin.tasks().first().schedule.has_value(); }, 600));
    // clang-format on

    host.confirmation = true;
    stopSchedule->click();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !plugin.tasks().first().schedule.has_value(); }));
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { const auto rebuilt = view->findChildren<QToolButton*>(QStringLiteral("aiTaskSchedule")); return std::none_of(rebuilt.cbegin(), rebuilt.cend(), [](const QToolButton* button) { return !button->isHidden(); }); }));
    // clang-format on
    plugin.shutdown();
}

TEST(AiTasksViewTest, KeepsTheCardAliveWhileItsTaskMovesUnderAnOpenDialog) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});
    AiTestsHelper::installExecutionRows(host, {}, {});

    FakeChatClient* client = nullptr;
    // clang-format off
    AiPlugin plugin([&client](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); client = created.get(); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    std::unique_ptr<QWidget> view(plugin.createNavigationView(QStringLiteral("tasks"), nullptr));
    ASSERT_NE(view, nullptr);
    view->resize(1100, 700);
    view->show();

    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return client != nullptr && client->sendCalls == 1; }));
    // clang-format on

    auto* badge = view->findChild<QLabel*>(QStringLiteral("aiTaskBadge"));
    ASSERT_NE(badge, nullptr);
    const QPointer<QWidget> card = badge->parentWidget();
    ASSERT_FALSE(card.isNull());
    auto* information = view->findChild<QToolButton*>(QStringLiteral("aiTaskInfo"));
    ASSERT_NE(information, nullptr);

    // The dialog is opened without taking the event loop, so the click that opened it returns before anything else happens.
    QTest::mouseClick(information, Qt::LeftButton);
    auto* dialog = view->findChild<AiTaskInfoDialog*>();
    ASSERT_NE(dialog, nullptr);
    EXPECT_TRUE(dialog->isVisible());

    // The run finishes while that dialog is still up, so the task leaves the column it was in.
    client->deliver(QStringLiteral("done"), {1, 2}, QStringLiteral("stop"));
    // clang-format off
    EXPECT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on
    EXPECT_FALSE(card.isNull());
    EXPECT_EQ(plugin.lastExecutionStatus(task.id), ExecutionStatus::Succeeded);

    // Closing it must leave a card that is still there and release the dialog it was showing.
    const QPointer<AiTaskInfoDialog> shown(dialog);
    dialog->accept();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_FALSE(card.isNull());
    EXPECT_TRUE(shown.isNull());

    view.reset();
    plugin.shutdown();
}

// A task that is gone leaves nothing behind, in the database by the cascade its schema declares and in memory by what the plugin forgets.
TEST(AiPluginTest, LeavesNothingBehindWhenATaskOrItsWorkspaceIsRemoved) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    persistence::StateStore store(directory.filePath(QStringLiteral("workpane.sqlite3")));
    ASSERT_TRUE(store.initialize().hasValue());

    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    host.useDatabase(store, QStringLiteral("ai"));
    host.settingsDocument = AiTestsHelper::settingsDocument({AiTestsHelper::testConnection()}, ModelConnections::connectionKey(AiTestsHelper::testConnection()));

    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.createWorkspace(QStringLiteral("Product"))).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.createWorkspace(QStringLiteral("Doomed"))).hasValue());
    const QString keptId = plugin.workspaces().first().id;
    const QString doomedId = plugin.workspaces().last().id;

    AiTask removed = AiTestsHelper::makeTask(QStringLiteral("task-1"), keptId);
    AiTask inDoomedWorkspace = AiTestsHelper::makeTask(QStringLiteral("task-2"), doomedId);
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.saveTask(removed)).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.saveTask(inDoomedWorkspace)).hasValue());

    AiTaskRepository probe(host);
    const QDateTime now = QDateTime::currentDateTimeUtc();

    for (const auto& taskId : {removed.id, inDoomedWorkspace.id}) {
        const TaskExecution execution{QStringLiteral("run-") + taskId, taskId, ExecutionStatus::Failed, now, now.addSecs(1), 1, 2, QStringLiteral("error"), QStringLiteral("it broke"), {}};
        ASSERT_TRUE(test::TestFutures::awaitFuture(probe.startExecution(execution)).hasValue());
        ASSERT_TRUE(test::TestFutures::awaitFuture(probe.finishExecution(execution)).hasValue());
        ASSERT_TRUE(test::TestFutures::awaitFuture(probe.appendExecutionLog({QStringLiteral("log-") + taskId, execution.id, 1, now, ExecutionLogLevel::Error, ExecutionLogKind::Failed, QStringLiteral("detail")})).hasValue());
    }

    ASSERT_EQ(probe.lastOutcomes().value().size(), 2);
    plugin.shutdown();

    // A restart carries what happened into memory, which is the state a removal has to take with it.
    AiPlugin restarted;
    ASSERT_TRUE(restarted.initialize(host).hasValue());
    EXPECT_EQ(restarted.lastError(removed.id), QStringLiteral("it broke"));
    EXPECT_EQ(restarted.lastError(inDoomedWorkspace.id), QStringLiteral("it broke"));

    // A task that is gone leaves no conversation behind either, in memory or in storage.
    const ConversationMessage said{QStringLiteral("message-1"), removed.id, 1, ConversationRole::User, QStringLiteral("hello"), {}, {}, {}, {}, 0, now};
    ASSERT_TRUE(test::TestFutures::awaitFuture(probe.appendConversation({said})).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(restarted.loadConversation(removed.id)).hasValue());
    ASSERT_EQ(restarted.conversation(removed.id).size(), 1);

    // Removing the task takes its runs and their entries with it, and leaves nothing the plugin still answers for.
    ASSERT_TRUE(test::TestFutures::awaitFuture(restarted.removeTask(removed.id)).hasValue());
    EXPECT_TRUE(restarted.lastError(removed.id).isEmpty());
    EXPECT_TRUE(restarted.conversation(removed.id).isEmpty());
    EXPECT_TRUE(test::TestFutures::awaitFuture(probe.conversation(removed.id, 0, 100)).value().isEmpty());
    EXPECT_FALSE(restarted.hasLastExecution(removed.id));
    // clang-format off
    EXPECT_FALSE(std::ranges::any_of(restarted.tasks(), [&removed](const AiTask& candidate) { return candidate.id == removed.id; }));
    // clang-format on
    ASSERT_TRUE(probe.lastOutcomes().hasValue());
    EXPECT_FALSE(probe.lastOutcomes().value().contains(removed.id));
    EXPECT_TRUE(test::TestFutures::awaitFuture(probe.executionLogs(QStringLiteral("run-") + removed.id)).value().isEmpty());

    // Removing the workspace takes the tasks it holds and everything those tasks recorded.
    ASSERT_TRUE(test::TestFutures::awaitFuture(restarted.removeWorkspace(doomedId)).hasValue());
    EXPECT_TRUE(restarted.lastError(inDoomedWorkspace.id).isEmpty());
    EXPECT_FALSE(restarted.hasLastExecution(inDoomedWorkspace.id));
    EXPECT_TRUE(restarted.tasks().isEmpty());
    ASSERT_TRUE(probe.lastOutcomes().hasValue());
    EXPECT_TRUE(probe.lastOutcomes().value().isEmpty());
    EXPECT_TRUE(probe.tasks().value().isEmpty());
    EXPECT_TRUE(test::TestFutures::awaitFuture(probe.executionLogs(QStringLiteral("run-") + inDoomedWorkspace.id)).value().isEmpty());

    restarted.shutdown();
}

// The outcome is read from a real database, because what names the newest run of each task is real SQL rather than a double pretending to be one.
TEST(AiTaskRepositoryTest, ReadsBackWhatHappenedToEachTaskAcrossARestart) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    persistence::StateStore store(directory.filePath(QStringLiteral("workpane.sqlite3")));
    ASSERT_TRUE(store.initialize().hasValue());

    test::TestPluginHost host;
    host.useDatabase(store, QStringLiteral("ai"));
    AiTaskRepository repository(host);
    ASSERT_TRUE(repository.initialize().hasValue());

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    ASSERT_TRUE(test::TestFutures::awaitFuture(repository.createWorkspace(workspace)).hasValue());
    AiTask failing = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTask succeeding = AiTestsHelper::makeTask(QStringLiteral("task-2"), workspace.id);
    ASSERT_TRUE(test::TestFutures::awaitFuture(repository.saveTask(failing)).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(repository.saveTask(succeeding)).hasValue());

    // Nothing ran yet, so nothing is claimed about what happened.
    ASSERT_TRUE(repository.lastOutcomes().hasValue());
    EXPECT_TRUE(repository.lastOutcomes().value().isEmpty());

    // Two runs of one task, so the newest is the one a restart has to find.
    const TaskExecution older{QStringLiteral("run-1"), failing.id, ExecutionStatus::Succeeded, now.addSecs(-60), now.addSecs(-59), 1, 2, QStringLiteral("stop"), {}, QStringLiteral("first")};
    const TaskExecution newest{QStringLiteral("run-2"), failing.id, ExecutionStatus::Failed, now, now.addSecs(1), 1, 2, QStringLiteral("error"), QStringLiteral("the model refused"), {}};
    const TaskExecution other{QStringLiteral("run-3"), succeeding.id, ExecutionStatus::Succeeded, now, now.addSecs(1), 3, 4, QStringLiteral("stop"), {}, QStringLiteral("done")};

    for (const auto& execution : {older, newest, other}) {
        ASSERT_TRUE(test::TestFutures::awaitFuture(repository.startExecution(execution)).hasValue());
        ASSERT_TRUE(test::TestFutures::awaitFuture(repository.finishExecution(execution)).hasValue());
    }

    const auto outcomes = repository.lastOutcomes();
    ASSERT_TRUE(outcomes.hasValue());
    EXPECT_EQ(outcomes.value().size(), 2);
    EXPECT_EQ(outcomes.value().value(failing.id).status, ExecutionStatus::Failed);
    EXPECT_EQ(outcomes.value().value(failing.id).errorMessage, QStringLiteral("the model refused"));
    EXPECT_EQ(outcomes.value().value(succeeding.id).status, ExecutionStatus::Succeeded);
    EXPECT_TRUE(outcomes.value().value(succeeding.id).errorMessage.isEmpty());
}

TEST(AiPluginTest, TakesBackATurnItCouldNotWriteAndStopsTheRunThatProducedIt) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    FakeChatClient* client = nullptr;
    // clang-format off
    AiPlugin plugin([&client](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); client = created.get(); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    // The conversation is the source of truth, so a turn that storage refused is taken back instead of living only in memory.
    // clang-format off
    host.transactionFutureHandler = [](const QVector<persistence::DatabaseStatement>& statements) {
        const bool conversation = !statements.isEmpty() && statements.first().statement.contains(QStringLiteral("ai_tasks_messages"));
        return QtFuture::makeReadyValueFuture(conversation ? Result<void>::failure({"ai_storage_failed", "the disk is full", {}}) : Result<void>::success());
    };
    // clang-format on
    const auto refused = test::TestFutures::awaitFuture(plugin.startTask(task.id));
    EXPECT_EQ(refused.error().code, QStringLiteral("ai_storage_failed"));

    // Nothing is left behind: no turn in memory, no run started and the reason on the card.
    EXPECT_TRUE(plugin.conversation(task.id).isEmpty());
    EXPECT_EQ(plugin.runState(task.id), TaskRunState::Idle);
    EXPECT_EQ(client, nullptr);
    EXPECT_EQ(plugin.lastError(task.id), host.translate(QStringLiteral("ai.error.conversation-save")));
    plugin.shutdown();
}

TEST(AiPluginTest, KeepsTheConversationAndClaimsAMessageTypedWhileTheTurnIsRunning) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    // Play sends the prompt of the task again, so it is the first turn of the conversation.
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.size() == 1; }));
    // clang-format on
    ASSERT_EQ(plugin.conversation(task.id).size(), 1);
    EXPECT_EQ(plugin.conversation(task.id).first().role, ConversationRole::User);
    EXPECT_EQ(plugin.conversation(task.id).first().content, task.prompt);

    // A message typed while the turn is running joins the conversation at once instead of waiting for it to end.
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.sendMessage(task.id, QStringLiteral("also check the tests"))).hasValue());
    ASSERT_EQ(plugin.conversation(task.id).size(), 2);
    EXPECT_EQ(plugin.conversation(task.id).last().content, QStringLiteral("also check the tests"));
    EXPECT_EQ(clients.size(), 1);

    // The turn that was running answers with a tool call, and the next iteration carries what was typed meanwhile.
    clients.first()->deliverToolCalls({{QStringLiteral("call-1"), QStringLiteral("list_directory"), QJsonObject{{QStringLiteral("path"), QString{}}}}});
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.first()->sendCalls == 2; }));
    // clang-format on
    const QJsonArray secondTurn = clients.first()->sentMessages;
    bool carriesTypedMessage = false;

    for (const auto& value : secondTurn) {
        carriesTypedMessage = carriesTypedMessage || QJsonDocument(value.toObject()).toJson().contains(QByteArrayLiteral("also check the tests"));
    }

    EXPECT_TRUE(carriesTypedMessage);

    // The answer of the turn is kept, so the next run starts from everything that was said.
    clients.first()->deliver(QStringLiteral("all good"), {1, 2}, QStringLiteral("stop"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on
    ASSERT_GE(plugin.conversation(task.id).size(), 4);
    EXPECT_EQ(plugin.conversation(task.id).last().role, ConversationRole::Assistant);
    EXPECT_EQ(plugin.conversation(task.id).last().content, QStringLiteral("all good"));

    // A message that arrives while the turn is giving its final answer was never carried to the model, so it opens the next turn.
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.size() == 2; }));
    // clang-format on
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.sendMessage(task.id, QStringLiteral("one more thing"))).hasValue());
    EXPECT_EQ(clients.size(), 2);
    clients.at(1)->deliver(QStringLiteral("finished"), {1, 2}, QStringLiteral("stop"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.size() == 3; }));
    // clang-format on

    clients.at(2)->deliver(QStringLiteral("and that too"), {1, 2}, QStringLiteral("stop"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on

    // Resetting returns the task to the state it was created in.
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.resetConversation(task.id)).hasValue());
    EXPECT_TRUE(plugin.conversation(task.id).isEmpty());
    plugin.shutdown();
}

AiTaskInfoDialog* openTaskSurface(QWidget* view, const QString& objectName) {
    auto* button = view->findChild<QToolButton*>(objectName);

    if (button == nullptr) {
        return nullptr;
    }

    QTest::mouseClick(button, Qt::LeftButton);
    return view->findChild<AiTaskInfoDialog*>();
}

TEST(AiTasksViewTest, EditsATaskNobodyRanYetAndOpensTheSurfaceOfEveryOtherOne) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask fresh = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTask reviewed = AiTestsHelper::makeTask(QStringLiteral("task-2"), workspace.id);
    reviewed.column = TaskColumn::Review;
    AiTestsHelper::installAiRows(host, {workspace}, {fresh, reviewed}, {});
    AiTestsHelper::installExecutionRows(host, {}, {});

    // clang-format off
    AiPlugin plugin([](AiRequestGate&, const ModelConnection&) { return std::make_unique<FakeChatClient>(); });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    std::unique_ptr<QWidget> view(plugin.createNavigationView(QStringLiteral("tasks"), nullptr));
    ASSERT_NE(view, nullptr);
    view->resize(1100, 700);
    view->show();

    const auto badges = view->findChildren<QLabel*>(QStringLiteral("aiTaskBadge"));
    ASSERT_EQ(badges.size(), 2);
    QWidget* untouched = badges.at(0)->parentWidget();
    QWidget* alreadyRead = badges.at(1)->parentWidget();
    ASSERT_NE(untouched, nullptr);
    ASSERT_NE(alreadyRead, nullptr);

    // A task waiting in To Do that never ran is opened to be written.
    bool editorOpened = false;
    // clang-format off
    QTimer::singleShot(0, view.get(), [&view, &editorOpened]() { if (auto* editor = view->findChild<AiTaskDialog*>(); editor != nullptr) { editorOpened = true; editor->reject(); } });
    // clang-format on
    QTest::mouseDClick(untouched, Qt::LeftButton);
    EXPECT_TRUE(editorOpened);
    EXPECT_EQ(view->findChild<AiTaskInfoDialog*>(), nullptr);

    // Every other one is opened to be read, which is the surface that answers for the task.
    QTest::mouseDClick(alreadyRead, Qt::LeftButton);
    auto* surface = view->findChild<AiTaskInfoDialog*>();
    ASSERT_NE(surface, nullptr);
    EXPECT_TRUE(surface->isVisible());
    EXPECT_EQ(view->findChild<AiTaskDialog*>(), nullptr);

    // The surface is a modal window of its own, so it blocks what is behind it and carries the buttons every window of the platform carries.
    EXPECT_EQ(surface->windowModality(), Qt::ApplicationModal);
    EXPECT_EQ(surface->windowTitle(), reviewed.title);
    surface->reject();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    // The card opens the conversation directly, which is the same surface on its first tab.
    auto* chat = alreadyRead->findChild<QToolButton*>(QStringLiteral("aiTaskChat"));
    ASSERT_NE(chat, nullptr);
    EXPECT_TRUE(chat->isVisible());
    QTest::mouseClick(chat, Qt::LeftButton);
    auto* opened = view->findChild<AiTaskInfoDialog*>();
    ASSERT_NE(opened, nullptr);
    auto* conversation = opened->findChild<AiConversationView*>();
    ASSERT_NE(conversation, nullptr);
    EXPECT_EQ(conversation->taskId(), reviewed.id);
    opened->reject();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    view.reset();
    plugin.shutdown();
}

TEST(AiTasksViewTest, RendersTheBoardWithStatusBadgesAndTheInformationAction) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    FakeChatClient* client = nullptr;
    // clang-format off
    AiPlugin plugin([&client](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); client = created.get(); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    std::unique_ptr<QWidget> view(plugin.createNavigationView(QStringLiteral("tasks"), nullptr));
    ASSERT_NE(view, nullptr);
    view->resize(1100, 700);
    view->show();

    // The board owns the whole content area, so no terminal surface is created for a task.
    EXPECT_EQ(view->findChild<QWidget*>(QStringLiteral("aiTaskTerminals")), nullptr);
    EXPECT_EQ(view->findChild<QWidget*>(QStringLiteral("aiClearTerminal")), nullptr);
    EXPECT_EQ(view->findChildren<QWidget*>(QStringLiteral("aiKanbanColumn")).size(), 5);
    ASSERT_NE(view->findChild<QToolButton*>(QStringLiteral("aiTaskInfo")), nullptr);

    auto* badge = view->findChild<QLabel*>(QStringLiteral("aiTaskBadge"));
    ASSERT_NE(badge, nullptr);
    EXPECT_EQ(badge->text(), QStringLiteral("Idle"));
    EXPECT_EQ(badge->property("badge").toString(), QStringLiteral("idle"));

    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return client != nullptr && client->sendCalls == 1; }));
    // clang-format on
    auto* runningBadge = view->findChild<QLabel*>(QStringLiteral("aiTaskBadge"));
    ASSERT_NE(runningBadge, nullptr);
    EXPECT_EQ(runningBadge->property("badge").toString(), QStringLiteral("running"));

    // The badge is a rounded pill on its own line, and the live phase reads on the line below it.
    auto* card = runningBadge->parentWidget();
    ASSERT_NE(card, nullptr);
    QLabel* phase = nullptr;

    for (auto* candidate : card->findChildren<QLabel*>(QStringLiteral("mutedLabel"))) {
        if (candidate->text() == host.translate(QStringLiteral("ai.phase.sending"))) {
            phase = candidate;
        }
    }

    ASSERT_NE(phase, nullptr);
    card->layout()->activate();
    EXPECT_GE(phase->y(), runningBadge->y() + runningBadge->height());
    EXPECT_EQ(phase->x(), runningBadge->x());
    EXPECT_LT(runningBadge->width(), card->width());
    EXPECT_GT(host.theme().metric(ui::ThemeMetric::BadgeRadius), host.theme().metric(ui::ThemeMetric::ControlRadius));

    client->deliver(QStringLiteral("answer"), {3, 4}, QStringLiteral("stop"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { auto* current = view->findChild<QLabel*>(QStringLiteral("aiTaskBadge")); return current != nullptr && current->property("badge").toString() == QStringLiteral("succeeded"); }));
    // clang-format on

    view.reset();
    plugin.shutdown();
}

TEST(AiConnectionSettingsViewTest, StartsFromTheEmptyStateAndOpensADialogWithTheDeclaredCredential) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    AiTestsHelper::installEmptyProviderRows(host);
    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    // Nothing was ever configured, so the section presents the empty state instead of an empty grid.
    ASSERT_TRUE(plugin.connections().isEmpty());
    EXPECT_FALSE(plugin.defaultConnection().has_value());

    std::unique_ptr<QWidget> section(plugin.createSettingsSection(QStringLiteral("connections"), QStringLiteral("general"), nullptr));
    ASSERT_NE(section, nullptr);
    section->resize(760, 520);
    section->show();

    auto* grid = section->findChild<QTableWidget*>(QStringLiteral("aiConnectionGrid"));
    auto* empty = section->findChild<QLabel*>(QStringLiteral("aiConnectionEmpty"));
    auto* defaultConnection = section->findChild<QComboBox*>(QStringLiteral("aiDefaultConnection"));
    ASSERT_NE(grid, nullptr);
    ASSERT_NE(empty, nullptr);
    ASSERT_NE(defaultConnection, nullptr);
    EXPECT_FALSE(grid->isVisible());
    EXPECT_TRUE(empty->isVisible());
    EXPECT_FALSE(defaultConnection->isEnabled());

    AiConnectionDialog dialog(host, {}, {}, nullptr);
    dialog.resize(700, 620);
    dialog.show();
    auto* provider = dialog.findChild<QComboBox*>(QStringLiteral("aiConnectionProvider"));
    auto* model = dialog.findChild<QComboBox*>(QStringLiteral("aiConnectionModel"));
    auto* apiKey = dialog.findChild<ui::SecretField*>(QStringLiteral("aiConnectionApiKey"));
    ASSERT_NE(provider, nullptr);
    ASSERT_NE(model, nullptr);
    ASSERT_NE(apiKey, nullptr);
    EXPECT_EQ(provider->count(), ModelConnections::providersAnswering(ModelEndpoint::Chat).size());
    EXPECT_FALSE(apiKey->revealed());

    const ProviderDescriptor* first = ProviderCatalog::findProvider(provider->currentData().toString());
    ASSERT_NE(first, nullptr);
    EXPECT_FALSE(first->baseUrl.isEmpty());
    EXPECT_EQ(apiKey->value(), QStringLiteral("{env.%1}").arg(first->apiKeyVariable));
    EXPECT_FALSE(model->currentText().isEmpty());

    // The providers read alphabetically, and the list opening alphabetically never changes the model its provider declares.
    for (int index = 1; index < provider->count(); ++index) {
        EXPECT_LE(QString::compare(provider->itemText(index - 1), provider->itemText(index), Qt::CaseInsensitive), 0) << provider->itemText(index - 1).toStdString();
    }

    provider->setCurrentIndex(provider->findData(QStringLiteral("anthropic")));
    EXPECT_EQ(model->currentText(), ProviderCatalog::findProvider(QStringLiteral("anthropic"))->preferredModels.first());

    for (int index = 1; index < model->count(); ++index) {
        EXPECT_LE(QString::compare(model->itemText(index - 1), model->itemText(index), Qt::CaseInsensitive), 0) << model->itemText(index - 1).toStdString();
    }

    EXPECT_TRUE(ModelConnections::validateConnection(dialog.connection()).hasValue()) << ModelConnections::validateConnection(dialog.connection()).error().code.toStdString();

    provider->setCurrentIndex(provider->findData(QStringLiteral("openai")));
    model->setCurrentText(QStringLiteral("gpt-4o"));
    emit model->lineEdit()->editingFinished();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_NE(dialog.findChild<QWidget*>(QStringLiteral("aiParameter.temperature")), nullptr);
    EXPECT_EQ(dialog.findChild<QWidget*>(QStringLiteral("aiParameter.reasoningEffort")), nullptr);

    // A reasoning model swaps the sampling fields for the effort field without any code change.
    model->setCurrentText(QStringLiteral("o3-mini"));
    emit model->lineEdit()->editingFinished();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_EQ(dialog.findChild<QWidget*>(QStringLiteral("aiParameter.temperature")), nullptr);
    EXPECT_NE(dialog.findChild<QWidget*>(QStringLiteral("aiParameter.reasoningEffort")), nullptr);

    // Only a self-hosted service carries an address, so the field appears with it and disappears with the others.
    auto* address = dialog.findChild<QLineEdit*>(QStringLiteral("aiConnectionAddress"));
    ASSERT_NE(address, nullptr);
    EXPECT_FALSE(address->isVisible());
    provider->setCurrentIndex(provider->findData(QStringLiteral("ollama")));
    EXPECT_TRUE(address->isVisible());
    EXPECT_EQ(address->text(), ProviderCatalog::findProvider(QStringLiteral("ollama"))->baseUrl);
    // A provider that needs no credential shows no field for one rather than a field nobody may type in.
    EXPECT_FALSE(apiKey->isVisible());

    section.reset();
    plugin.shutdown();
}

TEST(AiChatClientTest, ReportsTheReasonTheProviderReturnedInsteadOfTheTransportMessage) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));
    // clang-format off
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server]() {
        QTcpSocket* socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
            if (!socket->readAll().contains(QByteArrayLiteral("\r\n\r\n"))) {
                return;
            }
            const QByteArray body = QByteArrayLiteral("{\"type\":\"error\",\"error\":{\"type\":\"invalid_request_error\",\"message\":\"model: unknown model\"}}");
            socket->write(QByteArrayLiteral("HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nContent-Length: ") + QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body);
            socket->disconnectFromHost();
        });
    });
    // clang-format on

    const ProviderDescriptor* anthropic = ProviderCatalog::findProvider(QStringLiteral("anthropic"));
    ASSERT_NE(anthropic, nullptr);
    const QString model = QStringLiteral("claude-3-haiku-20240307");
    const ModelConnection connection{anthropic->id, model, {}, QStringLiteral("sk-test"), {}, ProviderCatalog::defaultParameters(*anthropic, model), {}};

    AiRequestGate clientGate;
    AiHttpChatClient client(clientGate);
    QVector<Error> failures;
    // clang-format off
    QObject::connect(&client, &AiChatClient::failed, &client, [&failures](const Error& error) { failures.append(error); });
    // clang-format on
    // clang-format off
    client.send({connection, QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("hello")}}}, {}}, [](const QString& key) { return key; });
    // clang-format on

    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !failures.isEmpty(); }));
    // clang-format on
    EXPECT_EQ(failures.first().code, QStringLiteral("ai_request_failed"));
    EXPECT_TRUE(failures.first().message.contains(QStringLiteral("unknown model"))) << failures.first().message.toStdString();
    // The status opens the detail and the body the service answered follows it, so the reason is readable in full.
    EXPECT_TRUE(failures.first().detail.startsWith(QStringLiteral("HTTP 400\n"))) << failures.first().detail.toStdString();
    EXPECT_TRUE(failures.first().detail.contains(QStringLiteral("unknown model")));
    EXPECT_EQ(failures.size(), 1);
}

TEST(AiToolContractTest, RebuildsACallHoweverTheServiceStreamedItsArguments) {
    // A service that streams the arguments as text fragments is the common shape.
    ToolCallAccumulator streamed(WireProtocol::OpenAiCompatible);
    streamed.consume(AiTestsHelper::streamEvent(R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"c1","function":{"name":"write_file","arguments":"{\"path\":"}}]}}]})"));
    streamed.consume(AiTestsHelper::streamEvent(R"({"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"\"a.txt\"}"}}]}}]})"));
    const auto rebuilt = streamed.calls();
    ASSERT_TRUE(rebuilt.hasValue()) << rebuilt.error().detail.toStdString();
    ASSERT_EQ(rebuilt.value().size(), 1);
    EXPECT_EQ(rebuilt.value().first().arguments.value(QStringLiteral("path")).toString(), QStringLiteral("a.txt"));

    // A service that sends them already parsed is read the same way instead of arriving with nothing.
    ToolCallAccumulator parsed(WireProtocol::OpenAiCompatible);
    parsed.consume(AiTestsHelper::streamEvent(R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"c2","function":{"name":"read_file","arguments":{"path":"b.txt"}}}]}}]})"));
    const auto whole = parsed.calls();
    ASSERT_TRUE(whole.hasValue()) << whole.error().detail.toStdString();
    ASSERT_EQ(whole.value().size(), 1);
    EXPECT_EQ(whole.value().first().arguments.value(QStringLiteral("path")).toString(), QStringLiteral("b.txt"));

    // Arguments that were cut off say what arrived, because a failure nobody can read explains nothing.
    ToolCallAccumulator cut(WireProtocol::OpenAiCompatible);
    cut.consume(AiTestsHelper::streamEvent(R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"c3","function":{"name":"write_file","arguments":"{\"path\":\"half"}}]}}]})"));
    const auto broken = cut.calls();
    ASSERT_TRUE(broken.hasValue());
    ASSERT_EQ(broken.value().size(), 1);
    EXPECT_TRUE(broken.value().first().unreadableArguments.contains(QStringLiteral("half"))) << broken.value().first().unreadableArguments.toStdString();
}

TEST(AiChatClientTest, DeliversTheCallTheOutputBudgetCutAndTheReasonItWasCut) {
    // Recorded from a completion whose tool call arguments were cut when the model reached its output budget.
    const QByteArray stream = QByteArrayLiteral("data: {\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_1\",\"type\":\"function\",\"function\":{\"name\":\"write_file\",\"arguments\":\"\"}}]},\"finish_reason\":null}]}\n\n"
                                                "data: {\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"{\\\"path\\\":\\\"index.html\\\",\\\"content\\\":\\\"<html\"}}]},\"finish_reason\":null}]}\n\n"
                                                "data: {\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"length\"}]}\n\n"
                                                "data: [DONE]\n\n");

    RecordedStreamServer server(stream);
    ASSERT_TRUE(server.listen());

    const ProviderDescriptor* openai = ProviderCatalog::findProvider(QStringLiteral("openai"));
    ASSERT_NE(openai, nullptr);
    const QString model = QStringLiteral("gpt-4o");
    const ModelConnection connection{openai->id, model, {}, QStringLiteral("sk-test"), {}, ProviderCatalog::defaultParameters(*openai, model), {}};

    AiRequestGate clientGate;
    AiHttpChatClient client(clientGate);
    QVector<Error> failures;
    QVector<ToolCall> delivered;
    QStringList reasons;
    // clang-format off
    QObject::connect(&client, &AiChatClient::failed, &client, [&failures](const Error& error) { failures.append(error); });
    QObject::connect(&client, &AiChatClient::finished, &client, [&delivered, &reasons](const QString&, const QVector<ToolCall>& calls, ChatUsage, const QString& finishReason) { delivered = calls; reasons.append(finishReason); });
    client.send({connection, server.address(), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("write it")}}}, {}}, [](const QString& key) { return key; });
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !reasons.isEmpty() || !failures.isEmpty(); }));
    // clang-format on

    // The transport delivers what arrived and the reason the answer was cut, and the run decides what to do with both.
    EXPECT_TRUE(failures.isEmpty());
    EXPECT_EQ(reasons, QStringList{QStringLiteral("length")});
    ASSERT_EQ(delivered.size(), 1);
    EXPECT_EQ(delivered.first().name, QStringLiteral("write_file"));
    EXPECT_TRUE(delivered.first().unreadableArguments.contains(QStringLiteral("<html"))) << delivered.first().unreadableArguments.toStdString();
}

TEST(AiConversationViewTest, ReadsAsAChatWithSidedBubblesGroupsAndTheTimeOnTheLine) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.size() == 1; }));
    // clang-format on
    // The answer is long enough that no font fits it on one line inside the share the chat allows, because that is what sends the time to a line of its own.
    clients.first()->deliver(QStringLiteral("Sure. I read the three files you asked about and the failure is in the writer, not the reader, so the fix belongs there. The reader was only reporting what it had been handed, and the writer had already dropped the column before the row reached it, which is why every value after the third one came back empty."), {}, QStringLiteral("stop"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.conversation(task.id).size() >= 2; }));
    // clang-format on
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.sendMessage(task.id, QStringLiteral("thanks"))).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.sendMessage(task.id, QStringLiteral("open a pull request with it"))).hasValue());

    std::unique_ptr<QWidget> view(plugin.createNavigationView(QStringLiteral("tasks"), nullptr));
    ASSERT_NE(view, nullptr);
    view->resize(920, 640);
    view->show();
    auto* badge = view->findChild<QLabel*>(QStringLiteral("aiTaskBadge"));
    ASSERT_NE(badge, nullptr);
    auto* surface = openTaskSurface(view.get(), QStringLiteral("aiTaskChat"));
    ASSERT_NE(surface, nullptr);
    surface->resize(view->size());
    auto* conversation = surface->findChild<AiConversationView*>();
    ASSERT_NE(conversation, nullptr);
    // clang-format off
    // The turn that is being written is a bubble of its own, so the transcript carries one more row than it has messages.
    ASSERT_TRUE(test::TestFutures::waitUntil([conversation]() { return conversation->findChildren<QWidget*>(QStringLiteral("aiConversationRow")).size() == 5; }));
    QList<int> previousWidths;
    // Where the time reads is decided from the width the layout gave the bubble, so this waits for those widths to stop moving rather than for the first one they are given.
    const auto bubblesSettled = [conversation, &previousWidths]() {
        QList<int> widths;
        for (auto* bubble : conversation->findChildren<QWidget*>(QStringLiteral("aiConversationBubble"))) { widths.append(bubble->width()); }
        const bool laidOut = widths.size() == 5 && std::none_of(widths.cbegin(), widths.cend(), [](int width) { return width <= 1; });
        const bool settled = laidOut && widths == previousWidths;
        previousWidths = widths;
        return settled;
    };
    ASSERT_TRUE(test::TestFutures::waitUntil(bubblesSettled));
    // clang-format on

    QCoreApplication::processEvents();
    const auto rows = conversation->findChildren<QWidget*>(QStringLiteral("aiConversationRow"));
    QList<QWidget*> bubbles;

    for (auto* row : rows) {
        bubbles.append(row->findChild<QWidget*>(QStringLiteral("aiConversationBubble")));
        ASSERT_NE(bubbles.last(), nullptr);
    }

    auto* scroll = conversation->findChild<QScrollArea*>(QStringLiteral("aiConversationScroll"));
    ASSERT_NE(scroll, nullptr);

    // What the reader wrote sits against the right edge behind their avatar, and what the agent answered against the left one behind its own.
    auto* you = rows.at(0)->findChild<ui::AvatarBadge*>(QStringLiteral("aiConversationYou"));
    auto* agent = rows.at(1)->findChild<ui::AvatarBadge*>(QStringLiteral("aiConversationAgent"));
    ASSERT_NE(you, nullptr);
    ASSERT_NE(agent, nullptr);
    EXPECT_EQ(you->mapTo(rows.at(0), QPoint(0, 0)).x() + you->width(), rows.at(0)->width());
    EXPECT_GT(you->mapTo(rows.at(0), QPoint(0, 0)).x(), bubbles.at(0)->mapTo(rows.at(0), QPoint(0, 0)).x());
    EXPECT_EQ(agent->mapTo(rows.at(1), QPoint(0, 0)).x(), 0);
    EXPECT_LT(agent->mapTo(rows.at(1), QPoint(0, 0)).x(), bubbles.at(1)->mapTo(rows.at(1), QPoint(0, 0)).x());
    EXPECT_GT(bubbles.at(0)->mapTo(rows.at(0), QPoint(0, 0)).x(), rows.at(0)->width() / 2);
    EXPECT_LT(bubbles.at(1)->mapTo(rows.at(1), QPoint(0, 0)).x(), rows.at(1)->width() / 2);

    // A message that follows one from the same writer repeats no avatar, because the group already named its writer.
    auto* repeated = rows.at(3)->findChild<ui::AvatarBadge*>(QStringLiteral("aiConversationYou"));
    ASSERT_NE(repeated, nullptr);
    EXPECT_FALSE(repeated->isVisible());

    // Every bubble obeys one width rule, so a short message and a long one are bounded by the same share.
    int widest = 0;

    for (auto* bubble : bubbles) {
        EXPECT_LT(bubble->width(), scroll->viewport()->width());
        widest = std::max(widest, bubble->width());
    }

    EXPECT_GT(widest, scroll->viewport()->width() / 2);
    EXPECT_LT(bubbles.at(2)->width(), widest);

    // The surface is closed by the same key that closes a tab.
    auto* closeAction = surface->findChild<QAction*>(QStringLiteral("aiTaskSurfaceClose"));
    ASSERT_NE(closeAction, nullptr);
    EXPECT_EQ(closeAction->shortcut(), QKeySequence(QKeySequence::Close));
    EXPECT_EQ(closeAction->shortcutContext(), Qt::WidgetWithChildrenShortcut);

    // The time reads at the end of a line that has room for it and takes a line of its own when it does not.
    auto* shortTime = bubbles.at(2)->findChild<QLabel*>(QStringLiteral("aiConversationTime"));
    auto* shortLine = bubbles.at(2)->findChild<QWidget*>(QStringLiteral("aiConversationLine"));
    ASSERT_NE(shortTime, nullptr);
    ASSERT_NE(shortLine, nullptr);
    EXPECT_EQ(shortTime->parentWidget(), shortLine);
    EXPECT_TRUE(shortTime->isVisible());

    auto* longTime = bubbles.at(1)->findChild<QLabel*>(QStringLiteral("aiConversationTime"));
    ASSERT_NE(longTime, nullptr);
    EXPECT_EQ(longTime->parentWidget(), bubbles.at(1));
    EXPECT_TRUE(longTime->isVisible());

    // A message that follows one from the same writer is part of the same group, so it opens no gap of its own.
    EXPECT_GT(rows.at(2)->layout()->contentsMargins().top(), 0);
    EXPECT_EQ(rows.at(3)->layout()->contentsMargins().top(), 0);

    // The text of a bubble reads from the left, never centred, and no bubble passes the share the chat allows.
    for (auto* bubble : bubbles) {
        auto* written = bubble->findChild<ui::MarkdownView*>(QStringLiteral("aiConversationContent"));
        ASSERT_NE(written, nullptr);
        EXPECT_EQ(written->document()->defaultTextOption().alignment() & Qt::AlignHCenter, Qt::Alignment());
        EXPECT_LE(bubble->width(), static_cast<int>(scroll->viewport()->width() * 0.7) + 1);
    }

    // A message written with the return key keeps the line it was broken at.
    auto* broken = bubbles.at(2)->findChild<ui::MarkdownView*>(QStringLiteral("aiConversationContent"));
    ASSERT_NE(broken, nullptr);

    // Everything inside a bubble reads at the size the chat was given, so no line is smaller than the words beside it.
    const int reading = plugin.executionSettings().chatFontSize;
    EXPECT_EQ(broken->document()->defaultFont().pointSize(), reading);

    for (auto* bubble : bubbles) {
        for (auto* written : bubble->findChildren<QLabel*>()) {
            EXPECT_EQ(written->font().pointSize(), reading) << written->objectName().toStdString();
        }
    }

    // A turn that is being written says so inside the bubble its answer will land in.
    auto* stop = surface->findChild<QPushButton*>(QStringLiteral("destructiveButton"));
    ASSERT_NE(stop, nullptr);
    const bool running = plugin.runState(task.id) != TaskRunState::Idle;
    EXPECT_EQ(stop->isVisible(), running);

    if (running) {
        auto* thinking = conversation->findChild<QWidget*>(QStringLiteral("aiConversationThinking"));
        auto* busy = conversation->findChild<ui::BusyIndicator*>(QStringLiteral("aiConversationBusy"));
        auto* phase = conversation->findChild<QLabel*>(QStringLiteral("aiConversationPhase"));
        ASSERT_NE(thinking, nullptr);
        ASSERT_NE(busy, nullptr);
        ASSERT_NE(phase, nullptr);
        EXPECT_TRUE(thinking->isVisible());
        EXPECT_TRUE(busy->isVisible());
        EXPECT_FALSE(phase->text().isEmpty());

        // The bubble that is waiting carries the indicator instead of an empty box with a time in it.
        auto* pending = thinking->parentWidget();
        ASSERT_NE(pending, nullptr);
        auto* pendingTime = pending->findChild<QLabel*>(QStringLiteral("aiConversationTime"));
        ASSERT_NE(pendingTime, nullptr);
        EXPECT_FALSE(pendingTime->isVisible());

        QTest::mouseClick(stop, Qt::LeftButton);
        // clang-format off
        ASSERT_TRUE(test::TestFutures::waitUntil([&plugin, &task]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
        ASSERT_TRUE(test::TestFutures::waitUntil([conversation, stop]() { return conversation->findChild<QWidget*>(QStringLiteral("aiConversationThinking")) == nullptr && !stop->isVisible(); }));
        // clang-format on
    }

    // The action that sends is a circle, so its radius can never disagree with its shape.
    auto* send = conversation->findChild<QPushButton*>(QStringLiteral("aiConversationSend"));
    ASSERT_NE(send, nullptr);
    EXPECT_EQ(send->width(), send->height());
    EXPECT_EQ(send->width(), ui::ThemeManager::instance().theme().metric(ui::ThemeMetric::RoundButtonSize));

    // The composer opens on one line and grows with every line written into it until the bound it may not pass.
    auto* composer = conversation->findChild<ui::TextField*>(QStringLiteral("aiConversationComposer"));
    ASSERT_NE(composer, nullptr);
    const int oneLine = composer->height();
    composer->setPlainText(QStringLiteral("first\nsecond\nthird"));
    QCoreApplication::processEvents();
    const int threeLines = composer->height();
    EXPECT_GT(threeLines, oneLine);
    composer->setPlainText(QStringList(60, QStringLiteral("line")).join(QLatin1Char('\n')));
    QCoreApplication::processEvents();
    EXPECT_GT(composer->height(), threeLines);
    EXPECT_LT(composer->height(), conversation->height() / 2);

    view.reset();
    plugin.shutdown();
}

TEST(AiConversationViewTest, ReadsEveryRoleAsMarkdownAndAnswersEachCallInsideItsOwnRow) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    filesystem::FileSystemService files;
    host.useFileSystem(files);
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    task.workdir = workdir.path();
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.size() == 1; }));
    // clang-format on

    const QString first = QDir(QDir(workdir.path()).canonicalPath()).filePath(QStringLiteral("one.txt"));
    const QString second = QDir(QDir(workdir.path()).canonicalPath()).filePath(QStringLiteral("two.txt"));
    FakeChatClient* agent = clients.first();
    agent->deliverToolCalls({{QStringLiteral("c1"), QStringLiteral("write_file"), QJsonObject{{QStringLiteral("path"), first}, {QStringLiteral("content"), QStringLiteral("one")}}}, {QStringLiteral("c2"), QStringLiteral("write_file"), QJsonObject{{QStringLiteral("path"), second}, {QStringLiteral("content"), QStringLiteral("two")}}}});
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([agent]() { return agent->sendCalls == 2; }));
    // clang-format on
    agent->deliver(QStringLiteral("Both files are written:\n\n```cpp\nint answer = 42;\n```"), {10, 20}, QStringLiteral("stop"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on

    std::unique_ptr<QWidget> view(plugin.createNavigationView(QStringLiteral("tasks"), nullptr));
    ASSERT_NE(view, nullptr);
    view->resize(960, 700);
    view->show();
    auto* badge = view->findChild<QLabel*>(QStringLiteral("aiTaskBadge"));
    ASSERT_NE(badge, nullptr);
    auto* surface = openTaskSurface(view.get(), QStringLiteral("aiTaskChat"));
    ASSERT_NE(surface, nullptr);
    surface->resize(view->size());
    auto* conversation = surface->findChild<AiConversationView*>();
    ASSERT_NE(conversation, nullptr);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([conversation]() { return !conversation->findChildren<QWidget*>(QStringLiteral("aiConversationTool")).isEmpty(); }));
    // clang-format on
    QCoreApplication::processEvents();

    // A result answers inside the row of the call it belongs to, so it opens no bubble of its own.
    const auto rows = conversation->findChildren<QWidget*>(QStringLiteral("aiConversationRow"));
    const auto tools = conversation->findChildren<QWidget*>(QStringLiteral("aiConversationTool"));
    ASSERT_EQ(tools.size(), 2);

    // A call is read as the name the tool publishes and the one thing it is doing, never as the arguments it was given.
    for (auto* entry : tools) {
        auto* name = entry->findChild<QLabel*>(QStringLiteral("aiConversationToolName"));
        auto* activity = entry->findChild<QLabel*>(QStringLiteral("aiConversationToolActivity"));
        auto* mark = entry->findChild<QLabel*>(QStringLiteral("aiConversationToolIcon"));
        ASSERT_NE(name, nullptr);
        ASSERT_NE(activity, nullptr);
        ASSERT_NE(mark, nullptr);
        EXPECT_EQ(name->text(), host.translate(QStringLiteral("ai.tool-title.write-file")));
        EXPECT_FALSE(name->text().contains(QLatin1Char('_')));
        EXPECT_TRUE(activity->text().startsWith(host.translate(QStringLiteral("ai.tool-activity.write-file")).section(QLatin1Char('%'), 0, 0)));
        EXPECT_TRUE(activity->text().contains(QStringLiteral(".txt")));
        EXPECT_FALSE(activity->text().contains(QStringLiteral("content")));
        EXPECT_FALSE(mark->pixmap().isNull());

        // The name is read above what it is doing, with room between them, so a call is two lines rather than one long row.
        EXPECT_GE(activity->mapTo(entry, QPoint(0, 0)).y(), name->mapTo(entry, QPoint(0, 0)).y() + name->height() + 3);
        EXPECT_GE(activity->mapTo(entry, QPoint(0, 0)).x() + activity->contentsMargins().left(), name->mapTo(entry, QPoint(0, 0)).x());
    }

    // A result opens no bubble of its own, so a turn of two calls is one turn rather than three messages.
    EXPECT_EQ(rows.size(), 3);

    // Every role reads as Markdown, so a fenced block arrives as code rather than as its own source.
    QTextBrowser* answer = nullptr;

    for (auto* content : conversation->findChildren<QTextBrowser*>(QStringLiteral("aiConversationContent"))) {
        if (content->toPlainText().contains(QStringLiteral("Both files are written"))) {
            answer = content;
        }
    }

    ASSERT_NE(answer, nullptr);
    EXPECT_FALSE(answer->toPlainText().contains(QStringLiteral("```")));
    EXPECT_TRUE(answer->toPlainText().contains(QStringLiteral("int answer = 42;")));

    view.reset();
    plugin.shutdown();
}

TEST(AiConversationViewTest, FollowsTheReaderInsteadOfDraggingThemToTheEndOfEveryMessage) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    for (int index = 0; index < 30; ++index) {
        ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.sendMessage(task.id, QStringLiteral("message %1 that is long enough to take a line of its own in the conversation").arg(index))).hasValue());
    }

    std::unique_ptr<QWidget> view(plugin.createNavigationView(QStringLiteral("tasks"), nullptr));
    ASSERT_NE(view, nullptr);
    view->resize(900, 420);
    view->show();
    auto* badge = view->findChild<QLabel*>(QStringLiteral("aiTaskBadge"));
    ASSERT_NE(badge, nullptr);
    auto* surface = openTaskSurface(view.get(), QStringLiteral("aiTaskChat"));
    ASSERT_NE(surface, nullptr);
    surface->resize(view->size());
    auto* conversation = surface->findChild<AiConversationView*>();
    ASSERT_NE(conversation, nullptr);
    auto* scroll = conversation->findChild<QScrollArea*>(QStringLiteral("aiConversationScroll"));
    ASSERT_NE(scroll, nullptr);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([scroll]() { return scroll->verticalScrollBar()->maximum() > 0; }));
    ASSERT_TRUE(test::TestFutures::waitUntil([scroll]() { return scroll->verticalScrollBar()->value() == scroll->verticalScrollBar()->maximum(); }));
    // clang-format on

    // A reader who has scrolled up is reading, so a message that arrives leaves the viewport where they left it.
    scroll->verticalScrollBar()->setValue(0);
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.sendMessage(task.id, QStringLiteral("this one arrives while the reader is up here"))).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([conversation, scroll]() { return conversation->findChildren<QWidget*>(QStringLiteral("aiConversationRow")).size() >= 31 && scroll->verticalScrollBar()->maximum() > 0; }));
    // clang-format on

    for (int pass = 0; pass < 5; ++pass) {
        QCoreApplication::processEvents();
    }

    EXPECT_EQ(scroll->verticalScrollBar()->value(), 0);

    // A reader who is at the end is following, so the next message brings them along.
    scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.sendMessage(task.id, QStringLiteral("and this one arrives while they are at the end"))).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([scroll]() { return scroll->verticalScrollBar()->value() == scroll->verticalScrollBar()->maximum(); }));
    // clang-format on

    view.reset();
    plugin.shutdown();
}

TEST(AiTaskSurfaceTest, WritesTheLogOfARunWhileItRunsWithoutMovingTheReader) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    QVector<TaskExecution> recordedRuns;
    QVector<ExecutionLogEntry> recordedEntries;

    for (int index = 0; index < 2; ++index) {
        recordedRuns.append({QStringLiteral("execution-%1").arg(index), task.id, ExecutionStatus::Running, now.addSecs(-index), {}, 0, 0, {}, {}, {}});
    }

    for (int index = 0; index < 80; ++index) {
        recordedEntries.append({QStringLiteral("entry-%1").arg(index), QStringLiteral("execution-1"), index, now, ExecutionLogLevel::Info, ExecutionLogKind::Started, QStringLiteral("line %1").arg(index)});
    }

    const AiTestsHelper::RecordedRuns recorded = AiTestsHelper::installExecutionRows(host, recordedRuns, recordedEntries);

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    AiTaskInfoDialog surface(plugin, host, task, nullptr);
    surface.resize(900, 460);
    surface.show();
    auto* executions = surface.findChild<QTableWidget*>(QStringLiteral("aiExecutionGrid"));
    auto* entries = surface.findChild<QTableWidget*>(QStringLiteral("aiLogGrid"));
    ASSERT_NE(executions, nullptr);
    ASSERT_NE(entries, nullptr);
    auto* tabs = surface.findChild<ui::TabWidget*>(QStringLiteral("aiTaskInfoTabs"));
    ASSERT_NE(tabs, nullptr);
    tabs->setCurrentIndex(tabs->indexOf(entries->parentWidget()));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([executions]() { return executions->rowCount() == 2; }));
    // clang-format on

    // The reader opened the second run and is reading further down its entries.
    executions->setCurrentCell(1, 0);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([entries]() { return entries->rowCount() == 80; }));
    // clang-format on
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([entries]() { return entries->verticalScrollBar()->maximum() > 0; }));
    // clang-format on
    entries->verticalScrollBar()->setValue(entries->verticalScrollBar()->maximum());
    ASSERT_GT(entries->verticalScrollBar()->value(), 0);
    const QString onScreen = entries->item(entries->rowAt(0), 3)->text();
    ASSERT_FALSE(onScreen.isEmpty());

    recorded.logs->append({QStringLiteral("entry-80"), QStringLiteral("execution-1"), 80, now, ExecutionLogLevel::Info, ExecutionLogKind::Started, QStringLiteral("line 80")});
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());

    // The entry the run wrote reaches the open surface, and it leaves the reader where they were.
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([entries]() { return entries->rowCount() == 81; }));
    // clang-format on
    EXPECT_EQ(executions->currentRow(), 1);
    EXPECT_EQ(entries->item(entries->rowAt(0), 3)->text(), onScreen);

    // A reader already on the newest entry follows the run instead of being anchored behind it.
    entries->verticalScrollBar()->setValue(0);
    recorded.logs->append({QStringLiteral("entry-81"), QStringLiteral("execution-1"), 81, now, ExecutionLogLevel::Info, ExecutionLogKind::Started, QStringLiteral("line 81")});
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.stopTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([entries]() { return entries->rowCount() == 82; }));
    // clang-format on
    EXPECT_EQ(entries->verticalScrollBar()->value(), 0);
    EXPECT_EQ(entries->item(0, 3)->text(), QStringLiteral("line 81"));

    plugin.shutdown();
}

TEST(AiPluginTest, AnswersAToolCallItCouldNotReadInsteadOfEndingTheRun) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.size() == 1; }));
    // clang-format on

    ToolCall unreadable;
    unreadable.id = QStringLiteral("call-1");
    unreadable.name = QStringLiteral("read_file");
    unreadable.unreadableArguments = QStringLiteral("{\"path\":");
    clients.first()->deliverToolCalls({unreadable});

    // The run keeps going and the model is told what arrived, so it writes the call again instead of losing the turn.
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.first()->sentMessages.size() > 1; }));
    // clang-format on
    EXPECT_EQ(plugin.runState(task.id), TaskRunState::Running);
    const QString transcript = QString::fromUtf8(QJsonDocument(clients.first()->sentMessages).toJson(QJsonDocument::Compact));
    EXPECT_TRUE(transcript.contains(QStringLiteral("could not be read"))) << transcript.toStdString();
    EXPECT_TRUE(transcript.contains(QStringLiteral("read_file"))) << transcript.toStdString();

    // Repeating the same unreadable call is answered by the repetition budget rather than forever.
    for (int attempt = 0; attempt < ProviderCatalog::aiLimits().repeatedToolCallLimit + 1; ++attempt) {
        clients.first()->deliverToolCalls({unreadable});
    }
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on
    EXPECT_EQ(plugin.lastStopReason(task.id), AgentStopReason::ToolRepetition);
    EXPECT_EQ(plugin.lastExecutionStatus(task.id), ExecutionStatus::Succeeded);

    plugin.shutdown();
}

TEST(AiTaskRepositoryTest, RecordsTheReasonARunStoppedAndRefusesOneNobodyDeclared) {
    const QVector<AgentStopReason> reasons{AgentStopReason::Answered, AgentStopReason::IterationLimit, AgentStopReason::OutputBudget, AgentStopReason::ToolRepetition, AgentStopReason::Cancelled, AgentStopReason::Failed};

    for (const auto reason : reasons) {
        const QString name = AiTaskRepository::agentStopReasonName(reason);
        EXPECT_FALSE(name.isEmpty());
        ASSERT_TRUE(AiTaskRepository::agentStopReasonFromName(name).has_value()) << name.toStdString();
        EXPECT_EQ(AiTaskRepository::agentStopReasonFromName(name).value(), reason);
    }

    EXPECT_FALSE(AiTaskRepository::agentStopReasonFromName(QStringLiteral("gave-up")).has_value());
    EXPECT_FALSE(AiTaskRepository::agentStopReasonFromName(QString{}).has_value());

    // A run reads back with the reason it recorded, and a reason nobody declares is refused where it is read.
    test::TestPluginHost host;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    TaskExecution stopped{QStringLiteral("execution-1"), QStringLiteral("task-1"), ExecutionStatus::Succeeded, now, now, 10, 20, QStringLiteral("length"), {}, QStringLiteral("half"), AgentStopReason::OutputBudget};
    AiTestsHelper::installExecutionRows(host, {stopped}, {});
    AiTaskRepository repository(host);
    const auto restored = test::TestFutures::awaitFuture(repository.executions(QStringLiteral("task-1")));
    ASSERT_TRUE(restored.hasValue());
    ASSERT_EQ(restored.value().size(), 1);
    EXPECT_EQ(restored.value().first().stopReason, AgentStopReason::OutputBudget);

    auto previous = host.queryHandler;
    // clang-format off
    host.queryHandler = [previous](const QString& statement, const QVariantList& bindings) {
        auto rows = previous(statement, bindings);
        if (statement.contains(QStringLiteral("FROM ai_tasks_executions")) && rows.hasValue() && !rows.value().isEmpty()) {
            rows.value().first().insert(QStringLiteral("stop_reason"), QStringLiteral("gave-up"));
        }
        return rows;
    };
    // clang-format on
    EXPECT_EQ(test::TestFutures::awaitFuture(repository.executions(QStringLiteral("task-1"))).error().code, QStringLiteral("ai_tasks_execution_invalid"));
}

TEST(AiToolContractTest, GivesEachProtocolTheConversationShapeItDemands) {
    const QJsonObject system{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), QStringLiteral("be brief")}};
    const QJsonObject firstUser{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("one")}};
    const QJsonObject secondUser{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("two")}};
    const QJsonObject assistant{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), QStringLiteral("here")}};
    const QJsonObject secondAssistant{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), QStringLiteral("and here")}};

    // The OpenAI compatible API accepts the conversation as it stands, so nothing is rewritten for it.
    const QJsonArray conversation{system, firstUser, secondUser, assistant, secondAssistant};
    EXPECT_FALSE(ToolContracts::protocolRequiresAlternatingRoles(WireProtocol::OpenAiCompatible));
    EXPECT_EQ(ToolContracts::enforceProtocolShape(WireProtocol::OpenAiCompatible, conversation), conversation);

    // The Anthropic API refuses a repeated role, so two turns of one role become the turn it expects.
    EXPECT_TRUE(ToolContracts::protocolRequiresAlternatingRoles(WireProtocol::Anthropic));
    const QJsonArray shaped = ToolContracts::enforceProtocolShape(WireProtocol::Anthropic, conversation);
    ASSERT_EQ(shaped.size(), 3);
    EXPECT_EQ(shaped.at(0).toObject(), system);
    EXPECT_EQ(shaped.at(1).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("user"));
    EXPECT_EQ(shaped.at(2).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("assistant"));
    const QJsonArray joined = shaped.at(1).toObject().value(QStringLiteral("content")).toArray();
    ASSERT_EQ(joined.size(), 2);
    EXPECT_EQ(joined.at(0).toObject().value(QStringLiteral("text")).toString(), QStringLiteral("one"));
    EXPECT_EQ(joined.at(1).toObject().value(QStringLiteral("text")).toString(), QStringLiteral("two"));

    // A conversation whose oldest turns were dropped can open with an assistant turn, which answers nothing and is left out.
    const QJsonArray opening{system, assistant, firstUser};
    const QJsonArray openingShaped = ToolContracts::enforceProtocolShape(WireProtocol::Anthropic, opening);
    ASSERT_EQ(openingShaped.size(), 2);
    EXPECT_EQ(openingShaped.at(1).toObject(), firstUser);

    // A turn already written as content blocks keeps its blocks when it is joined.
    const QJsonObject blocks{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("tool_result")}, {QStringLiteral("tool_use_id"), QStringLiteral("call-1")}}}}};
    const QJsonArray withBlocks = ToolContracts::enforceProtocolShape(WireProtocol::Anthropic, QJsonArray{blocks, firstUser});
    ASSERT_EQ(withBlocks.size(), 1);
    const QJsonArray blockContent = withBlocks.at(0).toObject().value(QStringLiteral("content")).toArray();
    ASSERT_EQ(blockContent.size(), 2);
    EXPECT_EQ(blockContent.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("tool_result"));
    EXPECT_EQ(blockContent.at(1).toObject().value(QStringLiteral("text")).toString(), QStringLiteral("one"));
}

TEST(AiPluginTest, SendsAgainThePictureAStoredResultCarries) {
    const QByteArray picture = QByteArrayLiteral("bytes of a picture");
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    // A run before this one read an image, so its answer is stored with the bytes it returned.
    auto previous = host.queryHandler;
    const QString stamp = now.toString(Qt::ISODateWithMs);
    // clang-format off
    host.queryHandler = [previous, picture, stamp](const QString& statement, const QVariantList& bindings) {
        if (!statement.contains(QStringLiteral("FROM ai_tasks_messages")) || !statement.contains(QStringLiteral("tool_call_id"))) {
            return previous(statement, bindings);
        }
        persistence::DatabaseRows rows;
        rows.append({{QStringLiteral("id"), QStringLiteral("message-2")}, {QStringLiteral("task_id"), QStringLiteral("task-1")}, {QStringLiteral("sequence"), 2}, {QStringLiteral("role"), QStringLiteral("tool")}, {QStringLiteral("content"), QStringLiteral("media/square.png")}, {QStringLiteral("tool_calls"), QStringLiteral("[]")}, {QStringLiteral("tool_call_id"), QStringLiteral("call-1")}, {QStringLiteral("summarized_until"), 0}, {QStringLiteral("created_at_utc"), stamp}, {QStringLiteral("image_data"), picture}, {QStringLiteral("image_media_type"), QStringLiteral("image/png")}});
        rows.append({{QStringLiteral("id"), QStringLiteral("message-1")}, {QStringLiteral("task_id"), QStringLiteral("task-1")}, {QStringLiteral("sequence"), 1}, {QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), QStringLiteral("Looking at it")}, {QStringLiteral("tool_calls"), QStringLiteral(R"([{"id":"call-1","name":"read_image","arguments":{}}])")}, {QStringLiteral("tool_call_id"), QString{}}, {QStringLiteral("summarized_until"), 0}, {QStringLiteral("created_at_utc"), stamp}, {QStringLiteral("image_data"), QByteArray{}}, {QStringLiteral("image_media_type"), QString{}}});
        return Result<persistence::DatabaseRows>::success(rows);
    };
    // clang-format on

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.loadConversation(task.id)).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.size() == 1 && !clients.first()->sentMessages.isEmpty(); }));
    // clang-format on

    // The picture is sent again from what was stored, so the model still sees what an earlier run showed it.
    const QString body = QString::fromUtf8(QJsonDocument(clients.first()->sentMessages).toJson(QJsonDocument::Compact));
    EXPECT_TRUE(body.contains(QString::fromUtf8(picture.toBase64()))) << body.left(600).toStdString();
    EXPECT_FALSE(body.contains(QStringLiteral("does not read images")));
    plugin.shutdown();
}

TEST(AiPluginTest, KeepsARunAliveWhileAnotherOneStartsUnderTheSignalItIsEmitting) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    QVector<AiTask> tasks;

    for (int index = 0; index < 8; ++index) {
        tasks.append(AiTestsHelper::makeTask(QStringLiteral("task-%1").arg(index), workspace.id));
    }

    AiTestsHelper::installAiRows(host, {workspace}, tasks, {});
    QJsonObject settings = host.settingsDocument;
    settings.insert(QStringLiteral("execution"), QJsonObject{{QStringLiteral("parallelExecutions"), 0}});
    host.settingsDocument = settings;

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    // Starting a run while another one is announcing itself grows the collection of running tasks under that announcement.
    bool started = false;
    // clang-format off
    QObject::connect(&plugin, &AiPlugin::taskRunStateChanged, &plugin, [&plugin, &tasks, &started](const QString& taskId) {
        if (started || taskId != tasks.first().id) {
            return;
        }
        started = true;
        for (int index = 1; index < tasks.size(); ++index) {
            std::ignore = plugin.startTask(tasks.at(index).id);
        }
    });
    // clang-format on
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(tasks.first().id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.size() == tasks.size(); }));
    // clang-format on

    // Every run reached the model with its own instructions, so none of them was reading a run that had moved.
    for (auto* client : clients) {
        ASSERT_NE(client, nullptr);
        EXPECT_EQ(client->sendCalls, 1);
        ASSERT_FALSE(client->sentMessages.isEmpty());
        EXPECT_FALSE(client->sentMessages.first().toObject().value(QStringLiteral("content")).toString().isEmpty());
    }

    for (const auto& task : tasks) {
        EXPECT_NE(plugin.runState(task.id), TaskRunState::Idle) << task.id.toStdString();
    }

    plugin.shutdown();
}

TEST(AiToolRegistryTest, NamesTheToolAndTheArgumentOfEveryCallItRefuses) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    AiToolRegistry registry(host);

    // A call judged against the schema the model received is answered by name, so no tool leaves the model guessing what it got wrong.
    for (const auto& schema : registry.schemas()) {
        const QJsonArray required = schema.parameters.value(QStringLiteral("required")).toArray();
        if (required.isEmpty()) {
            continue;
        }

        QVector<ToolResult> results;
        // clang-format off
        const auto collect = [&results](ToolResult result) { results.append(std::move(result)); };
        registry.invoke({QStringLiteral("call-1"), schema.name, QJsonObject{}, {}}, QStringLiteral("/tmp"), collect);
        ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !results.isEmpty(); })) << schema.name.toStdString();
        // clang-format on
        EXPECT_TRUE(results.first().failed) << schema.name.toStdString();
        EXPECT_TRUE(results.first().text.contains(schema.name)) << schema.name.toStdString() << ": " << results.first().text.toStdString();
        EXPECT_TRUE(results.first().text.contains(required.first().toString())) << schema.name.toStdString() << ": " << results.first().text.toStdString();
    }

    // A value of the wrong type is named the same way, so the model reads which argument and which type were expected.
    QVector<ToolResult> mistyped;
    // clang-format off
    const auto collectMistyped = [&mistyped](ToolResult result) { mistyped.append(std::move(result)); };
    registry.invoke({QStringLiteral("call-2"), QStringLiteral("read_file"), QJsonObject{{QStringLiteral("path"), 12}}, {}}, QStringLiteral("/tmp"), collectMistyped);
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !mistyped.isEmpty(); }));
    // clang-format on
    EXPECT_TRUE(mistyped.first().failed);
    EXPECT_TRUE(mistyped.first().text.contains(QStringLiteral("read_file"))) << mistyped.first().text.toStdString();
    EXPECT_TRUE(mistyped.first().text.contains(QStringLiteral("path"))) << mistyped.first().text.toStdString();
}

TEST(AiTasksViewTest, ReadsAScheduledTaskAndAStoppedRunAsTheStatesTheyAreIn) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask waiting = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    TaskSchedule schedule;
    schedule.kind = ScheduleKind::Interval;
    schedule.intervalSeconds = 3600;
    schedule.timeZoneId = QByteArrayLiteral("UTC");
    schedule.nextRunAtUtc = now.addSecs(3600);
    waiting.schedule = schedule;
    const AiTask plain = AiTestsHelper::makeTask(QStringLiteral("task-2"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {waiting, plain}, {});
    AiTestsHelper::installExecutionRows(host, {}, {});

    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    std::unique_ptr<QWidget> view(plugin.createNavigationView(QStringLiteral("tasks"), nullptr));
    ASSERT_NE(view, nullptr);
    view->resize(1100, 700);
    view->show();

    // A task waiting for a schedule is not a task nobody scheduled, so it reads as its own state.
    const auto badges = view->findChildren<QLabel*>(QStringLiteral("aiTaskBadge"));
    ASSERT_EQ(badges.size(), 2);
    QStringList states;

    for (const auto* badge : badges) {
        states.append(badge->property("badge").toString());
    }

    states.sort();
    EXPECT_EQ(states, (QStringList{QStringLiteral("idle"), QStringLiteral("scheduled")}));
    // clang-format off
    const auto scheduled = std::find_if(badges.cbegin(), badges.cend(), [](const QLabel* badge) { return badge->property("badge").toString() == QStringLiteral("scheduled"); });
    // clang-format on
    ASSERT_NE(scheduled, badges.cend());
    EXPECT_EQ((*scheduled)->text(), host.translate(QStringLiteral("ai.badge.scheduled")));
    plugin.shutdown();
}

TEST(AiTasksViewTest, ReadsARunStoppedByALimitAsStoppedRatherThanAsSucceeded) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});
    const TaskExecution stopped{QStringLiteral("execution-1"), task.id, ExecutionStatus::Succeeded, now, now, 10, 20, {}, {}, {}, AgentStopReason::IterationLimit};
    AiTestsHelper::installExecutionRows(host, {stopped}, {});

    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    std::unique_ptr<QWidget> view(plugin.createNavigationView(QStringLiteral("tasks"), nullptr));
    ASSERT_NE(view, nullptr);
    view->resize(1100, 700);
    view->show();

    // A green badge over a line saying the run stopped without answering contradicts itself.
    auto* badge = view->findChild<QLabel*>(QStringLiteral("aiTaskBadge"));
    ASSERT_NE(badge, nullptr);
    EXPECT_EQ(badge->property("badge").toString(), QStringLiteral("stopped"));
    EXPECT_EQ(badge->text(), host.translate(QStringLiteral("ai.badge.stopped")));
    plugin.shutdown();
}

TEST(AiConversationViewTest, MeasuresATurnOfToolsByTheSameRuleAsAMessage) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    const QString command = QStringLiteral("find . -maxdepth 3 -type f -print -o -name node_modules -prune");
    auto previous = host.queryHandler;
    const QString stamp = now.toString(Qt::ISODateWithMs);
    // clang-format off
    host.queryHandler = [previous, command, stamp](const QString& statement, const QVariantList& bindings) {
        if (!statement.contains(QStringLiteral("FROM ai_tasks_messages")) || !statement.contains(QStringLiteral("tool_call_id"))) {
            return previous(statement, bindings);
        }
        const QString calls = QStringLiteral(R"([{"id":"call-1","name":"run_command","arguments":{"command":"%1"}}])").arg(command);
        persistence::DatabaseRows rows;
        rows.append({{QStringLiteral("id"), QStringLiteral("message-2")}, {QStringLiteral("task_id"), QStringLiteral("task-1")}, {QStringLiteral("sequence"), 2}, {QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), QString{}}, {QStringLiteral("tool_calls"), calls}, {QStringLiteral("tool_call_id"), QString{}}, {QStringLiteral("summarized_until"), 0}, {QStringLiteral("created_at_utc"), stamp}, {QStringLiteral("image_data"), QByteArray{}}, {QStringLiteral("image_media_type"), QString{}}});
        rows.append({{QStringLiteral("id"), QStringLiteral("message-1")}, {QStringLiteral("task_id"), QStringLiteral("task-1")}, {QStringLiteral("sequence"), 1}, {QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("List the files of this project and tell me what it is")}, {QStringLiteral("tool_calls"), QStringLiteral("[]")}, {QStringLiteral("tool_call_id"), QString{}}, {QStringLiteral("summarized_until"), 0}, {QStringLiteral("created_at_utc"), stamp}, {QStringLiteral("image_data"), QByteArray{}}, {QStringLiteral("image_media_type"), QString{}}});
        return Result<persistence::DatabaseRows>::success(rows);
    };
    // clang-format on

    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    AiConversationView view(plugin, host, nullptr);
    view.resize(900, 600);
    view.show();
    view.setTask(task.id);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return view.findChildren<ui::RoundedSurface*>(QStringLiteral("aiConversationBubble")).size() == 2; }));
    // clang-format on
    QApplication::processEvents();

    const auto bubbles = view.findChildren<ui::RoundedSurface*>(QStringLiteral("aiConversationBubble"));
    ASSERT_EQ(bubbles.size(), 2);
    auto* activity = view.findChild<QLabel*>(QStringLiteral("aiConversationToolActivity"));
    ASSERT_NE(activity, nullptr);
    EXPECT_TRUE(activity->text().contains(QStringLiteral("maxdepth"))) << activity->text().toStdString();

    // A turn of tools is as wide as the words it carries, exactly like a message, instead of collapsing to one word.
    auto* carrying = bubbles.constLast();
    EXPECT_GE(carrying->width(), activity->fontMetrics().horizontalAdvance(activity->text())) << carrying->width();

    // The line the reader sees is not wrapped, because a box measured to the exact advance of its text still breaks it.
    EXPECT_LT(activity->heightForWidth(activity->width()), 2 * activity->fontMetrics().height()) << activity->width();
    EXPECT_LE(carrying->width(), static_cast<int>(view.width() * 0.70) + 1);

    for (auto* bubble : bubbles) {
        EXPECT_LE(bubble->width(), static_cast<int>(view.width() * 0.70) + 1) << bubble->width();
    }

    plugin.shutdown();
}

TEST(AiPluginTest, DispatchesOneDueOccurrenceOnceWhileItsQueueRowIsStillBeingWritten) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask due = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    TaskSchedule schedule;
    schedule.kind = ScheduleKind::Interval;
    schedule.intervalSeconds = 3600;
    schedule.timeZoneId = QByteArrayLiteral("UTC");
    schedule.nextRunAtUtc = now.addSecs(-60);
    due.schedule = schedule;
    AiTestsHelper::installAiRows(host, {workspace}, {due}, {});

    // The row that takes the task out of the schedule is still being written while the scheduler wakes again.
    QVector<std::shared_ptr<QPromise<Result<void>>>> pending;
    auto previous = host.transactionFutureHandler;
    // clang-format off
    host.transactionFutureHandler = [&pending, previous](const QVector<persistence::DatabaseStatement>& statements) {
        const bool queueing = !statements.isEmpty() && statements.first().statement.contains(QStringLiteral("ai_tasks_queue"));
        if (!queueing) {
            return previous(statements);
        }
        auto promise = std::make_shared<QPromise<Result<void>>>();
        promise->start();
        pending.append(promise);
        return promise->future();
    };
    // clang-format on

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !pending.isEmpty(); }));
    // clang-format on

    // The scheduler is given every chance to wake again before that write lands.
    for (int turn = 0; turn < 40; ++turn) {
        QApplication::processEvents(QEventLoop::AllEvents, 5);
    }

    EXPECT_EQ(pending.size(), 1);

    // A second start asked for while that write is in flight is refused instead of reaching storage as a duplicate.
    EXPECT_EQ(test::TestFutures::awaitFuture(plugin.startTask(due.id)).error().code, QStringLiteral("ai_tasks_task_busy"));
    EXPECT_EQ(pending.size(), 1);

    // The row that could not be written returns the task to the schedule it was taken from.
    EXPECT_EQ(plugin.runState(due.id), TaskRunState::Waiting);
    pending.first()->addResult(Result<void>::failure({"write_failed", "Write failed", {}}));
    pending.first()->finish();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(due.id) == TaskRunState::Idle; }));
    // clang-format on
    ASSERT_NE(plugin.tasks().constFirst().schedule.has_value(), false);
    EXPECT_EQ(plugin.tasks().constFirst().schedule->nextRunAtUtc, schedule.nextRunAtUtc);
    EXPECT_EQ(plugin.tasks().constFirst().column, due.column);
    plugin.shutdown();
}

TEST(AiPluginTest, GivesTheProtocolItsShapeOnTheTurnThatFollowsACompaction) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);

    const ProviderDescriptor* anthropic = ProviderCatalog::findProvider(QStringLiteral("anthropic"));
    ASSERT_NE(anthropic, nullptr);
    const QString model = QStringLiteral("claude-3-haiku-20240307");
    const ModelConnection connection{anthropic->id, model, {}, QStringLiteral("sk-test"), {}, ProviderCatalog::defaultParameters(*anthropic, model), {}};
    AiAgent agent = AiTestsHelper::testAgent();
    agent.connectionKey = ModelConnections::connectionKey(connection);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {}, {connection}, {agent});

    // The conversation is far larger than the window of this model, so the turn that follows has to be compacted.
    const QString filler(120000, QLatin1Char('x'));
    auto previous = host.queryHandler;
    const QString stamp = now.toString(Qt::ISODateWithMs);
    // clang-format off
    host.queryHandler = [previous, filler, stamp](const QString& statement, const QVariantList& bindings) {
        if (!statement.contains(QStringLiteral("FROM ai_tasks_messages")) || !statement.contains(QStringLiteral("tool_call_id"))) {
            return previous(statement, bindings);
        }
        persistence::DatabaseRows rows;
        for (int index = 8; index >= 1; --index) {
            const bool user = index % 2 == 1;
            rows.append({{QStringLiteral("id"), QStringLiteral("message-%1").arg(index)}, {QStringLiteral("task_id"), QStringLiteral("task-1")}, {QStringLiteral("sequence"), index}, {QStringLiteral("role"), user ? QStringLiteral("user") : QStringLiteral("assistant")}, {QStringLiteral("content"), filler}, {QStringLiteral("tool_calls"), QStringLiteral("[]")}, {QStringLiteral("tool_call_id"), QString{}}, {QStringLiteral("summarized_until"), 0}, {QStringLiteral("created_at_utc"), stamp}, {QStringLiteral("image_data"), QByteArray{}}, {QStringLiteral("image_media_type"), QString{}}});
        }
        return Result<persistence::DatabaseRows>::success(rows);
    };
    // clang-format on

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.loadConversation(task.id)).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());

    // The run asks for a summary of what no longer fits, which is a second client of its own.
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.size() == 2; }));
    // clang-format on
    clients.at(1)->deliver(QStringLiteral("They discussed the project"), {10, 20}, QStringLiteral("stop"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !clients.first()->sentMessages.isEmpty(); }));
    // clang-format on

    // The Anthropic API refuses a repeated role and a conversation that opens with an assistant turn, whether or not it was compacted.
    const QJsonArray sent = clients.first()->sentMessages;
    ASSERT_GE(sent.size(), 2);
    QString previousRole;

    for (const auto& value : sent) {
        const QString role = value.toObject().value(QStringLiteral("role")).toString();
        if (role == QStringLiteral("system")) {
            continue;
        }
        if (previousRole.isEmpty()) {
            EXPECT_EQ(role, QStringLiteral("user"));
        }
        EXPECT_NE(role, previousRole) << QString::fromUtf8(QJsonDocument(sent).toJson(QJsonDocument::Compact)).left(400).toStdString();
        previousRole = role;
    }

    plugin.shutdown();
}

TEST(AiTranslationsTest, ReachesEveryKeyItBuildsFromAnEnumOrFromTheCatalog) {
    const plugins::TranslationEntries english = translations::AiCatalog::english();
    const QVector<ExecutionLogKind> kinds{ExecutionLogKind::Started, ExecutionLogKind::Iteration, ExecutionLogKind::Compacted, ExecutionLogKind::RequestSent, ExecutionLogKind::FirstTokenReceived, ExecutionLogKind::ResponseReceived, ExecutionLogKind::UsageReported, ExecutionLogKind::ToolCalled, ExecutionLogKind::ToolReturned, ExecutionLogKind::Throttled, ExecutionLogKind::Succeeded, ExecutionLogKind::Failed, ExecutionLogKind::Cancelled};
    const QVector<ExecutionPhase> phases{ExecutionPhase::Idle, ExecutionPhase::Queued, ExecutionPhase::Throttled, ExecutionPhase::Sending, ExecutionPhase::Streaming, ExecutionPhase::CallingTool, ExecutionPhase::Compacting, ExecutionPhase::Running};
    const QVector<TaskColumn> columns{TaskColumn::Todo, TaskColumn::Doing, TaskColumn::Blocked, TaskColumn::Review, TaskColumn::Done};

    QSet<QString> asked;
    // clang-format off
    for (const auto kind : kinds) { asked.insert(QStringLiteral("ai.log-kind.") + AiTaskRepository::executionLogKindName(kind)); }
    for (const auto phase : phases) { asked.insert(QStringLiteral("ai.phase.") + AiPlugin::phaseName(phase)); }
    for (const auto column : columns) { asked.insert(QStringLiteral("ai.column.%1").arg(AiTaskRepository::columnName(column))); }
    // clang-format on
    // A turn calling several tools says how many, which is the one sentence of that family the phase name does not spell.
    asked.insert(QStringLiteral("ai.phase.tool-count"));

    for (const auto& provider : ProviderCatalog::providerCatalog()) {
        asked.insert(provider.titleKey);
        for (const auto& parameter : provider.parameters) {
            asked.insert(parameter.titleKey);
            for (const auto& option : parameter.options) {
                asked.insert(option.titleKey);
            }
        }
    }

    // A key the plugin builds must be spelled, otherwise the reader is shown the key itself.
    for (const auto& key : asked) {
        EXPECT_TRUE(english.contains(key)) << "the ai catalog spells no " << key.toStdString();
    }

    // A key nothing builds is dead weight, so every key of those families answers something.
    const QStringList families{QStringLiteral("ai.log-kind."), QStringLiteral("ai.phase."), QStringLiteral("ai.column."), QStringLiteral("ai.provider."), QStringLiteral("ai.parameter."), QStringLiteral("ai.effort.")};

    for (auto entry = english.constBegin(); entry != english.constEnd(); ++entry) {
        // clang-format off
        const QString key = entry.key();
        const bool owned = std::any_of(families.constBegin(), families.constEnd(), [key](const QString& family) { return key.startsWith(family); });
        // clang-format on
        if (!owned) {
            continue;
        }

        EXPECT_TRUE(asked.contains(entry.key())) << "nothing builds " << entry.key().toStdString();
    }
}

TEST(AiTranslationsTest, SpellsEveryKeyInEveryLanguageTheSelectorOffers) {
    workpane::plugins::ai::AiPlugin plugin;
    workpane::test::TestCatalogs::expectCompleteCatalog(QStringLiteral("ai"), plugin.translations());
}

// A command line agent is invoked rather than requested, so the catalog holds it to what that means.
TEST(AiProviderCatalogTest, RefusesEveryMalformedCommandLineProviderItDeclaresARefusalFor) {
    QFile providersFile(QStringLiteral(":/workpane/ai/assets/providers.json"));
    ASSERT_TRUE(providersFile.open(QIODevice::ReadOnly));
    const QJsonObject shipped = QJsonDocument::fromJson(providersFile.readAll()).object();
    const QByteArray models = QJsonDocument(QJsonObject{{QStringLiteral("providers"), QJsonObject{{QStringLiteral("claude-cli"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("claude-opus-5")}, {QStringLiteral("context"), 1000000}, {QStringLiteral("output"), 128000}, {QStringLiteral("traits"), QJsonArray{}}}}}}}}).toJson(QJsonDocument::Compact);

    // clang-format off
    const auto onlyThis = [&shipped](const QJsonObject& replacement) {
        QJsonObject copy = shipped;
        copy.insert(QStringLiteral("providers"), QJsonArray{replacement});
        return QJsonDocument(copy).toJson(QJsonDocument::Compact);
    };
    const auto declared = [&shipped](const QString& id) {
        QJsonObject found;
        for (const auto& value : shipped.value(QStringLiteral("providers")).toArray()) {
            if (value.toObject().value(QStringLiteral("id")).toString() == id) {
                found = value.toObject();
            }
        }
        return found;
    };
    // clang-format on

    const QJsonObject sound = declared(QStringLiteral("claude-cli"));
    ASSERT_FALSE(sound.isEmpty());
    ASSERT_TRUE(ProviderCatalog::loadAiCatalog(onlyThis(sound), models).hasValue());

    QVector<QPair<QString, QJsonObject>> malformed;

    QJsonObject noCommand = sound;
    noCommand.remove(QStringLiteral("command"));
    malformed.append({QStringLiteral("no program at all"), noCommand});

    QJsonObject emptyProgram = sound;
    emptyProgram.insert(QStringLiteral("command"), QJsonObject{{QStringLiteral("program"), QString{}}, {QStringLiteral("arguments"), QJsonArray{QStringLiteral("{prompt}")}}});
    malformed.append({QStringLiteral("an empty program"), emptyProgram});

    QJsonObject notAnObject = sound;
    notAnObject.insert(QStringLiteral("command"), QStringLiteral("claude -p"));
    malformed.append({QStringLiteral("a program that is not a program"), notAnObject});

    QJsonObject unknownField = sound;
    unknownField.insert(QStringLiteral("command"), QJsonObject{{QStringLiteral("program"), QStringLiteral("claude")}, {QStringLiteral("arguments"), QJsonArray{QStringLiteral("{prompt}")}}, {QStringLiteral("shell"), true}});
    malformed.append({QStringLiteral("a command field nobody declares"), unknownField});

    QJsonObject emptyArgument = sound;
    emptyArgument.insert(QStringLiteral("command"), QJsonObject{{QStringLiteral("program"), QStringLiteral("claude")}, {QStringLiteral("arguments"), QJsonArray{QStringLiteral("{prompt}"), QString{}}}});
    malformed.append({QStringLiteral("an empty argument"), emptyArgument});

    QJsonObject noPrompt = sound;
    noPrompt.insert(QStringLiteral("command"), QJsonObject{{QStringLiteral("program"), QStringLiteral("claude")}, {QStringLiteral("arguments"), QJsonArray{QStringLiteral("--quiet")}}});
    malformed.append({QStringLiteral("a provider that never passes the prompt"), noPrompt});

    QJsonObject noModel = sound;
    noModel.insert(QStringLiteral("command"), QJsonObject{{QStringLiteral("program"), QStringLiteral("claude")}, {QStringLiteral("arguments"), QJsonArray{QStringLiteral("-p"), QStringLiteral("{prompt}")}}, {QStringLiteral("clearedVariables"), QJsonArray{QStringLiteral("ANTHROPIC_API_KEY")}}});
    malformed.append({QStringLiteral("a provider that never passes the model"), noModel});

    QJsonObject variablesNotAList = sound;
    variablesNotAList.insert(QStringLiteral("command"), QJsonObject{{QStringLiteral("program"), QStringLiteral("claude")}, {QStringLiteral("arguments"), QJsonArray{QStringLiteral("-p"), QStringLiteral("{prompt}")}}, {QStringLiteral("clearedVariables"), QStringLiteral("ANTHROPIC_API_KEY")}});
    malformed.append({QStringLiteral("variables to clear that are not a list"), variablesNotAList});

    QJsonObject emptyVariable = sound;
    emptyVariable.insert(QStringLiteral("command"), QJsonObject{{QStringLiteral("program"), QStringLiteral("claude")}, {QStringLiteral("arguments"), QJsonArray{QStringLiteral("-p"), QStringLiteral("{prompt}")}}, {QStringLiteral("clearedVariables"), QJsonArray{QStringLiteral("  ")}}});
    malformed.append({QStringLiteral("an empty variable to clear"), emptyVariable});

    QJsonObject withAddress = sound;
    withAddress.insert(QStringLiteral("baseUrl"), QStringLiteral("https://example.com"));
    malformed.append({QStringLiteral("a command line provider declaring an address"), withAddress});

    QJsonObject withCredential = sound;
    withCredential.insert(QStringLiteral("requiresApiKey"), true);
    withCredential.insert(QStringLiteral("apiKeyVariable"), QStringLiteral("SOMETHING"));
    malformed.append({QStringLiteral("a command line provider requiring a credential"), withCredential});

    for (const auto& shape : malformed) {
        const auto rejected = ProviderCatalog::loadAiCatalog(onlyThis(shape.second), models);
        ASSERT_FALSE(rejected.hasValue()) << shape.first.toStdString() << " was accepted";
        EXPECT_EQ(rejected.error().code, QStringLiteral("ai_catalog_invalid")) << shape.first.toStdString();
    }

    QJsonObject wireWithProgram = declared(QStringLiteral("openai"));
    ASSERT_FALSE(wireWithProgram.isEmpty());
    wireWithProgram.insert(QStringLiteral("command"), QJsonObject{{QStringLiteral("program"), QStringLiteral("openai")}, {QStringLiteral("arguments"), QJsonArray{QStringLiteral("{prompt}")}}});
    EXPECT_FALSE(ProviderCatalog::loadAiCatalog(onlyThis(wireWithProgram), models).hasValue()) << "a provider reached over a wire declared a program";
}

// A price is data like every other field of the catalog, so one written in a shape nobody declares rejects the plugin rather than reaching a run.
TEST(AiProviderCatalogTest, RefusesAModelPriceThatIsNotANumberOrIsBelowZero) {
    QFile providersFile(QStringLiteral(":/workpane/ai/assets/providers.json"));
    ASSERT_TRUE(providersFile.open(QIODevice::ReadOnly));
    const QJsonObject shipped = QJsonDocument::fromJson(providersFile.readAll()).object();
    QJsonObject openai;

    for (const auto& value : shipped.value(QStringLiteral("providers")).toArray()) {
        if (value.toObject().value(QStringLiteral("id")).toString() == QStringLiteral("openai")) {
            openai = value.toObject();
        }
    }

    ASSERT_FALSE(openai.isEmpty());
    openai.insert(QStringLiteral("preferredModels"), QJsonArray{QStringLiteral("gpt-4o")});
    QJsonObject onlyOpenAi = shipped;
    onlyOpenAi.insert(QStringLiteral("providers"), QJsonArray{openai});
    const QByteArray providers = QJsonDocument(onlyOpenAi).toJson(QJsonDocument::Compact);
    // clang-format off
    const auto withCost = [](const QJsonValue& input, const QJsonValue& output) {
        QJsonObject model{{QStringLiteral("id"), QStringLiteral("gpt-4o")}, {QStringLiteral("name"), QStringLiteral("GPT-4o")}, {QStringLiteral("context"), 128000}, {QStringLiteral("output"), 16384}, {QStringLiteral("traits"), QJsonArray{QStringLiteral("function-calling"), QStringLiteral("sampling")}}};
        if (!input.isNull()) { model.insert(QStringLiteral("inputCost"), input); }
        if (!output.isNull()) { model.insert(QStringLiteral("outputCost"), output); }
        return QJsonDocument(QJsonObject{{QStringLiteral("providers"), QJsonObject{{QStringLiteral("openai"), QJsonArray{model}}}}}).toJson(QJsonDocument::Compact);
    };
    // clang-format on

    ASSERT_TRUE(ProviderCatalog::loadAiCatalog(providers, withCost(2.5e-06, 1e-05)).hasValue());
    ASSERT_TRUE(ProviderCatalog::loadAiCatalog(providers, withCost(QJsonValue::Null, QJsonValue::Null)).hasValue());

    const QVector<QPair<QString, QByteArray>> refused{{QStringLiteral("a price written as text"), withCost(QStringLiteral("2.5e-06"), 1e-05)}, {QStringLiteral("a price below zero"), withCost(-1.0, 1e-05)}, {QStringLiteral("an answer price written as text"), withCost(2.5e-06, QStringLiteral("cheap"))}};

    for (const auto& shape : refused) {
        const auto rejected = ProviderCatalog::loadAiCatalog(providers, shape.second);
        ASSERT_FALSE(rejected.hasValue()) << shape.first.toStdString() << " was accepted";
        EXPECT_EQ(rejected.error().code, QStringLiteral("ai_catalog_invalid")) << shape.first.toStdString();
    }
}

// The prompt reaches the agent as written, because no shell reads the arguments it is given.
TEST(AiCliChatClientTest, PassesAPromptAShellWouldHaveActedOnExactlyAsItIsWritten) {
    const ProviderDescriptor* provider = ProviderCatalog::findProvider(QStringLiteral("claude-cli"));
    ASSERT_NE(provider, nullptr);
    EXPECT_EQ(provider->protocol, WireProtocol::CommandLine);

    const QString hostile = QStringLiteral("say \"$HOME\" && rm -rf / ; `whoami` | tee $(id) 'quoted'\nsecond line\ttabbed\\backslash");
    const QStringList arguments = CommandLineAgents::commandLineArguments(provider->commandLine, hostile, QStringLiteral("/tmp/project"), QStringLiteral("claude-opus-5"));

    EXPECT_TRUE(arguments.contains(hostile)) << "the prompt was altered on its way to the agent";
    EXPECT_EQ(arguments.count(hostile), 1);
    EXPECT_FALSE(arguments.contains(QString::fromLatin1(commandLinePromptPlaceholder)));
    EXPECT_EQ(arguments.size(), provider->commandLine.arguments.size());
    // The prompt follows the flag the provider declares for it, whatever position the catalog gives that pair.
    const qsizetype promptAt = arguments.indexOf(hostile);
    ASSERT_GT(promptAt, 0);
    EXPECT_EQ(arguments.at(promptAt - 1), QStringLiteral("-p"));

    // The model the connection names replaces its placeholder rather than reaching the agent as the mark itself.
    EXPECT_TRUE(arguments.contains(QStringLiteral("claude-opus-5")));
    EXPECT_FALSE(arguments.contains(QString::fromLatin1(commandLineModelPlaceholder)));

    const ProviderDescriptor* codex = ProviderCatalog::findProvider(QStringLiteral("codex-cli"));
    ASSERT_NE(codex, nullptr);
    const QStringList placed = CommandLineAgents::commandLineArguments(codex->commandLine, hostile, QStringLiteral("/tmp/project"), QStringLiteral("gpt-5.4"));
    EXPECT_TRUE(placed.contains(QStringLiteral("/tmp/project")));
    EXPECT_TRUE(placed.contains(hostile));
    EXPECT_FALSE(placed.contains(QString::fromLatin1(commandLineWorkdirPlaceholder)));
    EXPECT_TRUE(placed.contains(QStringLiteral("gpt-5.4")));

    const QJsonArray messages{QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), QStringLiteral("be brief")}}, QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("first")}}, QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), QStringLiteral("answered")}}, QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QString{}}}};
    const QString rendered = CommandLineAgents::renderConversationPrompt(messages);
    EXPECT_TRUE(rendered.contains(QStringLiteral("be brief")));
    EXPECT_TRUE(rendered.contains(QStringLiteral("first")));
    EXPECT_TRUE(rendered.contains(QStringLiteral("answered")));
    EXPECT_LT(rendered.indexOf(QStringLiteral("be brief")), rendered.indexOf(QStringLiteral("first")));
}

// The transport starts a real program, so it is exercised against one rather than only through the arguments it would build.
TEST(AiCliChatClientTest, RunsTheProgramWhereTheTaskSaysAndAnswersWithWhatItPrinted) {
    QTemporaryDir project;
    ASSERT_TRUE(project.isValid());
    const QString workdir = QFileInfo(project.path()).canonicalFilePath();
    // clang-format off
    const auto resolver = [](const QString&) { return QCoreApplication::applicationFilePath(); };
    // clang-format on
    qputenv("WORKPANE_TEST_CLI_AGENT", QByteArrayLiteral("1"));
    // A credential in the environment would make the agent bill that key instead of the subscription the reader pays for.
    qputenv("ANTHROPIC_API_KEY", QByteArrayLiteral("a-key-the-agent-must-not-see"));
    AiCliChatClient client(resolver, nullptr);

    ChatUsage reported;
    QString answered;
    Error failure;
    bool finished = false;
    // clang-format off
    QObject::connect(&client, &AiChatClient::finished, &client, [&answered, &finished, &reported](const QString& content, const QVector<ToolCall>&, ChatUsage usage, const QString&) { answered = content; reported = usage; finished = true; });
    QObject::connect(&client, &AiChatClient::failed, &client, [&failure, &finished](const Error& error) { failure = error; finished = true; });
    const auto translate = [](const QString& key) { return key; };
    // clang-format on

    ModelConnection connection;
    connection.providerId = QStringLiteral("claude-cli");
    connection.modelId = QStringLiteral("claude-cli");
    const QString prompt = QStringLiteral("say \"$HOME\" && rm -rf / ; `whoami`\nsecond line");
    ChatRequest request;
    request.connection = connection;
    request.messages = QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), prompt}}};
    request.workdir = workdir;

    client.send(request, translate);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&finished]() { return finished; }));
    // clang-format on
    ASSERT_TRUE(failure.code.isEmpty()) << failure.code.toStdString() << " " << failure.message.toStdString();

    // The agent ran where the task said and read the prompt exactly as it was written.
    EXPECT_TRUE(answered.contains(QStringLiteral("directory: ") + workdir)) << answered.toStdString();
    EXPECT_TRUE(answered.contains(prompt.section(QLatin1Char('\n'), 0, 0))) << answered.toStdString();
    EXPECT_FALSE(client.running());

    // The provider names that variable among the ones it clears, so the agent never reads it.
    EXPECT_TRUE(answered.contains(QStringLiteral("credential:"))) << answered.toStdString();

    // The input of the agent is closed at once, so a program that reads what it was piped is not left waiting for something nobody sends.
    EXPECT_TRUE(answered.contains(QStringLiteral("stdin: closed"))) << answered.toStdString();

    // No command line agent reports what it spent, so what it was given and what it answered are counted at the four characters a token averages.
    EXPECT_EQ(reported.outputTokens, static_cast<qint64>(answered.size()) / 4);
    // What is sent is the rendered conversation, so it counts at least what the prompt alone would.
    EXPECT_GE(reported.inputTokens, static_cast<qint64>(prompt.size()) / 4);
    EXPECT_GT(reported.inputTokens, 0);
    EXPECT_FALSE(answered.contains(QStringLiteral("a-key-the-agent-must-not-see"))) << answered.toStdString();
    qunsetenv("ANTHROPIC_API_KEY");
    qunsetenv("WORKPANE_TEST_CLI_AGENT");
}

// A run that failed without printing anything still tells the reader what happened.
TEST(AiCliChatClientTest, ReportsTheExitCodeWhenTheProgramFailedWithoutPrintingAReason) {
    QTemporaryDir project;
    ASSERT_TRUE(project.isValid());
    // clang-format off
    const auto resolver = [](const QString&) { return QCoreApplication::applicationFilePath(); };
    // clang-format on
    qputenv("WORKPANE_TEST_CLI_AGENT", QByteArrayLiteral("silent"));
    AiCliChatClient client(resolver, nullptr);

    Error failure;
    bool finished = false;
    // clang-format off
    QObject::connect(&client, &AiChatClient::finished, &client, [&finished](const QString&, const QVector<ToolCall>&, ChatUsage, const QString&) { finished = true; });
    QObject::connect(&client, &AiChatClient::failed, &client, [&failure, &finished](const Error& error) { failure = error; finished = true; });
    const auto translate = [](const QString& key) { return key + QStringLiteral(" %1"); };
    // clang-format on

    ModelConnection connection;
    connection.providerId = QStringLiteral("claude-cli");
    connection.modelId = QStringLiteral("claude-cli");
    ChatRequest request;
    request.connection = connection;
    request.messages = QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("anything")}}};
    request.workdir = QFileInfo(project.path()).canonicalFilePath();

    client.send(request, translate);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&finished]() { return finished; }));
    // clang-format on

    EXPECT_EQ(failure.code, QStringLiteral("ai_cli_failed"));
    EXPECT_EQ(failure.message, QStringLiteral("ai.error.exit-code 9"));
    EXPECT_EQ(failure.detail, QStringLiteral("9"));
    qunsetenv("WORKPANE_TEST_CLI_AGENT");
}

// A program that is not installed is named rather than answered with nothing.
TEST(AiCliChatClientTest, RefusesToRunWhatIsNotInstalledAndWhatHasNowhereToRun) {
    // clang-format off
    const auto missing = [](const QString&) { return QString{}; };
    // clang-format on
    AiCliChatClient client(missing, nullptr);

    Error failure;
    // clang-format off
    QObject::connect(&client, &AiChatClient::failed, &client, [&failure](const Error& error) { failure = error; });
    const auto translate = [](const QString& key) { return key; };
    // clang-format on

    ModelConnection connection;
    connection.providerId = QStringLiteral("claude-cli");
    connection.modelId = QStringLiteral("claude-cli");
    ChatRequest request;
    request.connection = connection;
    request.messages = QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("anything")}}};
    request.workdir = QDir::tempPath();

    client.send(request, translate);
    EXPECT_EQ(failure.code, QStringLiteral("ai_cli_program_missing"));
    EXPECT_EQ(failure.detail, QStringLiteral("claude"));

    // A task with no working directory has nowhere to run an agent.
    failure = {};
    request.workdir.clear();
    client.send(request, translate);
    EXPECT_EQ(failure.code, QStringLiteral("ai_cli_workdir_required"));

    // A connection naming a provider reached over a wire is not a command line agent.
    failure = {};
    request.workdir = QDir::tempPath();
    request.connection.providerId = QStringLiteral("openai");
    client.send(request, translate);
    EXPECT_EQ(failure.code, QStringLiteral("ai_cli_provider_invalid"));
}

// A command line agent is started and stopped again and again, so nothing it owns may outlive the run that started it.
TEST(AiCliChatClientTest, SurvivesManyRunsStartedAndStoppedBeforeTheyAnswer) {
    QTemporaryDir project;
    ASSERT_TRUE(project.isValid());
    const QString workdir = QFileInfo(project.path()).canonicalFilePath();
    // clang-format off
    const auto resolver = [](const QString&) { return QCoreApplication::applicationFilePath(); };
    const auto translate = [](const QString& key) { return key; };
    // clang-format on
    qputenv("WORKPANE_TEST_CLI_AGENT", QByteArrayLiteral("1"));

    ModelConnection connection;
    connection.providerId = QStringLiteral("claude-cli");
    connection.modelId = QStringLiteral("claude-cli");
    ChatRequest request;
    request.connection = connection;
    request.messages = QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("anything")}}};
    request.workdir = workdir;

    for (int round = 0; round < 15; ++round) {
        auto client = std::make_unique<AiCliChatClient>(resolver, nullptr);
        bool settled = false;
        // clang-format off
        QObject::connect(client.get(), &AiChatClient::finished, client.get(), [&settled](const QString&, const QVector<ToolCall>&, ChatUsage, const QString&) { settled = true; });
        QObject::connect(client.get(), &AiChatClient::failed, client.get(), [&settled](const Error&) { settled = true; });
        // clang-format on
        client->send(request, translate);

        // Every third run is let finish, and the rest are stopped while the agent is still writing.
        if (round % 3 == 0) {
            // clang-format off
            ASSERT_TRUE(test::TestFutures::waitUntil([&settled]() { return settled; }));
            // clang-format on
        } else {
            client->cancel();
        }

        EXPECT_FALSE(client->running());
        client.reset();
        QCoreApplication::processEvents();
    }

    qunsetenv("WORKPANE_TEST_CLI_AGENT");
}

} // namespace workpane::plugins::ai
