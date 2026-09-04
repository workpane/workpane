#include "AiToolContract.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>
#include <functional>
#include <optional>
#include <utility>

namespace workpane::plugins::ai {

constexpr int maximumBlockDepth = 64;

constexpr qsizetype maximumReportedArgumentCharacters = 2000;

class AiToolContractHelper final {
  public:
    static QJsonValue joinedContent(const QJsonValue& first, const QJsonValue& second);
    static const QRegularExpression& toolNamePattern();
    static QJsonObject openAiToolCall(const ToolCall& call);
    static QJsonObject anthropicToolUse(const ToolCall& call);
    static qint64 messageTokens(const QJsonObject& message);
    static bool answersToolCalls(const QJsonObject& message);
    static QString prunedText(const QString& text);
    static QJsonObject prunedBlock(const QJsonObject& block, bool& changed, int depth);
    static QJsonObject prunedToolResult(const QJsonObject& message, bool& changed);
    static bool matchesDeclaredType(const QString& declared, const QJsonValue& value);
};

const QRegularExpression& AiToolContractHelper::toolNamePattern() {
    static const QRegularExpression pattern(QStringLiteral("^[a-z][a-z0-9_]{0,63}$"));
    return pattern;
}

QJsonObject AiToolContractHelper::openAiToolCall(const ToolCall& call) {
    const QJsonObject function{{QStringLiteral("name"), call.name}, {QStringLiteral("arguments"), QString::fromUtf8(QJsonDocument(call.arguments).toJson(QJsonDocument::Compact))}};
    return {{QStringLiteral("id"), call.id}, {QStringLiteral("type"), QStringLiteral("function")}, {QStringLiteral("function"), function}};
}

QJsonObject AiToolContractHelper::anthropicToolUse(const ToolCall& call) {
    return {{QStringLiteral("type"), QStringLiteral("tool_use")}, {QStringLiteral("id"), call.id}, {QStringLiteral("name"), call.name}, {QStringLiteral("input"), call.arguments}};
}

constexpr double contextSafetyMargin = 0.7;
constexpr qsizetype prunedToolResultThreshold = 8192;
constexpr qsizetype prunedToolResultHead = 4096;
constexpr qsizetype prunedToolResultTail = 1024;
constexpr qint64 charactersPerToken = 4;

qint64 AiToolContractHelper::messageTokens(const QJsonObject& message) {
    return QJsonDocument(message).toJson(QJsonDocument::Compact).size() / charactersPerToken;
}

bool AiToolContractHelper::answersToolCalls(const QJsonObject& message) {
    const QString role = message.value(QStringLiteral("role")).toString();

    if (role == QStringLiteral("tool")) {
        return true;
    }
    if (role != QStringLiteral("user")) {
        return false;
    }

    const QJsonArray blocks = message.value(QStringLiteral("content")).toArray();
    return !blocks.isEmpty() && blocks.first().toObject().value(QStringLiteral("type")).toString() == QStringLiteral("tool_result");
}

qint64 ToolContracts::estimateTokens(const QJsonArray& messages) {
    qint64 total = 0;

    for (const auto& value : messages) {
        total += AiToolContractHelper::messageTokens(value.toObject());
    }

    return total;
}

// The window has to hold the answer and the tool declarations as well as the conversation, so the budget is what is left of it.
std::optional<qint64> ToolContracts::fittingTokenLimit(int contextWindow, qint64 reservedTokens) {
    if (contextWindow <= 0) {
        return std::nullopt;
    }

    // A reservation that consumes the whole window leaves nothing to fit into, which is a limit of zero rather than the absence of one.
    const qint64 available = static_cast<qint64>(contextWindow) - std::max<qint64>(reservedTokens, 0);
    return available <= 0 ? 0 : static_cast<qint64>(static_cast<double>(available) * contextSafetyMargin);
}

QString AiToolContractHelper::prunedText(const QString& text) {
    if (text.size() <= prunedToolResultThreshold) {
        return text;
    }

    return text.left(prunedToolResultHead) + QStringLiteral("\n[... %1 characters pruned ...]\n").arg(QString::number(text.size() - prunedToolResultHead - prunedToolResultTail)) + text.right(prunedToolResultTail);
}

// A result that carries an image holds its text in its own blocks, so only text is shortened and everything else is left exactly as it was.
QJsonObject AiToolContractHelper::prunedBlock(const QJsonObject& block, bool& changed, int depth) {
    // A block nests as deep as whoever wrote it says, so the depth it may reach is ours to declare rather than theirs.
    if (depth >= maximumBlockDepth) {
        return block;
    }

    QJsonObject shortened = block;

    if (block.value(QStringLiteral("content")).isString()) {
        const QString text = block.value(QStringLiteral("content")).toString();
        const QString reduced = prunedText(text);
        changed = changed || reduced.size() != text.size();
        shortened.insert(QStringLiteral("content"), reduced);
        return shortened;
    }

    if (block.value(QStringLiteral("text")).isString()) {
        const QString text = block.value(QStringLiteral("text")).toString();
        const QString reduced = prunedText(text);
        changed = changed || reduced.size() != text.size();
        shortened.insert(QStringLiteral("text"), reduced);
        return shortened;
    }

    if (block.value(QStringLiteral("content")).isArray()) {
        QJsonArray carried;
        for (const auto& value : block.value(QStringLiteral("content")).toArray()) {
            carried.append(prunedBlock(value.toObject(), changed, depth + 1));
        }
        shortened.insert(QStringLiteral("content"), carried);
    }

    return shortened;
}

QJsonObject AiToolContractHelper::prunedToolResult(const QJsonObject& message, bool& changed) {
    QJsonObject pruned = message;

    if (message.value(QStringLiteral("content")).isString()) {
        const QString text = message.value(QStringLiteral("content")).toString();
        const QString shortened = prunedText(text);
        changed = changed || shortened.size() != text.size();
        pruned.insert(QStringLiteral("content"), shortened);
        return pruned;
    }

    QJsonArray blocks;

    for (const auto& value : message.value(QStringLiteral("content")).toArray()) {
        blocks.append(prunedBlock(value.toObject(), changed, 0));
    }

    pruned.insert(QStringLiteral("content"), blocks);
    return pruned;
}

// What a tool returned long ago is the cheapest thing to shorten, so its middle goes before any turn is dropped and before a model is asked for a summary.
qsizetype ToolContracts::pruneToolResults(QJsonArray& messages, std::optional<qint64> limit) {
    if (!limit.has_value()) {
        return 0;
    }

    qint64 current = ToolContracts::estimateTokens(messages);

    if (current <= limit.value()) {
        return 0;
    }

    qsizetype pruned = 0;

    for (qsizetype index = 0; index < messages.size() && current > limit.value(); ++index) {
        const QJsonObject message = messages.at(index).toObject();
        if (!AiToolContractHelper::answersToolCalls(message)) {
            continue;
        }
        bool changed = false;
        const QJsonObject shortened = AiToolContractHelper::prunedToolResult(message, changed);
        if (changed) {
            // Measuring a message serializes it, so what the total lost is read from the two sizes rather than from the whole conversation again.
            current -= AiToolContractHelper::messageTokens(message) - AiToolContractHelper::messageTokens(shortened);
            messages.replace(index, shortened);
            ++pruned;
        }
    }

    return pruned;
}

FittedConversation ToolContracts::fitConversation(const QJsonArray& messages, std::optional<qint64> limit) {
    // Measuring a message serializes it, so every size is taken once and the loop below reads the total it keeps.
    QList<qint64> sizes;
    sizes.reserve(messages.size());
    qint64 total = 0;

    for (const auto& value : messages) {
        sizes.append(AiToolContractHelper::messageTokens(value.toObject()));
        total += sizes.constLast();
    }

    if (!limit.has_value() || total <= limit.value()) {
        return {messages, {}, 0};
    }

    // The instructions and the task itself are never dropped, because without them the run loses its purpose.
    QJsonArray preserved;
    qsizetype start = 0;

    while (start < messages.size() && preserved.size() < 2) {
        const QJsonObject message = messages.at(start).toObject();
        const QString role = message.value(QStringLiteral("role")).toString();
        if (role != QStringLiteral("system") && role != QStringLiteral("user")) {
            break;
        }
        preserved.append(message);
        ++start;
    }

    QJsonArray dropped;
    qsizetype first = start;

    while (first < messages.size() && total > limit.value()) {
        dropped.append(messages.at(first));
        total -= sizes.at(first);
        ++first;
        while (first < messages.size() && AiToolContractHelper::answersToolCalls(messages.at(first).toObject())) {
            dropped.append(messages.at(first));
            total -= sizes.at(first);
            ++first;
        }
    }

    const qsizetype preservedHead = preserved.size();

    for (qsizetype index = first; index < messages.size(); ++index) {
        preserved.append(messages.at(index));
    }

    return {preserved, dropped, preservedHead};
}

bool AiToolContractHelper::matchesDeclaredType(const QString& declared, const QJsonValue& value) {
    if (declared == QStringLiteral("string")) {
        return value.isString();
    }
    if (declared == QStringLiteral("integer") || declared == QStringLiteral("number")) {
        return value.isDouble();
    }
    if (declared == QStringLiteral("boolean")) {
        return value.isBool();
    }
    if (declared == QStringLiteral("array")) {
        return value.isArray();
    }
    if (declared == QStringLiteral("object")) {
        return value.isObject();
    }

    return true;
}

Result<void> ToolContracts::validateToolSchema(const ToolSchema& schema) {
    if (!AiToolContractHelper::toolNamePattern().match(schema.name).hasMatch()) {
        return Result<void>::failure({"ai_tool_invalid", "The tool name is invalid", schema.name});
    }
    if (schema.descriptionKey.isEmpty()) {
        return Result<void>::failure({"ai_tool_invalid", "The tool description is missing", schema.name});
    }
    if (schema.parameters.value(QStringLiteral("type")).toString() != QStringLiteral("object") || !schema.parameters.value(QStringLiteral("properties")).isObject()) {
        return Result<void>::failure({"ai_tool_invalid", "The tool parameters must be a JSON Schema object", schema.name});
    }

    return Result<void>::success();
}

// An argument the schema declares is judged only by what that schema says, so a server tool is checked exactly like a native one.
std::optional<ToolArgumentError> ToolContracts::findToolArgumentError(const ToolSchema& schema, const QJsonObject& arguments) {
    const QJsonObject properties = schema.parameters.value(QStringLiteral("properties")).toObject();

    for (const auto& value : schema.parameters.value(QStringLiteral("required")).toArray()) {
        const QString name = value.toString();
        if (!arguments.contains(name)) {
            return ToolArgumentError{name, properties.value(name).toObject().value(QStringLiteral("type")).toString()};
        }
    }

    for (auto argument = arguments.constBegin(); argument != arguments.constEnd(); ++argument) {
        const QString declared = properties.value(argument.key()).toObject().value(QStringLiteral("type")).toString();
        if (declared.isEmpty() || AiToolContractHelper::matchesDeclaredType(declared, argument.value())) {
            continue;
        }
        return ToolArgumentError{argument.key(), declared};
    }

    return std::nullopt;
}

QJsonArray ToolContracts::serializeTools(WireProtocol protocol, const QVector<ToolSchema>& tools, const std::function<QString(const QString&)>& translate) {
    QJsonArray serialized;

    for (const auto& tool : tools) {
        const QString description = translate(tool.descriptionKey);
        if (protocol == WireProtocol::Anthropic) {
            serialized.append(QJsonObject{{QStringLiteral("name"), tool.name}, {QStringLiteral("description"), description}, {QStringLiteral("input_schema"), tool.parameters}});
            continue;
        }
        const QJsonObject function{{QStringLiteral("name"), tool.name}, {QStringLiteral("description"), description}, {QStringLiteral("parameters"), tool.parameters}};
        serialized.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("function")}, {QStringLiteral("function"), function}});
    }

    return serialized;
}

