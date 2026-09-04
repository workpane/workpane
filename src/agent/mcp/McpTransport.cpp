#include "agent/mcp/McpTransport.h"

#include <QDir>
#include <QJsonDocument>
#include <QThread>
#include <QTimer>

#include <utility>

namespace workpane::agent::mcp {

constexpr int transportTerminationGraceMs = 2000;

StdioTransport::StdioTransport(QString command, QStringList arguments, QString workdir) : m_command(std::move(command)), m_arguments(std::move(arguments)), m_workdir(std::move(workdir)) {}

void StdioTransport::start() {
    if (m_process != nullptr) {
        return;
    }

    m_process = new QProcess(this);

    if (!m_workdir.isEmpty() && QDir(m_workdir).exists()) {
        m_process->setWorkingDirectory(m_workdir);
    }
    // clang-format off
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() { readMessages(); });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) { if (!m_stopping) { emit failed(QStringLiteral("ai_mcp_failed"), m_process->errorString()); } });
    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) { if (!m_stopping) { emit exited(exitCode); } });
    // clang-format on
    m_process->start(m_command, m_arguments);
}

void StdioTransport::send(const QJsonObject& message) {
    if (m_process == nullptr || m_process->state() == QProcess::NotRunning) {
        return;
    }

    m_process->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
}

void StdioTransport::requestTermination() {
    m_stopping = true;

    if (m_process == nullptr || m_process->state() == QProcess::NotRunning) {
        return;
    }

    QProcess* process = m_process;
    m_process = nullptr;
    process->disconnect(this);
    process->setParent(nullptr);
    connect(process, &QProcess::finished, process, &QObject::deleteLater);
    process->closeWriteChannel();
    process->terminate();
    // clang-format off
    QTimer::singleShot(transportTerminationGraceMs, process, [process]() { if (process->state() != QProcess::NotRunning) { process->kill(); } });
    // clang-format on
}

void StdioTransport::shutdown() {
    requestTermination();

    if (QThread* owning = thread(); owning != nullptr) {
        owning->quit();
    }
}

// A server that broke the protocol is read no further, so what it keeps writing never grows past the bound that just refused it and never reports the same condition twice.
void StdioTransport::refuse(const QString& code, const QString& message) {
    emit failed(code, message);
    requestTermination();
}

void StdioTransport::readMessages() {
    if (m_process == nullptr) {
        return;
    }

    m_buffer.append(m_process->readAllStandardOutput());

    if (m_buffer.size() > mcpMaximumMessageBytes) {
        refuse(QStringLiteral("ai_mcp_message_too_large"), QStringLiteral("The MCP server message exceeded the permitted size"));
        return;
    }

    qsizetype boundary = m_buffer.indexOf('\n');

    while (boundary >= 0) {
        const QByteArray line = m_buffer.left(boundary).trimmed();
        m_buffer.remove(0, boundary + 1);
        if (!line.isEmpty()) {
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                refuse(QStringLiteral("ai_mcp_message_invalid"), QStringLiteral("The MCP server returned invalid JSON"));
                return;
            }
            emit messageReceived(document.object());
        }
        boundary = m_buffer.indexOf('\n');
    }
}

} // namespace workpane::agent::mcp
