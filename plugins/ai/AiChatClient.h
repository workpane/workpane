#pragma once

#include "AiModelConnection.h"
#include "AiRequestGate.h"
#include "AiToolContract.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <functional>

class QNetworkReply;

namespace workpane::plugins::ai {

struct ChatUsage final {
    qint64 inputTokens{0};
    qint64 outputTokens{0};
};

struct ChatRequest final {
    ModelConnection connection;
    QString address;
    QJsonArray messages;
    QVector<ToolSchema> tools;
    // A command line agent runs where the task says, so the request carries that directory.
    QString workdir{};
};

// A request waits either because this application is pacing it or because the service refused it and it is being tried again.
enum class ThrottleReason { RateLimit, Retry };

// The transport is an interface so a deterministic client can replace the network implementation in tests.
class AiChatClient : public QObject {
    Q_OBJECT

  public:
    explicit AiChatClient(QObject* parent = nullptr);

    virtual void send(const ChatRequest& request, const std::function<QString(const QString&)>& translate) = 0;
    virtual void cancel() = 0;
    [[nodiscard]] virtual bool running() const = 0;

  signals:
    void started();
    void requestSent(const QString& endpoint, const QString& body);
    void contentReceived(const QString& delta);
    void finished(const QString& content, const QVector<ToolCall>& calls, ChatUsage usage, const QString& finishReason);
    void failed(const Error& error);
    void throttled(ThrottleReason reason, qint64 milliseconds);
};

class AiHttpChatClient final : public AiChatClient {
    Q_OBJECT

  public:
    explicit AiHttpChatClient(AiRequestGate& gate, QObject* parent = nullptr);
    ~AiHttpChatClient() override;

    void send(const ChatRequest& request, const std::function<QString(const QString&)>& translate) override;
    void cancel() override;
    [[nodiscard]] bool running() const override;

  private:
    void readStream();
    void completeStream();
    void consumeEvent(const QByteArray& payload);
    void reportFailure(const Error& error);
    void release();
    void dispatch();
    void acquireAndDispatch();
    void releaseGate();
    [[nodiscard]] bool retryable(QNetworkReply::NetworkError error, int status) const;
    [[nodiscard]] qint64 retryDelay(const QByteArray& retryAfter) const;

    AiRequestGate& m_gate;
    QTimer m_retryTimer;
    QString m_providerId;
    QNetworkAccessManager m_network;
    ChatRequest m_request;
    std::function<QString(const QString&)> m_translate;
    QTimer m_idleTimer;
    int m_attempt{0};
    int m_maximumRetries{0};
    QPointer<QNetworkReply> m_reply;
    WireProtocol m_protocol{WireProtocol::OpenAiCompatible};
    QByteArray m_buffer;
    QByteArray m_failureBody;
    QString m_content;
    ChatUsage m_usage;
    QString m_finishReason;
    ToolCallAccumulator m_tools{WireProtocol::OpenAiCompatible};
    bool m_completed{false};
};

class ChatRequests final {
  public:
    [[nodiscard]] static QJsonObject buildRequestBody(const ProviderDescriptor& provider, const ChatRequest& request, const std::function<QString(const QString&)>& translate);
    [[nodiscard]] static bool truncatedByOutputBudget(const QString& finishReason);
};

} // namespace workpane::plugins::ai