QJsonObject ToolContracts::serializeAssistantTurn(WireProtocol protocol, const QString& content, const QVector<ToolCall>& calls) {
    if (protocol == WireProtocol::Anthropic) {
        QJsonArray blocks;
        if (!content.isEmpty()) {
            blocks.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), content}});
        }
        for (const auto& call : calls) {
            blocks.append(AiToolContractHelper::anthropicToolUse(call));
        }
        return {{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), blocks}};
    }

    QJsonObject message{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), content}};

    if (calls.isEmpty()) {
        return message;
    }

    QJsonArray toolCalls;

    for (const auto& call : calls) {
        toolCalls.append(AiToolContractHelper::openAiToolCall(call));
    }

    message.insert(QStringLiteral("tool_calls"), toolCalls);
    return message;
}

// The Anthropic API accepts an image inside the result of the tool that read it, so it travels with the answer it belongs to.
QVector<QJsonObject> ToolContracts::serializeToolResults(WireProtocol protocol, const QVector<ToolResult>& results) {
    if (protocol == WireProtocol::Anthropic) {
        QJsonArray blocks;
        for (const auto& result : results) {
            QJsonObject block{{QStringLiteral("type"), QStringLiteral("tool_result")}, {QStringLiteral("tool_use_id"), result.callId}, {QStringLiteral("content"), result.text}};
            if (!result.imageData.isEmpty()) {
                const QJsonObject source{{QStringLiteral("type"), QStringLiteral("base64")}, {QStringLiteral("media_type"), QString::fromUtf8(result.imageMediaType)}, {QStringLiteral("data"), QString::fromUtf8(result.imageData.toBase64())}};
                block.insert(QStringLiteral("content"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), result.text}}, QJsonObject{{QStringLiteral("type"), QStringLiteral("image")}, {QStringLiteral("source"), source}}});
            }
            if (result.failed) {
                block.insert(QStringLiteral("is_error"), true);
            }
            blocks.append(block);
        }
        return blocks.isEmpty() ? QVector<QJsonObject>{} : QVector<QJsonObject>{{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), blocks}}};
    }

    // The OpenAI tool message carries text alone, so an image the tool read follows it as the user turn that shows it.
    QVector<QJsonObject> messages;
    QJsonArray images;

    for (const auto& result : results) {
        messages.append({{QStringLiteral("role"), QStringLiteral("tool")}, {QStringLiteral("tool_call_id"), result.callId}, {QStringLiteral("content"), result.text}});
        if (!result.imageData.isEmpty()) {
            const QString url = QStringLiteral("data:%1;base64,%2").arg(QString::fromUtf8(result.imageMediaType), QString::fromUtf8(result.imageData.toBase64()));
            images.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("image_url")}, {QStringLiteral("image_url"), QJsonObject{{QStringLiteral("url"), url}}}});
        }
    }

    if (!images.isEmpty()) {
        messages.append({{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), images}});
    }

    return messages;
}

