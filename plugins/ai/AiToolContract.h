#pragma once

#include "AiProviderCatalog.h"
#include "domain/Result.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVector>

#include <functional>
#include <optional>

namespace workpane::plugins::ai {

struct ToolSchema final {
    QString name;
    QString descriptionKey;
    QJsonObject parameters;
    // What a reader is told the tool is doing, which is its public name and the one argument that explains the call.
    QString titleKey{};
    QString activityKey{};
    QString activityArgument{};
};

struct ToolPresentation final {
    QString title;
    QString activity;
};

struct ToolCall final {
    QString id;
    QString name;
    QJsonObject arguments;
    // A call whose arguments could not be read carries what arrived with it, so the model is answered instead of the run being ended.
    QString unreadableArguments{};
};

struct ToolResult final {
    QString callId;
    QString text;
    bool failed{false};
    // An image the tool read, carried to the model in the shape each protocol accepts.
    QByteArray imageData{};
    QByteArray imageMediaType{};
};

// What a call got wrong, named so the model can correct it instead of repeating the same call.
struct ToolArgumentError final {
    QString argument;
    // The type the schema declares, empty when the argument is simply absent.
    QString expectedType;
};

struct FittedConversation final {
    QJsonArray messages;
    QJsonArray dropped;
    qsizetype preservedHead{0};
};

// Both protocols stream tool arguments as text fragments, so the accumulator rebuilds each call before it is dispatched.
class ToolCallAccumulator final {
  public:
    explicit ToolCallAccumulator(WireProtocol protocol);

    void consume(const QJsonObject& event);
    [[nodiscard]] Result<QVector<ToolCall>> calls() const;
    [[nodiscard]] bool empty() const;
    void clear();

  private:
    struct PendingCall final {
        QString id;
        QString name;
        QString arguments;
    };

    WireProtocol m_protocol;
    QMap<int, PendingCall> m_pending;
};

class ToolContracts final {
  public:
    [[nodiscard]] static Result<void> validateToolSchema(const ToolSchema& schema);
    [[nodiscard]] static std::optional<ToolArgumentError> findToolArgumentError(const ToolSchema& schema, const QJsonObject& arguments);
    [[nodiscard]] static qint64 estimateTokens(const QJsonArray& messages);
    // A window nobody declares bounds nothing, which is not the same as a window the reservation leaves no room in.
    [[nodiscard]] static std::optional<qint64> fittingTokenLimit(int contextWindow, qint64 reservedTokens);
    [[nodiscard]] static qsizetype pruneToolResults(QJsonArray& messages, std::optional<qint64> limit);
    // Old turns are dropped whole, because an assistant turn carrying tool calls is invalid without the results that answer it.
    [[nodiscard]] static FittedConversation fitConversation(const QJsonArray& messages, std::optional<qint64> limit);
    [[nodiscard]] static QJsonArray serializeTools(WireProtocol protocol, const QVector<ToolSchema>& tools, const std::function<QString(const QString&)>& translate);
    [[nodiscard]] static QJsonObject serializeAssistantTurn(WireProtocol protocol, const QString& content, const QVector<ToolCall>& calls);
    [[nodiscard]] static bool protocolRequiresAlternatingRoles(WireProtocol protocol);
    // Each protocol declares what a conversation has to look like before it is accepted, so the projection ends by satisfying it.
    [[nodiscard]] static QJsonArray enforceProtocolShape(WireProtocol protocol, const QJsonArray& messages);
    [[nodiscard]] static QVector<QJsonObject> serializeToolResults(WireProtocol protocol, const QVector<ToolResult>& results);
};

} // namespace workpane::plugins::ai
