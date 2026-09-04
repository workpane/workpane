#include "WebServerInstance.h"

#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

namespace workpane::plugins::webserver {

constexpr qsizetype maximumConnections = 128;
constexpr qint64 maximumBufferedFileBytes = 256LL * 1024;
constexpr qint64 fileChunkSize = 64LL * 1024;
constexpr int requestTimeoutMilliseconds = 10'000;
constexpr qint64 maximumRequestBytes = 32LL * 1024;

WebServerInstance::WebServerInstance(QObject* parent) : QObject(parent) {}

bool WebServerInstance::start(const QString& root, const QString& host, quint16 requestedPort) {
    stop();
    m_server = std::make_unique<QTcpServer>();
    m_server->setMaxPendingConnections(maximumConnections);
    connect(m_server.get(), &QTcpServer::newConnection, this, &WebServerInstance::acceptConnection);

    m_resolver.emplace(root);
    QHostAddress address;

    if (!m_resolver->valid() || !address.setAddress(host) || !m_server->listen(address, requestedPort)) {
        m_server.reset();
        m_resolver.reset();
        return false;
    }

    m_port.store(m_server->serverPort(), std::memory_order_release);
    m_running.store(true, std::memory_order_release);
    return true;
}

void WebServerInstance::stop() {
    m_running.store(false, std::memory_order_release);
    m_port.store(0, std::memory_order_release);

    if (m_server != nullptr) {
        m_server->close();
    }

    for (auto* socket : std::as_const(m_sockets)) {
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }

    m_fileTransfers.clear();
    m_sockets.clear();
    m_server.reset();
    m_resolver.reset();
}

bool WebServerInstance::running() const {
    return m_running.load(std::memory_order_acquire);
}

quint16 WebServerInstance::port() const {
    return m_port.load(std::memory_order_acquire);
}

const RequestLogModel& WebServerInstance::requestLog() const {
    return m_requestLog;
}

void WebServerInstance::clearRequestLog() {
    m_requestLog.clear();
}

void WebServerInstance::acceptConnection() {
    while (m_server->hasPendingConnections()) {
        QTcpSocket* socket = m_server->nextPendingConnection();
        if (socket == nullptr) {
            continue;
        }
        if (m_sockets.size() >= maximumConnections) {
            socket->abort();
            socket->deleteLater();
            continue;
        }

        m_sockets.insert(socket);
        // The size of what a client sends is decided by that client, so the socket never buffers more than one request may occupy.
        socket->setReadBufferSize(maximumRequestBytes);
        auto* deadline = new QTimer(socket);
        deadline->setObjectName(QStringLiteral("requestDeadline"));
        deadline->setSingleShot(true);
        deadline->setInterval(requestTimeoutMilliseconds);
        deadline->start();
        connect(socket, &QTcpSocket::readyRead, this, &WebServerInstance::readRequest);
        connect(socket, &QTcpSocket::disconnected, this, &WebServerInstance::releaseSocket);
        connect(deadline, &QTimer::timeout, this, &WebServerInstance::expireConnection);
    }
}

void WebServerInstance::releaseSocket() {
    auto* socket = qobject_cast<QTcpSocket*>(sender());

    if (socket == nullptr) {
        return;
    }

    m_sockets.remove(socket);
    m_fileTransfers.remove(socket);
    socket->deleteLater();
}

void WebServerInstance::readRequest() {
    auto* socket = qobject_cast<QTcpSocket*>(sender());

    if (socket == nullptr) {
        return;
    }

    QByteArray request = socket->property("requestBuffer").toByteArray();
    request.append(socket->readAll());

    if (request.size() > maximumRequestBytes) {
        respond(*socket, {});
        return;
    }

    if (!request.contains("\r\n\r\n")) {
        socket->setProperty("requestBuffer", request);
        return;
    }

    respond(*socket, request);
}

void WebServerInstance::expireConnection() {
    const auto* deadline = qobject_cast<QTimer*>(sender());
    auto* socket = deadline == nullptr ? nullptr : qobject_cast<QTcpSocket*>(deadline->parent());

    if (socket != nullptr) {
        socket->abort();
    }
}

void WebServerInstance::respond(QTcpSocket& socket, const QByteArray& request) {
    QElapsedTimer timer;
    timer.start();
    const QList<QByteArray> requestLine = request.left(request.indexOf("\r\n")).split(' ');
    const QByteArray method = requestLine.isEmpty() || requestLine.first().isEmpty() ? QByteArrayLiteral("INVALID") : requestLine.first();
    const QByteArray requestTarget = requestLine.size() == 3 ? requestLine.at(1) : QByteArray{};
    const QByteArray protocol = requestLine.size() == 3 ? requestLine.at(2) : QByteArray{};
    const bool validRequest = method == QByteArrayLiteral("GET") && requestTarget.startsWith('/') && (protocol == QByteArrayLiteral("HTTP/1.0") || protocol == QByteArrayLiteral("HTTP/1.1"));
    const QByteArray path = validRequest ? requestTarget.split('?').first() : QByteArray{};
    const auto file = validRequest && m_resolver.has_value() ? m_resolver->resolve(path) : std::nullopt;

    std::shared_ptr<QFile> source;
    QByteArray fileMimeType;
    qint64 fileSize = 0;

    if (file.has_value()) {
        source = std::make_shared<QFile>(file->canonicalPath);
        if (!source->open(QIODevice::ReadOnly) || source->size() != file->size) {
            source.reset();
        }
        fileMimeType = file->mimeType;
        fileSize = file->size;
    }

    const bool fileAvailable = source != nullptr;
    const int status = fileAvailable ? 200 : validRequest ? 404 : 400;
    const QByteArray statusText = status == 200 ? QByteArrayLiteral("OK") : status == 404 ? QByteArrayLiteral("Not Found") : QByteArrayLiteral("Bad Request");
    const QByteArray body = fileAvailable ? QByteArray{} : statusText;
    const QByteArray mime = fileAvailable ? fileMimeType : QByteArrayLiteral("text/plain");
    const qint64 contentLength = fileAvailable ? fileSize : body.size();
    QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " " + statusText + "\r\n";
    response += "Content-Type: " + mime + "\r\n";
    response += "Content-Length: " + QByteArray::number(contentLength) + "\r\n";
    response += "Cache-Control: no-store\r\nConnection: close\r\n\r\n";
    response += body;
    disconnect(&socket, &QTcpSocket::readyRead, this, &WebServerInstance::readRequest);

    if (auto* deadline = socket.findChild<QTimer*>(QStringLiteral("requestDeadline")); deadline != nullptr) {
        deadline->stop();
    }

    if (socket.write(response) < 0) {
        socket.abort();
        return;
    }

    if (fileAvailable) {
        m_fileTransfers.insert(&socket, {std::move(source), contentLength});
        connect(&socket, &QTcpSocket::bytesWritten, this, &WebServerInstance::continueFileTransfer);
        writeFileChunks(socket);
    } else {
        socket.disconnectFromHost();
    }

    m_requestLog.append({0, QDateTime::currentDateTimeUtc(), QString::fromLatin1(method), QString::fromUtf8(path), status, timer.elapsed(), contentLength, socket.peerAddress().toString()});
}

void WebServerInstance::continueFileTransfer() {
    auto* socket = qobject_cast<QTcpSocket*>(sender());

    if (socket != nullptr) {
        writeFileChunks(*socket);
    }
}

void WebServerInstance::writeFileChunks(QTcpSocket& socket) {
    auto transfer = m_fileTransfers.find(&socket);

    if (transfer == m_fileTransfers.end()) {
        return;
    }

    auto& state = transfer.value();

    while (socket.bytesToWrite() < maximumBufferedFileBytes && state.remainingBytes > 0) {
        const QByteArray chunk = state.source->read(std::min(fileChunkSize, state.remainingBytes));
        if (chunk.isEmpty()) {
            socket.abort();
            m_fileTransfers.remove(&socket);
            return;
        }
        if (socket.write(chunk) < 0) {
            socket.abort();
            m_fileTransfers.remove(&socket);
            return;
        }
        state.remainingBytes -= chunk.size();
    }

    if (state.remainingBytes == 0 && socket.bytesToWrite() == 0) {
        m_fileTransfers.remove(&socket);
        socket.disconnectFromHost();
    }
}

} // namespace workpane::plugins::webserver
