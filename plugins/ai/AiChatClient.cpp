#include "AiChatClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <functional>

namespace workpane::plugins::ai {

constexpr int maximumFieldDepth = 16;

constexpr qsizetype maximumStreamBufferBytes = 1 << 20;
constexpr qsizetype maximumContentCharacters = 1 << 22;
constexpr qsizetype maximumFailureBodyBytes = 1 << 16;
constexpr qsizetype maximumReportedReasonCharacters = 500;

class AiChatClientHelper final {
  public:
    static void insertAtPath(QJsonObject& target, const QStringList& segments, const QJsonValue& value);
    static void removeAtPath(QJsonObject& target, const QStringList& segments);
    static void applyField(QJsonObject& body, const QString& field, const QJsonValue& value);
    static QString providerErrorMessage(const QByteArray& body);
    static QJsonArray liftInstructions(const QJsonArray& messages, QString& instructions);
};

void AiChatClientHelper::insertAtPath(QJsonObject& target, const QStringList& segments, const QJsonValue& value) {
    if (segments.size() == 1) {
        target.insert(segments.first(), value);
        return;
    }

    QJsonObject nested = target.value(segments.first()).toObject();
    insertAtPath(nested, segments.mid(1), value);
    target.insert(segments.first(), nested);
}

// A field the provider must not receive leaves no empty parent behind it.
void AiChatClientHelper::removeAtPath(QJsonObject& target, const QStringList& segments) {
    if (segments.size() == 1) {
        target.remove(segments.first());
        return;
    }

    if (!target.value(segments.first()).isObject()) {
        return;
    }

    QJsonObject nested = target.value(segments.first()).toObject();
    removeAtPath(nested, segments.mid(1));

    if (nested.isEmpty()) {
        target.remove(segments.first());
        return;
    }

    target.insert(segments.first(), nested);
}

void AiChatClientHelper::applyField(QJsonObject& body, const QString& field, const QJsonValue& value) {
    const QStringList segments = field.split(QLatin1Char('.'));

    // A wire field names a handful of levels, so a path deeper than the bound reaches nothing rather than one frame per segment.
    if (segments.size() > maximumFieldDepth) {
        return;
    }

    if (value.isNull()) {
        removeAtPath(body, segments);
        return;
    }

    insertAtPath(body, segments, value);
}

// Services report a rejection in more than one shape, and one that reports it in none is quoted so the reason is still readable.
QString AiChatClientHelper::providerErrorMessage(const QByteArray& body) {
    const QJsonDocument document = QJsonDocument::fromJson(body);

    if (!document.isObject()) {
        const QString text = QString::fromUtf8(body).simplified();
        return text.left(maximumReportedReasonCharacters);
    }

    const QJsonObject reported = document.object();
    const QJsonObject nested = reported.value(QStringLiteral("error")).toObject();
    const QString type = nested.value(QStringLiteral("type")).toString(reported.value(QStringLiteral("type")).toString());
    QString message = nested.value(QStringLiteral("message")).toString();

    if (message.isEmpty()) {
        message = reported.value(QStringLiteral("error")).toString();
    }

    if (message.isEmpty()) {
        message = reported.value(QStringLiteral("message")).toString();
    }

    if (message.isEmpty()) {
        message = reported.value(QStringLiteral("detail")).toString();
    }

    if (message.isEmpty()) {
        return QString::fromUtf8(body).simplified().left(maximumReportedReasonCharacters);
    }

    return type.isEmpty() ? message : QStringLiteral("%1: %2").arg(type, message);
}

// The Anthropic API carries the instructions in its own field instead of a message with a system role.
QJsonArray AiChatClientHelper::liftInstructions(const QJsonArray& messages, QString& instructions) {
    QJsonArray conversation;

    for (const auto& value : messages) {
        const QJsonObject message = value.toObject();
        if (message.value(QStringLiteral("role")).toString() == QStringLiteral("system")) {
            instructions = message.value(QStringLiteral("content")).toString();
            continue;
        }
        conversation.append(message);
    }

    return conversation;
}

// A model that ran out of output budget stopped in the middle of what it was saying, and each protocol names that reason its own way.
bool ChatRequests::truncatedByOutputBudget(const QString& finishReason) {
    return finishReason == QStringLiteral("length") || finishReason == QStringLiteral("max_tokens");
}

// The body carries the shape of the protocol, the declared parameters at the fields the catalog names and whatever the user added beyond them.
QJsonObject ChatRequests::buildRequestBody(const ProviderDescriptor& provider, const ChatRequest& request, const std::function<QString(const QString&)>& translate) {
    const ModelConnection& connection = request.connection;
    QString instructions;
    const bool anthropic = provider.protocol == WireProtocol::Anthropic;
    const QJsonArray conversation = anthropic ? AiChatClientHelper::liftInstructions(request.messages, instructions) : request.messages;

    QJsonObject body;
    body.insert(QStringLiteral("model"), connection.modelId);
    body.insert(QStringLiteral("messages"), conversation);
    body.insert(QStringLiteral("stream"), true);

    if (anthropic) {
        if (!instructions.isEmpty()) {
            body.insert(QStringLiteral("system"), instructions);
        }
    } else {
        body.insert(QStringLiteral("stream_options"), QJsonObject{{QStringLiteral("include_usage"), true}});
    }

    for (const auto& parameter : ProviderCatalog::applicableParameters(provider, connection.modelId)) {
        if (!connection.parameters.contains(parameter.id)) {
            continue;
        }
        // A zero asks for everything the model allows, so the maximum that model declares is what reaches the service, which some of them require.
        const QJsonValue declared = connection.parameters.value(parameter.id);
        if (parameter.modelMaximumWhenZero && declared.toInteger(0) == 0) {
            const qint64 maximum = ModelConnections::outputBudget(connection);
            if (maximum > 0) {
                AiChatClientHelper::applyField(body, parameter.field, QJsonValue(maximum));
            }
            continue;
        }
        AiChatClientHelper::applyField(body, parameter.field, declared);
    }

    for (auto extra = connection.extraParameters.constBegin(); extra != connection.extraParameters.constEnd(); ++extra) {
        AiChatClientHelper::applyField(body, extra.key(), extra.value());
    }

    if (!request.tools.isEmpty()) {
        body.insert(QStringLiteral("tools"), ToolContracts::serializeTools(provider.protocol, request.tools, translate));
    }

    return body;
}

AiChatClient::AiChatClient(QObject* parent) : QObject(parent) {}

AiHttpChatClient::AiHttpChatClient(AiRequestGate& gate, QObject* parent) : AiChatClient(parent), m_gate(gate) {
    m_idleTimer.setSingleShot(true);
    m_retryTimer.setSingleShot(true);
    // clang-format off
    connect(&m_retryTimer, &QTimer::timeout, this, [this]() { acquireAndDispatch(); });
    // clang-format on
    // clang-format off
    connect(&m_idleTimer, &QTimer::timeout, this, [this]() { if (m_reply != nullptr) { QNetworkReply* stalled = m_reply; release(); stalled->abort(); reportFailure({"ai_stream_idle", "The provider stream stopped sending data", {}}); } });
    // clang-format on
}

AiHttpChatClient::~AiHttpChatClient() {
    releaseGate();

    if (m_reply != nullptr) {
        m_reply->disconnect(this);
        m_reply->abort();
    }
}

// The provider counts a request that is in flight, so the admission is held until this client stops using it.
// A place is given back the moment this client stops wanting it, whether it was waiting for that place or already holding it.
void AiHttpChatClient::releaseGate() {
    m_gate.withdraw(m_providerId, this);
    m_gate.release(m_providerId, this);
}

void AiHttpChatClient::acquireAndDispatch() {
    // clang-format off
    const auto admitted = [this]() { dispatch(); };
    // clang-format on
    const qint64 wait = m_gate.acquire(m_providerId, this, admitted);

    if (wait != 0) {
        emit throttled(ThrottleReason::RateLimit, std::max<qint64>(0, wait));
    }
}

bool AiHttpChatClient::running() const {
    return m_reply != nullptr;
}

void AiHttpChatClient::send(const ChatRequest& request, const std::function<QString(const QString&)>& translate) {
    if (m_reply != nullptr) {
        reportFailure({"ai_execution_busy", "The client is already running a request", {}});
        return;
    }

    const auto validated = ModelConnections::validateConnection(request.connection);

    if (!validated.hasValue()) {
        reportFailure(validated.error());
        return;
    }

    const ProviderDescriptor* provider = ProviderCatalog::findProvider(validated.value().providerId);
    const auto apiKey = Secrets::resolveSecret(validated.value().apiKey);

    if (!apiKey.hasValue()) {
        reportFailure(apiKey.error());
        return;
    }

    if (provider->requiresApiKey && apiKey.value().isEmpty()) {
        reportFailure({"ai_api_key_missing", "The provider requires an API key", provider->id});
        return;
    }

    m_completed = false;
    m_protocol = provider->protocol;
    m_request = request;
    m_request.connection = validated.value();
    m_translate = translate;
    m_attempt = 0;
    m_maximumRetries = provider->requestMaxRetries;
    m_providerId = provider->id;
    acquireAndDispatch();
}

void AiHttpChatClient::dispatch() {
    const ProviderDescriptor* provider = ProviderCatalog::findProvider(m_request.connection.providerId);
    const auto apiKey = Secrets::resolveSecret(m_request.connection.apiKey);

    if (provider == nullptr || !apiKey.hasValue()) {
        reportFailure(apiKey.hasValue() ? Error{"ai_provider_unknown", "The selected AI provider is not supported", m_request.connection.providerId} : apiKey.error());
        return;
    }

    m_buffer.clear();
    m_failureBody.clear();
    m_content.clear();
    m_usage = {};
    m_finishReason.clear();
    m_tools = ToolCallAccumulator(m_protocol);

    const auto chat = ModelConnections::resolveEndpoint(m_request.connection.providerId, m_request.address, ModelEndpoint::Chat);

    if (!chat.has_value()) {
        emit failed({"ai_provider_unknown", m_translate(QStringLiteral("ai.error.provider-unknown")), m_request.connection.providerId});
        return;
    }

    QUrl endpoint(chat.value().url);

    if (!provider->queryParameters.isEmpty()) {
        QUrlQuery query(endpoint);
        for (auto parameter = provider->queryParameters.constBegin(); parameter != provider->queryParameters.constEnd(); ++parameter) {
            query.addQueryItem(parameter.key(), parameter.value());
        }
        endpoint.setQuery(query);
    }

    QNetworkRequest httpRequest(endpoint);
    httpRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    httpRequest.setRawHeader(QByteArrayLiteral("accept"), QByteArrayLiteral("text/event-stream"));
    httpRequest.setTransferTimeout(ProviderCatalog::aiLimits().requestTimeoutMs);

    for (auto header = provider->httpHeaders.constBegin(); header != provider->httpHeaders.constEnd(); ++header) {
        httpRequest.setRawHeader(header.key().toUtf8(), header.value().toUtf8());
    }

    if (m_protocol == WireProtocol::Anthropic) {
        httpRequest.setRawHeader(QByteArrayLiteral("x-api-key"), apiKey.value().toUtf8());
    } else if (!apiKey.value().isEmpty()) {
        httpRequest.setRawHeader(QByteArrayLiteral("authorization"), QByteArrayLiteral("Bearer ") + apiKey.value().toUtf8());
    }

    const QJsonObject body = ChatRequests::buildRequestBody(*provider, m_request, m_translate);
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Indented);
    m_reply = m_network.post(httpRequest, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, &AiHttpChatClient::readStream);
    connect(m_reply, &QNetworkReply::finished, this, &AiHttpChatClient::completeStream);

