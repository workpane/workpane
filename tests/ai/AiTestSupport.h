#pragma once

#include "AiAgentPrompt.h"
#include "AiChatClient.h"
#include "AiCommandRunner.h"
#include "AiConnectionDialog.h"
#include "AiModelConnection.h"
#include "AiModelDiscovery.h"
#include "AiPlugin.h"
#include "AiProviderCatalog.h"
#include "AiSecret.h"
#include "AiTaskDialog.h"
#include "AiTaskInfoDialog.h"
#include "AiTaskRepository.h"
#include "AiToolContract.h"
#include "AiToolRegistry.h"
#include "AiTranslations.h"
#include "CronExpression.h"
#include "TestFuture.h"
#include "TestPluginHost.h"
#include "TestProcess.h"
#include "agent/mcp/McpClient.h"
#include "ui/AppStyle.h"
#include "ui/Components.h"
#include "ui/TabBar.h"
#include "ui/Theme.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontMetrics>
#include <QHeaderView>
#include <QImage>
#include <QJsonDocument>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QSysInfo>
#include <QTabWidget>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTimeZone>
#include <QTimer>
#include <QToolButton>
#include <QWidget>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>

namespace workpane::plugins::ai {

// Serves a canned server-sent event stream so a provider response is exercised without a network call.
class RecordedStreamServer final {
  public:
    explicit RecordedStreamServer(const QByteArray& stream) {
        // clang-format off
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this, stream]() {
            QTcpSocket* socket = m_server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, stream]() {
                m_request.append(socket->readAll());
                if (!m_request.contains(QByteArrayLiteral("\r\n\r\n"))) {
                    return;
                }
                socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: ") + QByteArray::number(stream.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + stream);
                socket->disconnectFromHost();
            });
        });
        // clang-format on
    }

    [[nodiscard]] bool listen() {
        return m_server.listen(QHostAddress::LocalHost, 0);
    }

    [[nodiscard]] QString address() const {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    }

    [[nodiscard]] QByteArray requestBody() const {
        const qsizetype boundary = m_request.indexOf(QByteArrayLiteral("\r\n\r\n"));
        return boundary < 0 ? QByteArray{} : m_request.mid(boundary + 4);
    }

  private:
    QTcpServer m_server;
    QByteArray m_request;
};

// The Brave and Tavily payloads are the documented response shapes of each service, recorded rather than requested.
class RecordedSearchServer final {
  public:
    explicit RecordedSearchServer(QByteArray body) {
        // clang-format off
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this, body]() {
            QTcpSocket* socket = m_server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, body]() {
                m_request.append(socket->readAll());
                if (!m_request.contains(QByteArrayLiteral("\r\n\r\n"))) {
                    return;
                }
                const QByteArray status = m_rejecting ? QByteArrayLiteral("HTTP/1.1 401 Unauthorized") : QByteArrayLiteral("HTTP/1.1 200 OK");
                socket->write(status + QByteArrayLiteral("\r\nContent-Type: application/json\r\nContent-Length: ") + QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body);
                socket->disconnectFromHost();
            });
        });
        // clang-format on
    }

    [[nodiscard]] bool listen() {
        return m_server.listen(QHostAddress::LocalHost, 0);
    }

    [[nodiscard]] bool listenRejecting() {
        m_rejecting = true;
        return listen();
    }

    [[nodiscard]] QString address() const {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    }

    [[nodiscard]] QByteArray requestLine() const {
        return m_request.left(m_request.indexOf(QByteArrayLiteral("\r\n")));
    }

    [[nodiscard]] QByteArray requestHead() const {
        return m_request;
    }

  private:
    QTcpServer m_server;
    QByteArray m_request;
    bool m_rejecting{false};
};

class FakeChatClient : public AiChatClient {
  public:
    void send(const ChatRequest& request, const std::function<QString(const QString&)>&) override {
        ++sendCalls;
        sentConnection = request.connection;
        sentMessages = request.messages;
        sentTools = request.tools;
        m_running = true;
        emit started();
    }

