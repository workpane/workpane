#pragma once

#include "agent/mcp/McpClient.h"

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace workpane::agent::mcp {

// A server answers with content of a size it decides, so both transports of one client hold it to the same bound.
inline constexpr qsizetype mcpMaximumMessageBytes = 8 * 1024 * 1024;

// The process of a server, the lines it writes and their JSON live on their own thread, because a server answers with content of any size.
class StdioTransport final : public QObject {
    Q_OBJECT

  public:
    StdioTransport(QString command, QStringList arguments, QString workdir);

  public slots:
    void start();
    void send(const QJsonObject& message);
    void requestTermination();
    void shutdown();

  signals:
    void messageReceived(const QJsonObject& message);
    void failed(const QString& code, const QString& message);
    void exited(int exitCode);

  private:
    void readMessages();
    void refuse(const QString& code, const QString& message);

    QString m_command;
    QStringList m_arguments;
    QString m_workdir;
    QProcess* m_process{nullptr};
    QByteArray m_buffer;
    bool m_stopping{false};
};

} // namespace workpane::agent::mcp