    if (provider->streamIdleTimeoutMs > 0) {
        m_idleTimer.start(provider->streamIdleTimeoutMs);
    }

    emit started();

    // The credential travels in a header and is never recorded with the request.
    emit requestSent(endpoint.toString(), QString::fromUtf8(payload));
}

void AiHttpChatClient::cancel() {
    m_idleTimer.stop();
    m_retryTimer.stop();
    releaseGate();

    if (m_reply == nullptr) {
        return;
    }

    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
    m_completed = true;
}

void AiHttpChatClient::readStream() {
    if (m_reply == nullptr) {
        return;
    }

    m_idleTimer.start();
    const QByteArray chunk = m_reply->readAll();
    m_buffer.append(chunk);

    // A rejected request answers with a body instead of a stream, so it is kept apart from the line parser that consumes the buffer.
    if (m_failureBody.size() < maximumFailureBodyBytes) {
        m_failureBody.append(chunk.left(maximumFailureBodyBytes - m_failureBody.size()));
    }

    if (m_buffer.size() > maximumStreamBufferBytes) {
        reportFailure({"ai_stream_too_large", "The provider stream exceeded the permitted size", {}});
        cancel();
        return;
    }

    qsizetype boundary = m_buffer.indexOf('\n');

    while (boundary >= 0) {
        const QByteArray line = m_buffer.left(boundary).trimmed();
        m_buffer.remove(0, boundary + 1);
        if (line.startsWith(QByteArrayLiteral("data:"))) {
            consumeEvent(line.mid(5).trimmed());
        }
        boundary = m_buffer.indexOf('\n');
    }
}