ToolCallAccumulator::ToolCallAccumulator(WireProtocol protocol) : m_protocol(protocol) {}

void ToolCallAccumulator::consume(const QJsonObject& event) {
    if (m_protocol == WireProtocol::Anthropic) {
        const QString type = event.value(QStringLiteral("type")).toString();
        const int index = event.value(QStringLiteral("index")).toInt(-1);
        if (index < 0) {
            return;
        }
        if (type == QStringLiteral("content_block_start")) {
            const QJsonObject block = event.value(QStringLiteral("content_block")).toObject();
            if (block.value(QStringLiteral("type")).toString() == QStringLiteral("tool_use")) {
                m_pending.insert(index, {block.value(QStringLiteral("id")).toString(), block.value(QStringLiteral("name")).toString(), {}});
            }
            return;
        }
        if (type == QStringLiteral("content_block_delta") && m_pending.contains(index)) {
            const QJsonObject delta = event.value(QStringLiteral("delta")).toObject();
            if (delta.value(QStringLiteral("type")).toString() == QStringLiteral("input_json_delta")) {
                m_pending[index].arguments.append(delta.value(QStringLiteral("partial_json")).toString());
            }
        }
        return;
    }

    const QJsonArray choices = event.value(QStringLiteral("choices")).toArray();

    if (choices.isEmpty()) {
        return;
    }

    for (const auto& value : choices.first().toObject().value(QStringLiteral("delta")).toObject().value(QStringLiteral("tool_calls")).toArray()) {
        const QJsonObject call = value.toObject();
        const int index = call.value(QStringLiteral("index")).toInt(-1);
        if (index < 0) {
            continue;
        }
        PendingCall& target = m_pending[index];
        if (call.contains(QStringLiteral("id"))) {
            target.id = call.value(QStringLiteral("id")).toString();
        }
        const QJsonObject function = call.value(QStringLiteral("function")).toObject();
        if (function.contains(QStringLiteral("name"))) {
            target.name = function.value(QStringLiteral("name")).toString();
        }
        // A service that sends the arguments already parsed is read the same way as one that streams them as text.
        const QJsonValue arguments = function.value(QStringLiteral("arguments"));
        target.arguments.append(arguments.isObject() ? QString::fromUtf8(QJsonDocument(arguments.toObject()).toJson(QJsonDocument::Compact)) : arguments.toString());
    }
}