    void cancel() override {
        ++cancelCalls;
        m_running = false;
    }

    [[nodiscard]] bool running() const override {
        return m_running;
    }

    void deliver(const QString& content, ChatUsage usage, const QString& finishReason) {
        m_running = false;
        emit contentReceived(content);
        emit finished(content, {}, usage, finishReason);
    }

    void deliverToolCalls(const QVector<ToolCall>& calls) {
        m_running = false;
        emit finished(QString{}, calls, {}, QStringLiteral("tool_calls"));
    }

    void fail(const Error& error) {
        m_running = false;
        emit failed(error);
    }

    int sendCalls{0};
    int cancelCalls{0};
    ModelConnection sentConnection;
    QJsonArray sentMessages;
    QVector<ToolSchema> sentTools;

  private:
    bool m_running{false};
};

class AiTestsHelper final {
  public:
    // The wait is short because a stopped command is detached, so nothing the platform leaves behind outlives the case that started it.
    // The Windows wait uses ping because timeout refuses to run when the console input is redirected, which is how a test runner starts it.
    static QString sleepingCommand(int seconds);
    // The command tests speak the shell of the running platform, because the runner starts the native one.
    static QString printWorkingDirectoryCommand();
    // A command that only succeeds beside a file proves which directory it really ran in.
    static QString readFileCommand(const QString& name);
    static QString failingCommand();
    static ModelConnection testConnection();
    static AiWorkspace validWorkspace(int position, bool active);
    static AiTask makeTask(const QString& id, const QString& workspaceId);
    static void installEmptyProviderRows(test::TestPluginHost& host);
    // The rows a run records grow while it runs, so the fixture hands them back for a test to append to.
    struct RecordedRuns final {
        std::shared_ptr<QVector<TaskExecution>> executions;
        std::shared_ptr<QVector<ExecutionLogEntry>> logs;
    };
    static RecordedRuns installExecutionRows(test::TestPluginHost& host, const QVector<TaskExecution>& executions, const QVector<ExecutionLogEntry>& logs);
    // A reader only receives the columns its own statement names, so a column it forgot to select stays missing here too.
    static persistence::DatabaseRows selectedColumns(const QString& statement, const persistence::DatabaseRows& rows);
    static QJsonObject settingsDocument(const QVector<ModelConnection>& connections, const QString& defaultConnectionKey, const QVector<AiAgent>& agents = {AiTestsHelper::testAgent()});
    static AiAgent testAgent();
    // A recorded event is written as the JSON a service really sends, because nesting it as literals is unreadable and costs the compiler its memory.
    static QJsonObject streamEvent(const QByteArray& json);
    static void installAiRows(test::TestPluginHost& host, const QVector<AiWorkspace>& workspaces, const QVector<AiTask>& tasks, const QStringList& queued, const QVector<ModelConnection>& connections = {AiTestsHelper::testConnection()}, const QVector<AiAgent>& agents = {AiTestsHelper::testAgent()});
};

inline QJsonObject AiTestsHelper::streamEvent(const QByteArray& json) {
    return QJsonDocument::fromJson(json).object();
}

inline QString AiTestsHelper::sleepingCommand(int seconds) {
    return test::TestProcesses::sleepingCommand(seconds);
}

inline QString AiTestsHelper::printWorkingDirectoryCommand() {
#ifdef Q_OS_WIN
    return QStringLiteral("cd");
#else
    return QStringLiteral("pwd");
#endif
}

inline QString AiTestsHelper::readFileCommand(const QString& name) {
#ifdef Q_OS_WIN
    return QStringLiteral("type %1").arg(name);
#else
    return QStringLiteral("cat %1").arg(name);
#endif
}

inline QString AiTestsHelper::failingCommand() {
    return QStringLiteral("echo broken && exit 3");
}

inline ModelConnection AiTestsHelper::testConnection() {
    const ProviderDescriptor* provider = ProviderCatalog::findProvider(QStringLiteral("openai"));
    const QString model = QStringLiteral("gpt-4o");
    return {provider->id, model, {}, QStringLiteral("sk-test"), {}, ProviderCatalog::defaultParameters(*provider, model), {}};
}

