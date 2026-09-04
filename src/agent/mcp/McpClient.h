#pragma once

#include "domain/Result.h"

#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <functional>

namespace workpane::agent::mcp {

inline constexpr auto mcpProtocolVersion = "2025-06-18";

enum class McpTransport { Stdio, Http };

struct McpServerDescriptor final {
    QString id;
    McpTransport transport{McpTransport::Stdio};
    QString command;
    QStringList arguments;
    QString workdir;
    QString url;
    QString apiKey;
    QStringList roots;
    bool samplingEnabled{false};
    int samplingMaximumTokens{4096};
    // The budget for a server to answer initialization is given by whoever configures it, because the client belongs to no plugin catalog.
    int startTimeoutMs{30000};
};

struct McpToolDescriptor final {
    QString serverId;
    QString name;
    QString description;
    QJsonObject inputSchema;
    bool readOnly{false};
};

using McpReply = std::function<void(Result<QJsonObject>)>;
using McpSamplingHandler = std::function<void(const QJsonObject& parameters, int maximumTokens, McpReply reply)>;

class McpClient final : public QObject {
    Q_OBJECT

  public:
    explicit McpClient(McpServerDescriptor descriptor, QObject* parent = nullptr);
    ~McpClient() override;

    static void drainTransports();

    void start();
    void stop();
    [[nodiscard]] bool ready() const;
    [[nodiscard]] const QString& serverId() const;
    [[nodiscard]] const QVector<McpToolDescriptor>& tools() const;
    [[nodiscard]] const QJsonObject& serverCapabilities() const;

    void setSamplingHandler(McpSamplingHandler handler);
    void request(const QString& method, const QJsonObject& parameters, const McpReply& reply);
    void callTool(const QString& name, const QJsonObject& arguments, const McpReply& reply);
    void listResources(const QString& cursor, const McpReply& reply);
    void readResource(const QString& uri, const McpReply& reply);
    void listPrompts(const QString& cursor, const McpReply& reply);
    void getPrompt(const QString& name, const QJsonObject& arguments, const McpReply& reply);
    void ping(const McpReply& reply);

  signals:
    void initialized();
    void toolsChanged();
    void failed(const Error& error);
    void progressReported(const QString& token, double progress, double total, const QString& message);

  private:
    void startStdio();
    void startHttp();
    void performInitialize();
    void post(const QJsonObject& message);
    void consumeHttpPayload(const QByteArray& contentType, const QByteArray& payload);
    void deleteSession();
    void cancelPending();
    [[nodiscard]] QJsonObject capabilities() const;
    void dispatch(const QJsonObject& message);
    void handleServerRequest(const QJsonObject& message);
    void handleNotification(const QString& method, const QJsonObject& parameters);
    void notify(const QString& method, const QJsonObject& parameters);
    void respond(const QJsonValue& id, const QJsonObject& result);
    void respondWithError(const QJsonValue& id, int code, const QString& message);
    void send(const QJsonObject& message);
    void refreshTools();
    void releaseTransport();
    void completeAll(const Error& error);
    void reportFailure(const Error& error);

    McpServerDescriptor m_descriptor;
    QObject* m_transport{nullptr};
    bool m_running{false};
    QNetworkAccessManager m_network;
    QByteArray m_sessionId;
    McpSamplingHandler m_samplingHandler;
    QHash<qint64, McpReply> m_pending;
    QVector<McpToolDescriptor> m_tools;
    QJsonObject m_capabilities;
    QTimer m_startTimeout;
    qint64 m_nextId{0};
    bool m_ready{false};
    bool m_stopping{false};
    bool m_answeringPending{false};
};

} // namespace workpane::agent::mcp