QJsonValue AiToolContractHelper::joinedContent(const QJsonValue& first, const QJsonValue& second) {
    QJsonArray blocks;
    // clang-format off
    const auto append = [&blocks](const QJsonValue& value) {
        if (value.isArray()) {
            for (const auto& block : value.toArray()) { blocks.append(block); }
            return;
        }
        if (!value.toString().isEmpty()) { blocks.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), value.toString()}}); }
    };
    // clang-format on
    append(first);
    append(second);
    return blocks;
}

bool ToolContracts::protocolRequiresAlternatingRoles(WireProtocol protocol) {
    return protocol == WireProtocol::Anthropic;
}

// The Anthropic API refuses a conversation that opens with an assistant turn or repeats a role, so consecutive turns of one role are joined into the turn the protocol expects.
QJsonArray ToolContracts::enforceProtocolShape(WireProtocol protocol, const QJsonArray& messages) {
    if (!ToolContracts::protocolRequiresAlternatingRoles(protocol)) {
        return messages;
    }

    QJsonArray shaped;
    bool opened = false;

    for (const auto& value : messages) {
        const QJsonObject message = value.toObject();
        const QString role = message.value(QStringLiteral("role")).toString();
        if (role == QStringLiteral("system")) {
            shaped.append(message);
            continue;
        }
        // A turn nobody asked for has nothing to answer, so an assistant turn opening the conversation is dropped.
        if (!opened && role == QStringLiteral("assistant")) {
            continue;
        }
        if (opened && shaped.last().toObject().value(QStringLiteral("role")).toString() == role) {
            QJsonObject merged = shaped.last().toObject();
            merged.insert(QStringLiteral("content"), AiToolContractHelper::joinedContent(merged.value(QStringLiteral("content")), message.value(QStringLiteral("content"))));
            shaped.replace(shaped.size() - 1, merged);
            continue;
        }

        opened = true;
        shaped.append(message);
    }

    return shaped;
}

Result<QVector<ToolCall>> ToolCallAccumulator::calls() const {
    QVector<ToolCall> completed;

    for (auto entry = m_pending.constBegin(); entry != m_pending.constEnd(); ++entry) {
        const PendingCall& pending = entry.value();
        if (pending.id.isEmpty() || pending.name.isEmpty()) {
            return Result<QVector<ToolCall>>::failure({"ai_tool_call_invalid", "The model returned an incomplete tool call", pending.name});
        }

        const QString arguments = pending.arguments.isEmpty() ? QStringLiteral("{}") : pending.arguments;
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(arguments.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            completed.append({pending.id, pending.name, {}, arguments.left(maximumReportedArgumentCharacters)});
            continue;
        }
        completed.append({pending.id, pending.name, document.object(), {}});
    }

    return Result<QVector<ToolCall>>::success(std::move(completed));
}

bool ToolCallAccumulator::empty() const {
    return m_pending.isEmpty();
}

void ToolCallAccumulator::clear() {
    m_pending.clear();
}

} // namespace workpane::plugins::ai