inline AiWorkspace AiTestsHelper::validWorkspace(int position, bool active) {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    return {QStringLiteral("workspace-%1").arg(position), QStringLiteral("Workspace %1").arg(position), position, active, now, now};
}

inline AiTask AiTestsHelper::makeTask(const QString& id, const QString& workspaceId) {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    AiTask task;
    task.id = id;
    task.workspaceId = workspaceId;
    task.title = QStringLiteral("Review");
    task.description = QStringLiteral("Inspect the project");
    task.prompt = QStringLiteral("Review this repository");
    task.agentId = AiTestsHelper::testAgent().id;
    task.createdAtUtc = now;
    task.updatedAtUtc = now;
    return task;
}

inline void AiTestsHelper::installEmptyProviderRows(test::TestPluginHost& host) {
    // clang-format off
    host.queryHandler = [](const QString&, const QVariantList&) { return Result<persistence::DatabaseRows>::success({}); };
    // clang-format on
}

inline AiTestsHelper::RecordedRuns AiTestsHelper::installExecutionRows(test::TestPluginHost& host, const QVector<TaskExecution>& executions, const QVector<ExecutionLogEntry>& logs) {
    auto previous = host.queryHandler;
    const RecordedRuns recorded{std::make_shared<QVector<TaskExecution>>(executions), std::make_shared<QVector<ExecutionLogEntry>>(logs)};
    // clang-format off
    host.queryHandler = [previous, recorded](const QString& statement, const QVariantList& bindings) {
        if (statement.contains(QStringLiteral("FROM ai_tasks_executions"))) {
            persistence::DatabaseRows rows;
            const QString wantedTask = statement.contains(QStringLiteral("task_id = ?")) ? bindings.value(0).toString() : QString{};
            QVector<TaskExecution> ordered = *recorded.executions;
            // clang-format off
            if (statement.contains(QStringLiteral("ORDER BY started_at_utc DESC"))) { std::sort(ordered.begin(), ordered.end(), [](const TaskExecution& first, const TaskExecution& second) { return first.startedAtUtc > second.startedAtUtc; }); }
            // clang-format on

            for (const auto& execution : ordered) {
                if (!wantedTask.isEmpty() && execution.taskId != wantedTask) {
                    continue;
                }
                rows.append({{QStringLiteral("id"), execution.id}, {QStringLiteral("task_id"), execution.taskId}, {QStringLiteral("status"), AiTaskRepository::executionStatusName(execution.status)}, {QStringLiteral("started_at_utc"), execution.startedAtUtc.toString(Qt::ISODateWithMs)}, {QStringLiteral("finished_at_utc"), execution.finishedAtUtc.toString(Qt::ISODateWithMs)}, {QStringLiteral("input_tokens"), execution.inputTokens}, {QStringLiteral("output_tokens"), execution.outputTokens}, {QStringLiteral("finish_reason"), execution.finishReason}, {QStringLiteral("error_message"), execution.errorMessage}, {QStringLiteral("content"), execution.content}, {QStringLiteral("stop_reason"), AiTaskRepository::agentStopReasonName(execution.stopReason)}, {QStringLiteral("provider_id"), execution.providerId}, {QStringLiteral("model_id"), execution.modelId}});
            }

            return Result<persistence::DatabaseRows>::success(rows);
        }

        if (statement.contains(QStringLiteral("FROM ai_tasks_logs"))) {
            persistence::DatabaseRows rows;
            const QString wantedExecution = statement.contains(QStringLiteral("execution_id = ?")) ? bindings.value(0).toString() : QString{};
            QVector<ExecutionLogEntry> ordered = *recorded.logs;
            // clang-format off
            if (statement.contains(QStringLiteral("ORDER BY sequence DESC"))) { std::sort(ordered.begin(), ordered.end(), [](const ExecutionLogEntry& first, const ExecutionLogEntry& second) { return first.sequence > second.sequence; }); }
            // clang-format on
            for (const auto& entry : ordered) {
                if (!wantedExecution.isEmpty() && entry.executionId != wantedExecution) {
                    continue;
                }
                rows.append({{QStringLiteral("id"), entry.id}, {QStringLiteral("execution_id"), entry.executionId}, {QStringLiteral("sequence"), entry.sequence}, {QStringLiteral("timestamp_utc"), entry.timestampUtc.toString(Qt::ISODateWithMs)}, {QStringLiteral("level"), AiTaskRepository::executionLogLevelName(entry.level)}, {QStringLiteral("kind"), AiTaskRepository::executionLogKindName(entry.kind)}, {QStringLiteral("detail"), entry.detail}});
            }
            return Result<persistence::DatabaseRows>::success(rows);
        }

        return previous(statement, bindings);
    };
    // clang-format on

    return recorded;
}