void AiHttpChatClient::consumeEvent(const QByteArray& payload) {
    if (payload.isEmpty() || payload == QByteArrayLiteral("[DONE]")) {
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);

    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        reportFailure({"ai_stream_invalid", "The provider returned an invalid stream event", {}});
        cancel();
        return;
    }

    const QJsonObject event = document.object();
    m_tools.consume(event);
    QString delta;

    if (m_protocol == WireProtocol::Anthropic) {
        const QString type = event.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("content_block_delta")) {
            delta = event.value(QStringLiteral("delta")).toObject().value(QStringLiteral("text")).toString();
        } else if (type == QStringLiteral("message_start")) {
            m_usage.inputTokens = event.value(QStringLiteral("message")).toObject().value(QStringLiteral("usage")).toObject().value(QStringLiteral("input_tokens")).toInteger(0);
        } else if (type == QStringLiteral("message_delta")) {
            m_usage.outputTokens = event.value(QStringLiteral("usage")).toObject().value(QStringLiteral("output_tokens")).toInteger(m_usage.outputTokens);
            m_finishReason = event.value(QStringLiteral("delta")).toObject().value(QStringLiteral("stop_reason")).toString(m_finishReason);
        } else if (type == QStringLiteral("error")) {
            reportFailure({"ai_provider_error", event.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString(QStringLiteral("The provider reported an error")), {}});
            cancel();
            return;
        }
    } else {
        const QJsonArray choices = event.value(QStringLiteral("choices")).toArray();
        if (!choices.isEmpty()) {
            const QJsonObject choice = choices.first().toObject();
            delta = choice.value(QStringLiteral("delta")).toObject().value(QStringLiteral("content")).toString();
            m_finishReason = choice.value(QStringLiteral("finish_reason")).toString(m_finishReason);
        }
        if (event.value(QStringLiteral("usage")).isObject()) {
            const QJsonObject usage = event.value(QStringLiteral("usage")).toObject();
            m_usage.inputTokens = usage.value(QStringLiteral("prompt_tokens")).toInteger(m_usage.inputTokens);
            m_usage.outputTokens = usage.value(QStringLiteral("completion_tokens")).toInteger(m_usage.outputTokens);
        }
        if (event.value(QStringLiteral("error")).isObject()) {
            reportFailure({"ai_provider_error", event.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString(QStringLiteral("The provider reported an error")), {}});
            cancel();
            return;
        }
    }

    if (delta.isEmpty()) {
        return;
    }

    if (m_content.size() + delta.size() > maximumContentCharacters) {
        reportFailure({"ai_stream_too_large", "The provider stream exceeded the permitted size", {}});
        cancel();
        return;
    }

    m_content.append(delta);
    emit contentReceived(delta);
}

