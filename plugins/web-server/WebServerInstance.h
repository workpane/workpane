#pragma once

#include "RequestLogModel.h"
#include "StaticFileResolver.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QTcpServer>

#include <atomic>
#include <memory>
#include <optional>

class QTcpSocket;
class QFile;

namespace workpane::plugins::webserver {

class WebServerInstance final : public QObject {
    Q_OBJECT

  public:
    explicit WebServerInstance(QObject* parent = nullptr);

    [[nodiscard]] bool start(const QString& root, const QString& host, quint16 requestedPort);
    void stop();
    [[nodiscard]] bool running() const;
    [[nodiscard]] quint16 port() const;
    [[nodiscard]] const RequestLogModel& requestLog() const;
    void clearRequestLog();

  private slots:
    void acceptConnection();
    void readRequest();
    void releaseSocket();
    void expireConnection();
    void continueFileTransfer();

  private:
    struct FileTransfer final {
        std::shared_ptr<QFile> source;
        qint64 remainingBytes{};
    };

    void respond(QTcpSocket& socket, const QByteArray& request);
    void writeFileChunks(QTcpSocket& socket);

    std::unique_ptr<QTcpServer> m_server;
    QSet<QTcpSocket*> m_sockets;
    QHash<QTcpSocket*, FileTransfer> m_fileTransfers;
    std::optional<StaticFileResolver> m_resolver;
    RequestLogModel m_requestLog;
    std::atomic_bool m_running{false};
    std::atomic_uint16_t m_port{0};
};

} // namespace workpane::plugins::webserver