inline persistence::DatabaseRows AiTestsHelper::selectedColumns(const QString& statement, const persistence::DatabaseRows& rows) {
    const qsizetype from = statement.indexOf(QStringLiteral("FROM "));
    const QString selection = statement.mid(QStringLiteral("SELECT ").size(), from - QStringLiteral("SELECT ").size());
    QStringList columns;

    for (const auto& entry : selection.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString named = entry.trimmed();
        columns.append(named.contains(QStringLiteral(" AS ")) ? named.section(QStringLiteral(" AS "), -1).trimmed() : named.section(QLatin1Char('.'), -1));
    }

    persistence::DatabaseRows filtered;

    for (const auto& row : rows) {
        QVariantMap kept;
        for (auto value = row.constBegin(); value != row.constEnd(); ++value) {
            if (columns.contains(value.key())) {
                kept.insert(value.key(), value.value());
            }
        }
        filtered.append(kept);
    }

    return filtered;
}

inline AiAgent AiTestsHelper::testAgent() {
    return {QStringLiteral("reviewer"), QStringLiteral("Reviewer"), QStringLiteral("Reviews a repository"), QStringLiteral("You are {{AGENT_NAME}}.\n\n{{SYSTEM_PROMPT_DATA}}"), ModelConnections::connectionKey(AiTestsHelper::testConnection()), 8};
}

inline QJsonObject AiTestsHelper::settingsDocument(const QVector<ModelConnection>& connections, const QString& defaultConnectionKey, const QVector<AiAgent>& agents) {
    QJsonArray declared;

    for (const auto& connection : connections) {
        declared.append(QJsonObject{{QStringLiteral("providerId"), connection.providerId}, {QStringLiteral("modelId"), connection.modelId}, {QStringLiteral("displayName"), connection.displayName}, {QStringLiteral("apiKey"), connection.apiKey}, {QStringLiteral("address"), connection.address}, {QStringLiteral("parameters"), connection.parameters}, {QStringLiteral("extraParameters"), connection.extraParameters}});
    }

    QJsonArray declaredAgents;

    for (const auto& agent : agents) {
        if (connections.isEmpty()) {
            continue;
        }
        declaredAgents.append(QJsonObject{{QStringLiteral("id"), agent.id}, {QStringLiteral("name"), agent.name}, {QStringLiteral("description"), agent.description}, {QStringLiteral("systemPrompt"), agent.systemPrompt}, {QStringLiteral("connectionKey"), agent.connectionKey.isEmpty() ? ModelConnections::connectionKey(connections.first()) : agent.connectionKey}, {QStringLiteral("maximumIterations"), agent.maximumIterations}});
    }

    return {{QStringLiteral("connections"), declared}, {QStringLiteral("defaultConnectionKey"), defaultConnectionKey}, {QStringLiteral("agents"), declaredAgents}};
}