void AiHttpChatClient::completeStream() {
    if (m_reply == nullptr) {
        return;
    }

    QNetworkReply* reply = m_reply;
    const QNetworkReply::NetworkError networkError = reply->error();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray remainder = reply->readAll();
    const QByteArray retryAfter = reply->rawHeader("Retry-After");
    const QString networkMessage = reply->errorString();
    release();
    releaseGate();

    if (!remainder.isEmpty()) {
        m_buffer.append(remainder);
        m_failureBody.append(remainder.left(std::max<qsizetype>(0, maximumFailureBodyBytes - m_failureBody.size())));
    }

    if (networkError != QNetworkReply::NoError || status < 200 || status >= 300) {
        if (m_attempt < m_maximumRetries && retryable(networkError, status)) {
            // A rejection that repeats immediately reproduces the condition that caused it, so the next attempt waits.
            const qint64 delay = retryDelay(retryAfter);
            ++m_attempt;
            emit throttled(ThrottleReason::Retry, delay);
            m_retryTimer.start(static_cast<int>(delay));
            return;
        }
        // What the service answered is kept verbatim beside the status, because that body is what explains the rejection.
        const QString reported = AiChatClientHelper::providerErrorMessage(m_failureBody);
        const QString statusLine = status > 0 ? QStringLiteral("HTTP %1").arg(QString::number(status)) : networkMessage;
        const QString body = QString::fromUtf8(m_failureBody);
        reportFailure({"ai_request_failed", reported.isEmpty() ? networkMessage : reported, body.isEmpty() ? statusLine : QStringLiteral("%1\n%2").arg(statusLine, body)});
        return;
    }

    if (m_completed) {
        return;
    }

    const auto calls = m_tools.calls();

    if (!calls.hasValue()) {
        // A stream the provider cut at the output budget leaves its tool call half written, so what failed is the budget and not the parsing.
        reportFailure(ChatRequests::truncatedByOutputBudget(m_finishReason) ? Error{"ai_output_truncated", calls.error().message, m_finishReason} : calls.error());
        return;
    }

    m_completed = true;
    emit finished(m_content, calls.value(), m_usage, m_finishReason);
}

// A service that says how long to wait is obeyed, and otherwise the wait doubles with every attempt up to the declared ceiling.
qint64 AiHttpChatClient::retryDelay(const QByteArray& retryAfter) const {
    const qint64 ceiling = ProviderCatalog::aiLimits().maximumRetryBackoffMs;
    bool seconds = false;
    const qint64 requested = retryAfter.trimmed().toLongLong(&seconds);

    if (seconds && requested >= 0) {
        return std::min(ceiling, requested * 1000);
    }

    const QDateTime until = QDateTime::fromString(QString::fromLatin1(retryAfter.trimmed()), Qt::RFC2822Date);

    if (until.isValid()) {
        return std::clamp<qint64>(QDateTime::currentDateTimeUtc().msecsTo(until.toUTC()), 0, ceiling);
    }

    return std::min(ceiling, static_cast<qint64>(ProviderCatalog::aiLimits().retryBackoffMs) << m_attempt);
}

// Only a transient transport or server condition is retried, because a rejected request repeats the same rejection.
bool AiHttpChatClient::retryable(QNetworkReply::NetworkError error, int status) const {
    if (status == 408 || status == 429 || status >= 500) {
        return true;
    }
    if (status > 0) {
        return false;
    }

    return error == QNetworkReply::TimeoutError || error == QNetworkReply::TemporaryNetworkFailureError || error == QNetworkReply::RemoteHostClosedError || error == QNetworkReply::ConnectionRefusedError;
}

void AiHttpChatClient::release() {
    m_idleTimer.stop();

    if (m_reply == nullptr) {
        return;
    }

    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    reply->disconnect(this);
    reply->deleteLater();
}

void AiHttpChatClient::reportFailure(const Error& error) {
    releaseGate();

    if (m_completed) {
        return;
    }

    m_completed = true;
    emit failed(error);
}

} // namespace workpane::plugins::ai