inline void AiTestsHelper::installAiRows(test::TestPluginHost& host, const QVector<AiWorkspace>& workspaces, const QVector<AiTask>& tasks, const QStringList& queued, const QVector<ModelConnection>& connections, const QVector<AiAgent>& agents) {
    // clang-format off
    host.settingsDocument = AiTestsHelper::settingsDocument(connections, connections.isEmpty() ? QString{} : ModelConnections::connectionKey(connections.first()), agents);
    host.queryHandler = [workspaces, tasks, queued](const QString& statement, const QVariantList&) {
        persistence::DatabaseRows rows;
        if (statement.contains(QStringLiteral("FROM ai_tasks_workspaces"))) {
            for (const auto& workspace : workspaces) {
                rows.append({{QStringLiteral("id"), workspace.id}, {QStringLiteral("name"), workspace.name}, {QStringLiteral("position"), workspace.position}, {QStringLiteral("active"), workspace.active ? 1 : 0}, {QStringLiteral("created_at_utc"), workspace.createdAtUtc.toString(Qt::ISODateWithMs)}, {QStringLiteral("updated_at_utc"), workspace.updatedAtUtc.toString(Qt::ISODateWithMs)}});
            }
        }
        if (statement.contains(QStringLiteral("FROM ai_tasks_tasks"))) {
            for (const auto& task : tasks) {
                rows.append({{QStringLiteral("id"), task.id}, {QStringLiteral("workspace_id"), task.workspaceId}, {QStringLiteral("agent_id"), task.agentId}, {QStringLiteral("title"), task.title}, {QStringLiteral("description"), task.description}, {QStringLiteral("prompt"), task.prompt}, {QStringLiteral("issue_url"), task.issueUrl}, {QStringLiteral("execution_kind"), AiTaskRepository::taskExecutionKindName(task.executionKind)}, {QStringLiteral("workdir"), task.workdir}, {QStringLiteral("command"), task.command}, {QStringLiteral("command_timeout_seconds"), task.commandTimeoutSeconds}, {QStringLiteral("column_name"), AiTaskRepository::columnName(task.column)}, {QStringLiteral("position"), task.position}, {QStringLiteral("created_at_utc"), task.createdAtUtc.toString(Qt::ISODateWithMs)}, {QStringLiteral("updated_at_utc"), task.updatedAtUtc.toString(Qt::ISODateWithMs)}, {QStringLiteral("schedule_kind"), task.schedule.has_value() ? AiTaskRepository::scheduleKindName(task.schedule->kind) : QVariant{}}, {QStringLiteral("schedule_enabled"), task.schedule.has_value() ? QVariant(task.schedule->enabled ? 1 : 0) : QVariant{}}, {QStringLiteral("once_at_utc"), task.schedule.has_value() && task.schedule->onceAtUtc.isValid() ? task.schedule->onceAtUtc.toString(Qt::ISODateWithMs) : QVariant{}}, {QStringLiteral("interval_seconds"), task.schedule.has_value() ? QVariant(task.schedule->intervalSeconds) : QVariant{}}, {QStringLiteral("cron_expression"), task.schedule.has_value() ? QVariant(task.schedule->cronExpression) : QVariant{}}, {QStringLiteral("time_zone_id"), task.schedule.has_value() ? QVariant(QString::fromUtf8(task.schedule->timeZoneId)) : QVariant{}}, {QStringLiteral("next_run_at_utc"), task.schedule.has_value() && task.schedule->nextRunAtUtc.isValid() ? task.schedule->nextRunAtUtc.toString(Qt::ISODateWithMs) : QVariant{}}, {QStringLiteral("last_triggered_at_utc"), task.schedule.has_value() && task.schedule->lastTriggeredAtUtc.isValid() ? task.schedule->lastTriggeredAtUtc.toString(Qt::ISODateWithMs) : QVariant{}}});
            }
        }
        if (statement.contains(QStringLiteral("FROM ai_tasks_queue"))) {
            for (const auto& taskId : queued) {
                rows.append({{QStringLiteral("task_id"), taskId}, {QStringLiteral("queued_at_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}});
            }
        }
        return Result<persistence::DatabaseRows>::success(AiTestsHelper::selectedColumns(statement, rows));
    };
    // clang-format on
}

} // namespace workpane::plugins::ai
