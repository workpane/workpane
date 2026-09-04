#include "AiToolRegistry.h"

#include "agent/BoundedReply.h"
#include "persistence/StoredValues.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <QTimeZone>
#include <QUrl>
#include <QUrlQuery>
#include <QtConcurrent>

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

namespace workpane::plugins::ai {

constexpr int readableArgumentLimit = 56;

constexpr qint64 maximumReadBytes = 1 << 20;
constexpr int maximumListedEntries = 200;
constexpr int defaultCommandTimeoutSeconds = 120;
constexpr qint64 maximumFetchBytes = 1 << 19;
constexpr int fetchTimeoutMs = 30000;
constexpr qsizetype maximumResultCharacters = 24000;
constexpr qint64 maximumViewedImageBytes = 4LL * 1024 * 1024;
constexpr qsizetype maximumResultHeadCharacters = 18000;
constexpr qsizetype maximumResultTailCharacters = 6000;
constexpr qint64 maximumSkillBytes = 1 << 16;
constexpr int imageTimeoutMs = 180000;
constexpr int searchTimeoutMs = 30000;
constexpr int defaultSearchResults = 5;
constexpr int maximumSearchResults = 20;
constexpr qint64 maximumImageBytes = 1 << 24;

class AiToolRegistryHelper final {
  public:
    static const agent::ResourceDescriptor* findSkill(const QVector<agent::ResourceDescriptor>& catalog, const QString& name);
    static QString readableToolName(const QString& toolName);
    static QString shortenedArgument(const QString& value);
    static QJsonObject stringProperty(const QString& description);
    static QJsonObject integerProperty(const QString& description);
    static QJsonObject booleanProperty(const QString& description);
    static ToolSchema readFileSchema();
    static ToolSchema createDirectorySchema();
    static ToolSchema movePathSchema();
    static ToolSchema copyFileSchema();
    static ToolSchema removePathSchema();
    static ToolSchema describePathSchema();
    static ToolSchema searchFilesSchema();
    static ToolSchema runCommandSchema();
    static ToolSchema writeFileSchema();
    static ToolSchema editFileSchema();
    static ToolSchema readImageSchema();
    static QByteArray imageMediaType(const QString& path);
    static ToolSchema listDirectorySchema();
    static ToolSchema fetchUrlSchema();
    static QString readableText(const QByteArray& body);
    static ToolSchema generateImageSchema();
    static ToolSchema webSearchSchema();
    static ToolSchema listMcpResourcesSchema();
    static ToolSchema readMcpResourceSchema();
    static ToolSchema listMcpPromptsSchema();
    static ToolSchema readMcpPromptSchema();
    static QString serverCatalogText(const QString& serverId, const QJsonArray& entries, bool prompts);
    static ToolSchema listSkillsSchema();
    static ToolSchema searchSkillsSchema();
    static ToolSchema readSkillSchema();
    static ToolSchema readSkillFileSchema();
    static ToolSchema describeTaskSchema();
    static ToolSchema generateSpeechSchema();
    static ToolSchema listVoicesSchema();
    static QJsonArray searchEntries(SearchProvider provider, const QJsonObject& payload);
    static QString searchSummary(SearchProvider provider, const QJsonObject& entry);
    static QString formatSearchResults(SearchProvider provider, const QJsonObject& payload, int count, const QString& emptyMessage);
    static QString serviceErrorMessage(const QByteArray& payload, const QString& transportMessage);
    static ToolResult failure(const ToolCall& call, const QString& message);
    static QString selectedLines(const QString& content, int firstLine, int lastLine);
    static QString boundedText(const QString& text);
    static std::optional<QString> decodedText(const QByteArray& bytes);
    static bool pathsOverlap(const QString& first, const QString& second);
    static QString qualifiedToolName(const QString& serverId, const QString& toolName);
    static ToolResult mcpToolResult(const ToolCall& call, const QJsonObject& payload);
};

QJsonObject AiToolRegistryHelper::stringProperty(const QString& description) {
    return {{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), description}};
}

QJsonObject AiToolRegistryHelper::integerProperty(const QString& description) {
    return {{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), description}};
}

QJsonObject AiToolRegistryHelper::booleanProperty(const QString& description) {
    return {{QStringLiteral("type"), QStringLiteral("boolean")}, {QStringLiteral("description"), description}};
}

ToolSchema AiToolRegistryHelper::readFileSchema() {
    const QJsonObject properties{{QStringLiteral("path"), stringProperty(QStringLiteral("Path inside the task working directory, written relative to it or in full"))}, {QStringLiteral("start_line"), integerProperty(QStringLiteral("First line to read, counting from one"))}, {QStringLiteral("end_line"), integerProperty(QStringLiteral("Last line to read, counting from one"))}};
    return {QStringLiteral("read_file"), QStringLiteral("ai.tool.read-file"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties}, {QStringLiteral("required"), QJsonArray{QStringLiteral("path")}}}, QStringLiteral("ai.tool-title.read-file"), QStringLiteral("ai.tool-activity.read-file"), QStringLiteral("path")};
}

ToolSchema AiToolRegistryHelper::createDirectorySchema() {
    const QJsonObject properties{{QStringLiteral("path"), stringProperty(QStringLiteral("Directory to create, relative to the task working directory"))}};
    return {QStringLiteral("create_directory"), QStringLiteral("ai.tool.create-directory"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties}, {QStringLiteral("required"), QJsonArray{QStringLiteral("path")}}}, QStringLiteral("ai.tool-title.create-directory"), QStringLiteral("ai.tool-activity.create-directory"), QStringLiteral("path")};
}

ToolSchema AiToolRegistryHelper::movePathSchema() {
    const QJsonObject properties{{QStringLiteral("source"), stringProperty(QStringLiteral("Path to move or rename"))}, {QStringLiteral("destination"), stringProperty(QStringLiteral("Destination path, which must not exist"))}};
    return {QStringLiteral("move_path"), QStringLiteral("ai.tool.move-path"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties}, {QStringLiteral("required"), QJsonArray{QStringLiteral("source"), QStringLiteral("destination")}}}, QStringLiteral("ai.tool-title.move-path"), QStringLiteral("ai.tool-activity.move-path"), QStringLiteral("source")};
}

ToolSchema AiToolRegistryHelper::copyFileSchema() {
    const QJsonObject properties{{QStringLiteral("source"), stringProperty(QStringLiteral("File to copy"))}, {QStringLiteral("destination"), stringProperty(QStringLiteral("Destination path, which must not exist"))}};
    return {QStringLiteral("copy_file"), QStringLiteral("ai.tool.copy-file"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties}, {QStringLiteral("required"), QJsonArray{QStringLiteral("source"), QStringLiteral("destination")}}}, QStringLiteral("ai.tool-title.copy-file"), QStringLiteral("ai.tool-activity.copy-file"), QStringLiteral("source")};
}

ToolSchema AiToolRegistryHelper::removePathSchema() {
    const QJsonObject properties{{QStringLiteral("path"), stringProperty(QStringLiteral("File or directory to remove"))}, {QStringLiteral("recursive"), booleanProperty(QStringLiteral("Remove a directory together with everything inside it"))}};
    return {QStringLiteral("remove_path"), QStringLiteral("ai.tool.remove-path"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties}, {QStringLiteral("required"), QJsonArray{QStringLiteral("path")}}}, QStringLiteral("ai.tool-title.remove-path"), QStringLiteral("ai.tool-activity.remove-path"), QStringLiteral("path")};
}

ToolSchema AiToolRegistryHelper::describePathSchema() {
    const QJsonObject properties{{QStringLiteral("path"), stringProperty(QStringLiteral("Path to inspect"))}};
    return {QStringLiteral("describe_path"), QStringLiteral("ai.tool.describe-path"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties}, {QStringLiteral("required"), QJsonArray{QStringLiteral("path")}}}, QStringLiteral("ai.tool-title.describe-path"), QStringLiteral("ai.tool-activity.describe-path"), QStringLiteral("path")};
}

ToolSchema AiToolRegistryHelper::searchFilesSchema() {
    const QJsonObject properties{{QStringLiteral("pattern"), stringProperty(QStringLiteral("Name pattern such as *.cpp"))}, {QStringLiteral("path"), stringProperty(QStringLiteral("Directory to search, defaulting to the working directory"))}, {QStringLiteral("contains"), stringProperty(QStringLiteral("Text every matching file must contain"))}};
    return {QStringLiteral("search_files"), QStringLiteral("ai.tool.search-files"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties}, {QStringLiteral("required"), QJsonArray{QStringLiteral("pattern")}}}, QStringLiteral("ai.tool-title.search-files"), QStringLiteral("ai.tool-activity.search-files"), QStringLiteral("query")};
}

ToolSchema AiToolRegistryHelper::runCommandSchema() {
    const QJsonObject properties{{QStringLiteral("command"), stringProperty(QStringLiteral("Command to run inside the task working directory"))}, {QStringLiteral("timeout_seconds"), integerProperty(QStringLiteral("Time limit in seconds, where zero means unlimited"))}};
    return {QStringLiteral("run_command"), QStringLiteral("ai.tool.run-command"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties}, {QStringLiteral("required"), QJsonArray{QStringLiteral("command")}}}, QStringLiteral("ai.tool-title.run-command"), QStringLiteral("ai.tool-activity.run-command"), QStringLiteral("command")};
}

ToolSchema AiToolRegistryHelper::writeFileSchema() {
    const QJsonObject properties{{QStringLiteral("path"), stringProperty(QStringLiteral("Path inside the task working directory, written relative to it or in full"))}, {QStringLiteral("content"), stringProperty(QStringLiteral("Complete text content to write"))}};
    return {QStringLiteral("write_file"), QStringLiteral("ai.tool.write-file"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties}, {QStringLiteral("required"), QJsonArray{QStringLiteral("path"), QStringLiteral("content")}}}, QStringLiteral("ai.tool-title.write-file"), QStringLiteral("ai.tool-activity.write-file"), QStringLiteral("path")};
}

ToolSchema AiToolRegistryHelper::editFileSchema() {
    const QJsonObject properties{{QStringLiteral("path"), stringProperty(QStringLiteral("Path inside the task working directory, written relative to it or in full"))}, {QStringLiteral("old_text"), stringProperty(QStringLiteral("Exact text to replace, including its indentation"))}, {QStringLiteral("new_text"), stringProperty(QStringLiteral("Text that replaces it, empty to delete the passage"))}, {QStringLiteral("replace_all"), booleanProperty(QStringLiteral("Replace every occurrence instead of requiring exactly one"))}};
    return {QStringLiteral("edit_file"), QStringLiteral("ai.tool.edit-file"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties}, {QStringLiteral("required"), QJsonArray{QStringLiteral("path"), QStringLiteral("old_text"), QStringLiteral("new_text")}}}, QStringLiteral("ai.tool-title.edit-file"), QStringLiteral("ai.tool-activity.edit-file"), QStringLiteral("path")};
}

ToolSchema AiToolRegistryHelper::readImageSchema() {
    const QJsonObject properties{{QStringLiteral("path"), stringProperty(QStringLiteral("Image file relative to the task working directory"))}};
    return {QStringLiteral("read_image"), QStringLiteral("ai.tool.read-image"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties}, {QStringLiteral("required"), QJsonArray{QStringLiteral("path")}}}, QStringLiteral("ai.tool-title.read-image"), QStringLiteral("ai.tool-activity.read-image"), QStringLiteral("path")};
}

// The format is declared by the name, and a name that declares none is not an image this tool can hand to a model.
// The catalog carries every published kind, so the skill is matched by its kind as well as by its name.
const agent::ResourceDescriptor* AiToolRegistryHelper::findSkill(const QVector<agent::ResourceDescriptor>& catalog, const QString& name) {
    for (const auto& skill : catalog) {
        if (skill.kind == agent::ResourceKind::Skill && skill.name.compare(name, Qt::CaseInsensitive) == 0) {
            return &skill;
        }
    }

    return nullptr;
}

QByteArray AiToolRegistryHelper::imageMediaType(const QString& path) {
    static const QHash<QString, QByteArray> types{{QStringLiteral("png"), QByteArrayLiteral("image/png")}, {QStringLiteral("jpg"), QByteArrayLiteral("image/jpeg")}, {QStringLiteral("jpeg"), QByteArrayLiteral("image/jpeg")}, {QStringLiteral("webp"), QByteArrayLiteral("image/webp")}, {QStringLiteral("gif"), QByteArrayLiteral("image/gif")}};
    return types.value(QFileInfo(path).suffix().toLower());
}

ToolSchema AiToolRegistryHelper::listDirectorySchema() {
    const QJsonObject properties{{QStringLiteral("path"), stringProperty(QStringLiteral("Directory inside the task working directory, written relative to it or in full"))}};
    return {QStringLiteral("list_directory"), QStringLiteral("ai.tool.list-directory"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties}}, QStringLiteral("ai.tool-title.list-directory"), QStringLiteral("ai.tool-activity.list-directory"), QStringLiteral("path")};
}

ToolSchema AiToolRegistryHelper::fetchUrlSchema() {
    const QJsonObject properties{{QStringLiteral("url"), stringProperty(QStringLiteral("Absolute HTTP or HTTPS address to fetch"))}};
    return {QStringLiteral("fetch_url"), QStringLiteral("ai.tool.fetch-url"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties}, {QStringLiteral("required"), QJsonArray{QStringLiteral("url")}}}, QStringLiteral("ai.tool-title.fetch-url"), QStringLiteral("ai.tool-activity.fetch-url"), QStringLiteral("url")};
}

QString AiToolRegistryHelper::readableText(const QByteArray& body) {
    QString text = QString::fromUtf8(body);
    text.replace(QRegularExpression(QStringLiteral("(?is)<(script|style)\\b.*?</\\1>")), QString{});
    text.replace(QRegularExpression(QStringLiteral("<[^>]*>")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return text.trimmed();
}

ToolSchema AiToolRegistryHelper::generateImageSchema() {
    const QJsonObject properties{{QStringLiteral("prompt"), stringProperty(QStringLiteral("Description of the image to generate"))}, {QStringLiteral("path"), stringProperty(QStringLiteral("Destination file inside the task working directory"))}, {QStringLiteral("size"), stringProperty(QStringLiteral("Requested size such as 1024x1024"))}};
    return {QStringLiteral("generate_image"), QStringLiteral("ai.tool.generate-image"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties}, {QStringLiteral("required"), QJsonArray{QStringLiteral("prompt"), QStringLiteral("path")}}}, QStringLiteral("ai.tool-title.generate-image"), QStringLiteral("ai.tool-activity.generate-image"), QStringLiteral("prompt")};
}

// A published tool is named the way its server spelled it, with the marks that separate its words read as spaces.
QString AiToolRegistryHelper::readableToolName(const QString& toolName) {
    QString written = toolName.section(QLatin1Char('.'), -1);
    written.replace(QLatin1Char('_'), QLatin1Char(' '));
    written.replace(QLatin1Char('-'), QLatin1Char(' '));
    QStringList words;

    for (const auto& word : written.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        words.append(word.left(1).toUpper() + word.mid(1));
    }

    return words.join(QLatin1Char(' '));
}

// A long argument keeps its beginning and its end, because the end of a path is what names the file.
QString AiToolRegistryHelper::shortenedArgument(const QString& value) {
    const QString single = QString(value).replace(QLatin1Char('\n'), QLatin1Char(' ')).simplified();

    if (single.size() <= readableArgumentLimit) {
        return single;
    }

    const int half = (readableArgumentLimit - 1) / 2;
    return single.left(half) + QStringLiteral("…") + single.right(half);
}

ToolSchema AiToolRegistryHelper::webSearchSchema() {
    const QJsonObject properties{{QStringLiteral("query"), stringProperty(QStringLiteral("Words to search the web for"))}, {QStringLiteral("count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("How many results to return, up to twenty")}}}};
    return {QStringLiteral("web_search"), QStringLiteral("ai.tool.web-search"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties}, {QStringLiteral("required"), QJsonArray{QStringLiteral("query")}}}, QStringLiteral("ai.tool-title.web-search"), QStringLiteral("ai.tool-activity.web-search"), QStringLiteral("query")};
}

ToolSchema AiToolRegistryHelper::listMcpResourcesSchema() {
    return {QStringLiteral("list_mcp_resources"), QStringLiteral("ai.tool.list-mcp-resources"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), QJsonObject{}}}, QStringLiteral("ai.tool-title.list-mcp-resources"), QStringLiteral("ai.tool-activity.list-mcp-resources"), QStringLiteral("")};
}

ToolSchema AiToolRegistryHelper::readMcpResourceSchema() {
    const QJsonObject properties{{QStringLiteral("server"), stringProperty(QStringLiteral("Identifier of the server that publishes the resource"))}, {QStringLiteral("uri"), stringProperty(QStringLiteral("Address of the resource to read"))}};
    return {QStringLiteral("read_mcp_resource"), QStringLiteral("ai.tool.read-mcp-resource"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties}, {QStringLiteral("required"), QJsonArray{QStringLiteral("server"), QStringLiteral("uri")}}}, QStringLiteral("ai.tool-title.read-mcp-resource"), QStringLiteral("ai.tool-activity.read-mcp-resource"), QStringLiteral("uri")};
}

ToolSchema AiToolRegistryHelper::listMcpPromptsSchema() {
    return {QStringLiteral("list_mcp_prompts"), QStringLiteral("ai.tool.list-mcp-prompts"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), QJsonObject{}}}, QStringLiteral("ai.tool-title.list-mcp-prompts"), QStringLiteral("ai.tool-activity.list-mcp-prompts"), QStringLiteral("")};
}

ToolSchema AiToolRegistryHelper::readMcpPromptSchema() {
    const QJsonObject properties{{QStringLiteral("server"), stringProperty(QStringLiteral("Identifier of the server that publishes the prompt"))}, {QStringLiteral("name"), stringProperty(QStringLiteral("Name of the prompt to load"))}, {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("description"), QStringLiteral("Arguments the prompt declares")}}}};
    return {QStringLiteral("read_mcp_prompt"), QStringLiteral("ai.tool.read-mcp-prompt"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties}, {QStringLiteral("required"), QJsonArray{QStringLiteral("server"), QStringLiteral("name")}}}, QStringLiteral("ai.tool-title.read-mcp-prompt"), QStringLiteral("ai.tool-activity.read-mcp-prompt"), QStringLiteral("name")};
}

QString AiToolRegistryHelper::serverCatalogText(const QString& serverId, const QJsonArray& entries, bool prompts) {
    QStringList lines;

    for (const auto& value : entries) {
        const QJsonObject entry = value.toObject();
        const QString identity = prompts ? entry.value(QStringLiteral("name")).toString() : entry.value(QStringLiteral("uri")).toString();
        lines.append(QStringLiteral("%1 | %2 | %3").arg(serverId, identity, entry.value(QStringLiteral("description")).toString()));
    }

    return lines.join(QLatin1Char('\n'));
}

ToolSchema AiToolRegistryHelper::listSkillsSchema() {
    return {QStringLiteral("list_skills"), QStringLiteral("ai.tool.list-skills"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), QJsonObject{}}}, QStringLiteral("ai.tool-title.list-skills"), QStringLiteral("ai.tool-activity.list-skills"), QStringLiteral("")};
}

ToolSchema AiToolRegistryHelper::searchSkillsSchema() {
    const QJsonObject properties{{QStringLiteral("query"), stringProperty(QStringLiteral("Words to match against the skill name and description"))}};
    return {QStringLiteral("search_skills"), QStringLiteral("ai.tool.search-skills"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties}, {QStringLiteral("required"), QJsonArray{QStringLiteral("query")}}}, QStringLiteral("ai.tool-title.search-skills"), QStringLiteral("ai.tool-activity.search-skills"), QStringLiteral("query")};
}

ToolSchema AiToolRegistryHelper::readSkillSchema() {
    const QJsonObject properties{{QStringLiteral("name"), stringProperty(QStringLiteral("Name of the skill to load"))}};
    return {QStringLiteral("read_skill"), QStringLiteral("ai.tool.read-skill"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties}, {QStringLiteral("required"), QJsonArray{QStringLiteral("name")}}}, QStringLiteral("ai.tool-title.read-skill"), QStringLiteral("ai.tool-activity.read-skill"), QStringLiteral("name")};
}

ToolSchema AiToolRegistryHelper::readSkillFileSchema() {
    const QJsonObject properties{{QStringLiteral("name"), stringProperty(QStringLiteral("Name of the skill that carries the file"))}, {QStringLiteral("path"), stringProperty(QStringLiteral("Path of the file inside the skill, such as reference.md"))}};
    return {QStringLiteral("read_skill_file"), QStringLiteral("ai.tool.read-skill-file"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties}, {QStringLiteral("required"), QJsonArray{QStringLiteral("name"), QStringLiteral("path")}}}, QStringLiteral("ai.tool-title.read-skill-file"), QStringLiteral("ai.tool-activity.read-skill-file"), QStringLiteral("path")};
}

ToolSchema AiToolRegistryHelper::describeTaskSchema() {
    return {QStringLiteral("describe_task"), QStringLiteral("ai.tool.describe-task"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), QJsonObject{}}}, QStringLiteral("ai.tool-title.describe-task"), QStringLiteral("ai.tool-activity.describe-task"), QStringLiteral("")};
}

ToolSchema AiToolRegistryHelper::generateSpeechSchema() {
    const QJsonObject properties{{QStringLiteral("text"), stringProperty(QStringLiteral("Text to speak"))}, {QStringLiteral("path"), stringProperty(QStringLiteral("Destination file inside the task working directory"))}, {QStringLiteral("voice"), stringProperty(QStringLiteral("Voice to speak with, defaulting to the configured one"))}};
    return {QStringLiteral("generate_speech"), QStringLiteral("ai.tool.generate-speech"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties}, {QStringLiteral("required"), QJsonArray{QStringLiteral("text"), QStringLiteral("path")}}}, QStringLiteral("ai.tool-title.generate-speech"), QStringLiteral("ai.tool-activity.generate-speech"), QStringLiteral("text")};
}

ToolSchema AiToolRegistryHelper::listVoicesSchema() {
    return {QStringLiteral("list_voices"), QStringLiteral("ai.tool.list-voices"), {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), QJsonObject{}}}, QStringLiteral("ai.tool-title.list-voices"), QStringLiteral("ai.tool-activity.list-voices"), QStringLiteral("")};
}

QJsonArray AiToolRegistryHelper::searchEntries(SearchProvider provider, const QJsonObject& payload) {
    if (provider == SearchProvider::Brave) {
        return payload.value(QStringLiteral("web")).toObject().value(QStringLiteral("results")).toArray();
    }

    return payload.value(QStringLiteral("results")).toArray();
}

QString AiToolRegistryHelper::searchSummary(SearchProvider provider, const QJsonObject& entry) {
    return provider == SearchProvider::Brave ? entry.value(QStringLiteral("description")).toString() : entry.value(QStringLiteral("content")).toString();
}

QString AiToolRegistryHelper::formatSearchResults(SearchProvider provider, const QJsonObject& payload, int count, const QString& emptyMessage) {
    const QJsonArray entries = searchEntries(provider, payload);
    QStringList lines;

    for (const auto& value : entries) {
        if (lines.size() >= count) {
            break;
        }
        const QJsonObject entry = value.toObject();
        lines.append(QStringLiteral("%1\n%2\n%3").arg(entry.value(QStringLiteral("title")).toString(), entry.value(QStringLiteral("url")).toString(), readableText(searchSummary(provider, entry).toUtf8())));
    }

    return lines.isEmpty() ? emptyMessage : lines.join(QStringLiteral("\n\n"));
}

// A service answers a failure with its own reason, and the agent needs that reason to explain what happened.
QString AiToolRegistryHelper::serviceErrorMessage(const QByteArray& payload, const QString& transportMessage) {
    const QJsonObject document = QJsonDocument::fromJson(payload).object();
    const QStringList candidates{QStringLiteral("message"), QStringLiteral("detail"), QStringLiteral("error_description")};

    for (const auto& key : candidates) {
        const QJsonValue value = document.value(key);
        if (value.isString() && !value.toString().isEmpty()) {
            return value.toString();
        }
        if (value.isObject() && value.toObject().value(QStringLiteral("message")).isString()) {
            return value.toObject().value(QStringLiteral("message")).toString();
        }
    }

    const QJsonValue error = document.value(QStringLiteral("error"));

    if (error.isString() && !error.toString().isEmpty()) {
        return error.toString();
    }
    if (error.isObject() && error.toObject().value(QStringLiteral("message")).isString()) {
        return error.toObject().value(QStringLiteral("message")).toString();
    }

    return payload.isEmpty() ? transportMessage : QStringLiteral("%1: %2").arg(transportMessage, QString::fromUtf8(payload.left(512)));
}

ToolResult AiToolRegistryHelper::failure(const ToolCall& call, const QString& message) {
    return {call.id, message, true};
}

// A large file must not enter the context in full, so the agent asks for the whole file, a first line or a closed range.
QString AiToolRegistryHelper::selectedLines(const QString& content, int firstLine, int lastLine) {
    if (firstLine <= 0 && lastLine <= 0) {
        return content;
    }

    const QStringList lines = content.split(QLatin1Char('\n'));
    const qsizetype from = firstLine > 0 ? std::min<qsizetype>(firstLine - 1, lines.size()) : 0;
    const qsizetype to = lastLine > 0 ? std::min<qsizetype>(lastLine, lines.size()) : lines.size();

    if (from >= to) {
        return QString{};
    }

    return QStringList(lines.begin() + from, lines.begin() + to).join(QLatin1Char('\n'));
}

// A tool result travels straight into the model context, so an oversized one is truncated, and what a command reports last is what explains its failure so its end is kept with its beginning.
// A file that is not UTF-8 is refused rather than answered with the character that stands for every byte the decoding lost.
std::optional<QString> AiToolRegistryHelper::decodedText(const QByteArray& bytes) {
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder(bytes);
    return decoder.hasError() ? std::nullopt : std::optional<QString>{std::move(text)};
}

QString AiToolRegistryHelper::boundedText(const QString& text) {
    if (text.size() <= maximumResultCharacters) {
        return text;
    }

    return text.left(maximumResultHeadCharacters) + QStringLiteral("\n[... %1 characters omitted ...]\n").arg(QString::number(text.size() - maximumResultHeadCharacters - maximumResultTailCharacters)) + text.right(maximumResultTailCharacters);
}

bool AiToolRegistryHelper::pathsOverlap(const QString& first, const QString& second) {
    if (first.isEmpty() || second.isEmpty()) {
        return false;
    }
    if (first == second) {
        return true;
    }

    return first.startsWith(second + QLatin1Char('/')) || second.startsWith(first + QLatin1Char('/'));
}

QString AiToolRegistryHelper::qualifiedToolName(const QString& serverId, const QString& toolName) {
    QString exposed = QStringLiteral("mcp_%1_%2").arg(serverId, toolName).toLower();

    for (auto& character : exposed) {
        if (!character.isLetterOrNumber() && character != QLatin1Char('_')) {
            character = QLatin1Char('_');
        }
    }

    return exposed.size() > 64 ? QString() : exposed;
}

// A server answers with content blocks, so the textual blocks become the agent visible result.
ToolResult AiToolRegistryHelper::mcpToolResult(const ToolCall& call, const QJsonObject& payload) {
    QStringList sections;

    for (const auto& value : payload.value(QStringLiteral("content")).toArray()) {
        const QJsonObject block = value.toObject();
        const QString type = block.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("text")) {
            sections.append(block.value(QStringLiteral("text")).toString());
            continue;
        }
        if (type == QStringLiteral("resource")) {
            sections.append(block.value(QStringLiteral("resource")).toObject().value(QStringLiteral("text")).toString());
        }
    }

    const bool failed = payload.value(QStringLiteral("isError")).toBool();
    return {call.id, boundedText(sections.join(QStringLiteral("\n"))), failed};
}

AiToolRegistry::AiToolRegistry(PluginHost& host, QObject* parent) : QObject(parent), m_host(host), m_skills(std::make_unique<agent::AgentResourceCatalog>(host, this)), m_nativeSchemas({AiToolRegistryHelper::readFileSchema(), AiToolRegistryHelper::writeFileSchema(), AiToolRegistryHelper::editFileSchema(), AiToolRegistryHelper::readImageSchema(), AiToolRegistryHelper::listDirectorySchema(), AiToolRegistryHelper::createDirectorySchema(), AiToolRegistryHelper::movePathSchema(), AiToolRegistryHelper::copyFileSchema(), AiToolRegistryHelper::removePathSchema(), AiToolRegistryHelper::describePathSchema(), AiToolRegistryHelper::searchFilesSchema(), AiToolRegistryHelper::runCommandSchema(), AiToolRegistryHelper::fetchUrlSchema(), AiToolRegistryHelper::webSearchSchema(), AiToolRegistryHelper::generateImageSchema(), AiToolRegistryHelper::generateSpeechSchema(), AiToolRegistryHelper::listVoicesSchema(), AiToolRegistryHelper::listSkillsSchema(), AiToolRegistryHelper::searchSkillsSchema(), AiToolRegistryHelper::readSkillSchema(), AiToolRegistryHelper::readSkillFileSchema(), AiToolRegistryHelper::describeTaskSchema()}), m_schemas(m_nativeSchemas) {}

const QVector<ToolSchema>& AiToolRegistry::schemas() const {
    return m_schemas;
}

void AiToolRegistry::discoverResources(const QString& sandboxRoot, const agent::AgentResourceCatalog::Completion& completion) {
    m_skills->discover(sandboxRoot, completion);
}

void AiToolRegistry::forgetResources() {
    m_skills->forget();
}

// A call is read as the tool it is, so a declared one is named by the catalog and a published one by the server that published it.
ToolPresentation AiToolRegistry::presentation(const QString& toolName, const QJsonObject& arguments) const {
    // clang-format off
    const auto declared = std::find_if(m_schemas.cbegin(), m_schemas.cend(), [&toolName](const ToolSchema& schema) { return schema.name == toolName; });
    // clang-format on

    if (declared == m_schemas.cend()) {
        return {AiToolRegistryHelper::readableToolName(toolName), {}};
    }
    if (declared->titleKey.isEmpty()) {
        return {AiToolRegistryHelper::readableToolName(toolName), declared->descriptionKey};
    }

    const QString title = m_host.translate(declared->titleKey);

    if (declared->activityArgument.isEmpty()) {
        return {title, {}};
    }

    const QJsonValue value = arguments.value(declared->activityArgument);
    const QString written = value.isString() ? value.toString() : QString::fromUtf8(QJsonDocument::fromVariant(value.toVariant()).toJson(QJsonDocument::Compact)).trimmed();
    return {title, written.isEmpty() ? QString{} : m_host.translate(declared->activityKey).arg(AiToolRegistryHelper::shortenedArgument(written))};
}

// Two calls of one turn run together unless they can reach the same file, because a pair of edits landing on one file would otherwise lose the first.
bool ToolAccessRules::toolAccessesConflict(const ToolAccess& first, const ToolAccess& second) {
    if (first.kind == ToolAccessKind::None || second.kind == ToolAccessKind::None) {
        return false;
    }
    if (first.kind == ToolAccessKind::Everything || second.kind == ToolAccessKind::Everything) {
        return true;
    }
    if (first.kind == ToolAccessKind::Read && second.kind == ToolAccessKind::Read) {
        return false;
    }

    for (const auto& left : first.paths) {
        for (const auto& right : second.paths) {
            if (AiToolRegistryHelper::pathsOverlap(left, right)) {
                return true;
            }
        }
    }

    return false;
}

// A tool that never answers would hold the turn open forever, so every one of them carries a deadline except a command the task itself declared unlimited.
int AiToolRegistry::deadlineMsFor(const ToolCall& call) const {
    if (call.name != QStringLiteral("run_command")) {
        return ProviderCatalog::aiLimits().toolDeadlineMs;
    }

    const int declared = call.arguments.value(QStringLiteral("timeout_seconds")).toInt(defaultCommandTimeoutSeconds);
    return declared <= 0 ? 0 : declared * 1000 + ProviderCatalog::aiLimits().toolDeadlineMs;
}

// An unknown name reaches nothing here, and a command or a server tool can reach anything, so both wait for the turn to be theirs alone.
ToolAccess AiToolRegistry::accessOf(const ToolCall& call, const QString& sandboxRoot) const {
    static const QSet<QString> readingTools{QStringLiteral("read_file"), QStringLiteral("read_image"), QStringLiteral("list_directory"), QStringLiteral("describe_path"), QStringLiteral("search_files")};
    static const QSet<QString> writingTools{QStringLiteral("write_file"), QStringLiteral("edit_file"), QStringLiteral("create_directory"), QStringLiteral("remove_path"), QStringLiteral("generate_image"), QStringLiteral("generate_speech")};
    static const QSet<QString> pairTools{QStringLiteral("move_path"), QStringLiteral("copy_file")};
    static const QSet<QString> detachedTools{QStringLiteral("fetch_url"), QStringLiteral("web_search"), QStringLiteral("list_voices"), QStringLiteral("describe_task"), QStringLiteral("list_skills"), QStringLiteral("search_skills"), QStringLiteral("read_skill"), QStringLiteral("read_skill_file"), QStringLiteral("list_mcp_resources"), QStringLiteral("read_mcp_resource"), QStringLiteral("list_mcp_prompts"), QStringLiteral("read_mcp_prompt")};

    if (detachedTools.contains(call.name) || m_readOnlyMcpTools.contains(call.name)) {
        return {ToolAccessKind::None, {}};
    }

    if (readingTools.contains(call.name)) {
        const auto resolved = resolveSandboxPath(sandboxRoot, call.arguments.value(QStringLiteral("path")).toString());
        return resolved.hasValue() ? ToolAccess{ToolAccessKind::Read, {resolved.value()}} : ToolAccess{ToolAccessKind::None, {}};
    }

    if (writingTools.contains(call.name)) {
        const auto resolved = resolveSandboxPath(sandboxRoot, call.arguments.value(QStringLiteral("path")).toString());
        return resolved.hasValue() ? ToolAccess{ToolAccessKind::Write, {resolved.value()}} : ToolAccess{ToolAccessKind::None, {}};
    }

    if (pairTools.contains(call.name)) {
        const auto source = resolveSandboxPath(sandboxRoot, call.arguments.value(QStringLiteral("source")).toString());
        const auto destination = resolveSandboxPath(sandboxRoot, call.arguments.value(QStringLiteral("destination")).toString());
        QStringList paths;
        if (source.hasValue()) {
            paths.append(source.value());
        }
        if (destination.hasValue()) {
            paths.append(destination.value());
        }
        return paths.isEmpty() ? ToolAccess{ToolAccessKind::None, {}} : ToolAccess{ToolAccessKind::Write, paths};
    }

    return {ToolAccessKind::Everything, {}};
}

// Stopping a task must stop the work it started, so a command still running for one of its calls is terminated instead of left to finish alone.
void AiToolRegistry::cancel(const QString& callId) {
    const auto running = m_runningCommands.take(callId);

    if (running.runner == nullptr) {
        return;
    }

    // The answer to this call is the cancellation itself, so the runner is disconnected before it can report the exit it is about to have.
    running.runner->disconnect(this);
    running.runner->cancel();
    running.runner->deleteLater();
    running.completion({callId, m_host.translate(QStringLiteral("ai.error.tool-cancelled")), true});
}

// A tool never leaves the working directory the task declared, and a task without one has no file access at all.
Result<QString> AiToolRegistry::resolveSandboxPath(const QString& sandboxRoot, const QString& path) const {
    if (sandboxRoot.isEmpty()) {
        return Result<QString>::failure({"ai_tool_sandbox_missing", m_host.translate(QStringLiteral("ai.error.tool-workdir-none")), {}});
    }

    const QString root = QDir(sandboxRoot).canonicalPath();

    if (root.isEmpty()) {
        return Result<QString>::failure({"ai_tool_sandbox_missing", m_host.translate(QStringLiteral("ai.error.tool-workdir-unavailable")).arg(sandboxRoot), sandboxRoot});
    }
    // An empty path names the working directory itself, which is what listing it is asked for.
    // A path is accepted for where it lands rather than for how it was written, because a model told its working directory naturally writes the absolute form.
    const QString candidate = QDir::cleanPath(QDir::isAbsolutePath(path) ? path : QDir(root).filePath(path));
    // What the path really points at is what has to be inside the root, so the deepest ancestor that exists is resolved and the rest is measured from it.
    QString ancestor = candidate;
    QStringList remainder;

    while (!QFileInfo::exists(ancestor)) {
        const QString parent = QFileInfo(ancestor).absolutePath();
        if (parent == ancestor || parent.isEmpty()) {
            break;
        }
        remainder.prepend(QFileInfo(ancestor).fileName());
        ancestor = parent;
    }

    const QString canonicalAncestor = QFileInfo(ancestor).canonicalFilePath();
    const QString contained = canonicalAncestor.isEmpty() ? candidate : QDir::cleanPath(canonicalAncestor + QLatin1Char('/') + remainder.join(QLatin1Char('/')));

    if (contained != root && !contained.startsWith(root + QLatin1Char('/'))) {
        return Result<QString>::failure({"ai_tool_path_outside", m_host.translate(QStringLiteral("ai.error.tool-path-outside")).arg(path, root), path});
    }

    return Result<QString>::success(contained);
}

void AiToolRegistry::invoke(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion) {
    // A call is judged against the schema the model received, so what it got wrong is named instead of leaving it to guess and repeat.
    // clang-format off
    const auto declared = std::find_if(m_schemas.cbegin(), m_schemas.cend(), [&call](const ToolSchema& schema) { return schema.name == call.name; });
    // clang-format on

    if (declared != m_schemas.cend()) {
        if (const auto invalid = ToolContracts::findToolArgumentError(*declared, call.arguments); invalid.has_value()) {
            const QString key = call.arguments.contains(invalid->argument) ? QStringLiteral("ai.error.tool-argument-type") : QStringLiteral("ai.error.tool-argument-missing");
            completion(AiToolRegistryHelper::failure(call, m_host.translate(key).arg(call.name, invalid->argument, invalid->expectedType)));
            return;
        }
    }

    if (call.name == QStringLiteral("read_file")) {
        readFile(call, sandboxRoot, completion);
        return;
    }

    if (call.name == QStringLiteral("write_file")) {
        writeFile(call, sandboxRoot, completion);
        return;
    }

    if (call.name == QStringLiteral("edit_file")) {
        editFile(call, sandboxRoot, completion);
        return;
    }

    if (call.name == QStringLiteral("read_image")) {
        readImage(call, sandboxRoot, completion);
        return;
    }

    if (call.name == QStringLiteral("list_directory")) {
        listDirectory(call, sandboxRoot, completion);
        return;
    }

    if (call.name == QStringLiteral("create_directory")) {
        createDirectory(call, sandboxRoot, completion);
        return;
    }

    if (call.name == QStringLiteral("move_path")) {
        movePath(call, sandboxRoot, completion);
        return;
    }

    if (call.name == QStringLiteral("copy_file")) {
        copyFile(call, sandboxRoot, completion);
        return;
    }

    if (call.name == QStringLiteral("remove_path")) {
        removePath(call, sandboxRoot, completion);
        return;
    }

    if (call.name == QStringLiteral("describe_path")) {
        describePath(call, sandboxRoot, completion);
        return;
    }

    if (call.name == QStringLiteral("search_files")) {
        searchFiles(call, sandboxRoot, completion);
        return;
    }

    if (call.name == QStringLiteral("run_command")) {
        runCommand(call, sandboxRoot, completion);
        return;
    }

    if (call.name == QStringLiteral("fetch_url")) {
        fetchUrl(call, completion);
        return;
    }

    if (call.name == QStringLiteral("web_search")) {
        searchWeb(call, completion);
        return;
    }

    if (call.name == QStringLiteral("generate_image")) {
        generateImage(call, sandboxRoot, completion);
        return;
    }

    if (call.name == QStringLiteral("generate_speech")) {
        generateSpeech(call, sandboxRoot, completion);
        return;
    }

    if (call.name == QStringLiteral("list_voices")) {
        listVoices(call, completion);
        return;
    }

    if (call.name == QStringLiteral("list_skills")) {
        listSkills(sandboxRoot, completion, call);
        return;
    }

    if (call.name == QStringLiteral("search_skills")) {
        searchSkills(call, sandboxRoot, completion);
        return;
    }

    if (call.name == QStringLiteral("read_skill")) {
        readSkill(call, sandboxRoot, completion);
        return;
    }

    if (call.name == QStringLiteral("read_skill_file")) {
        readSkillFile(call, sandboxRoot, completion);
        return;
    }

    if (call.name == QStringLiteral("describe_task")) {
        describeTask(call, completion);
        return;
    }

    if (call.name == QStringLiteral("list_mcp_resources") || call.name == QStringLiteral("list_mcp_prompts")) {
        listServerCatalog(call, call.name == QStringLiteral("list_mcp_prompts"), completion);
        return;
    }

    if (call.name == QStringLiteral("read_mcp_resource")) {
        readServerResource(call, completion);
        return;
    }

    if (call.name == QStringLiteral("read_mcp_prompt")) {
        readServerPrompt(call, completion);
        return;
    }

    if (m_mcpTools.contains(call.name)) {
        callMcpTool(call, completion);
        return;
    }

    completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-unknown")).arg(call.name)));
}

// A server tool is exposed under a prefixed name so two servers publishing the same tool never collide.
void AiToolRegistry::setMcpClients(const QList<agent::mcp::McpClient*>& clients) {
    m_mcpTools.clear();
    m_readOnlyMcpTools.clear();
    m_mcpClients.clear();
    m_schemas = m_nativeSchemas;

    for (auto* client : clients) {
        if (client == nullptr || !client->ready()) {
            continue;
        }
        m_mcpClients.append(client);
        for (const auto& tool : client->tools()) {
            const QString exposed = AiToolRegistryHelper::qualifiedToolName(client->serverId(), tool.name);
            if (exposed.isEmpty() || m_mcpTools.contains(exposed)) {
                continue;
            }
            const ToolSchema schema{exposed, tool.description, tool.inputSchema, {}, {}, {}};
            if (!ToolContracts::validateToolSchema(schema).hasValue()) {
                continue;
            }
            m_mcpTools.insert(exposed, {client, tool.name});
            if (tool.readOnly) {
                m_readOnlyMcpTools.insert(exposed);
            }
            m_schemas.append(schema);
        }
    }

    if (!m_mcpClients.isEmpty()) {
        m_schemas.append(AiToolRegistryHelper::listMcpResourcesSchema());
        m_schemas.append(AiToolRegistryHelper::readMcpResourceSchema());
        m_schemas.append(AiToolRegistryHelper::listMcpPromptsSchema());
        m_schemas.append(AiToolRegistryHelper::readMcpPromptSchema());
    }
}

// The resource and prompt catalogs of every ready server are reachable only while at least one server is connected.
void AiToolRegistry::listServerCatalog(const ToolCall& call, bool prompts, const ToolCompletion& completion) {
    auto sections = std::make_shared<QStringList>();
    auto remaining = std::make_shared<int>(static_cast<int>(m_mcpClients.size()));

    if (*remaining == 0) {
        completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.mcp-none"))));
        return;
    }

    for (const auto& client : m_mcpClients) {
        if (client.isNull()) {
            --*remaining;
            continue;
        }

        const QString serverId = client->serverId();
        // clang-format off
        const auto collect = [call, completion, sections, remaining, serverId, prompts](Result<QJsonObject> result) {
            if (result.hasValue()) {
                const QString text = AiToolRegistryHelper::serverCatalogText(serverId, result.value().value(prompts ? QStringLiteral("prompts") : QStringLiteral("resources")).toArray(), prompts);
                if (!text.isEmpty()) {
                    sections->append(text);
                }
            }
            if (--*remaining > 0) {
                return;
            }
            completion({call.id, sections->isEmpty() ? QString{} : AiToolRegistryHelper::boundedText(sections->join(QLatin1Char('\n'))), false});
        };
        // clang-format on
        if (prompts) {
            client->listPrompts({}, collect);
            continue;
        }
        client->listResources({}, collect);
    }
}

agent::mcp::McpClient* AiToolRegistry::readyServer(const QString& serverId) const {
    for (const auto& client : m_mcpClients) {
        if (!client.isNull() && client->ready() && client->serverId() == serverId) {
            return client.data();
        }
    }

    return nullptr;
}

void AiToolRegistry::readServerResource(const ToolCall& call, const ToolCompletion& completion) {
    agent::mcp::McpClient* client = readyServer(call.arguments.value(QStringLiteral("server")).toString());
    const QString uri = call.arguments.value(QStringLiteral("uri")).toString();

    if (client == nullptr || uri.isEmpty()) {
        completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.mcp-unavailable")).arg(call.name)));
        return;
    }

    // clang-format off
    const auto reply = [call, completion](Result<QJsonObject> result) {
        if (!result.hasValue()) {
            completion({call.id, result.error().message, true});
            return;
        }
        QStringList sections;
        for (const auto& value : result.value().value(QStringLiteral("contents")).toArray()) {
            sections.append(value.toObject().value(QStringLiteral("text")).toString());
        }
        completion({call.id, AiToolRegistryHelper::boundedText(sections.join(QStringLiteral("\n"))), false});
    };
    // clang-format on
    client->readResource(uri, reply);
}

void AiToolRegistry::readServerPrompt(const ToolCall& call, const ToolCompletion& completion) {
    agent::mcp::McpClient* client = readyServer(call.arguments.value(QStringLiteral("server")).toString());
    const QString name = call.arguments.value(QStringLiteral("name")).toString();

    if (client == nullptr || name.isEmpty()) {
        completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.mcp-unavailable")).arg(call.name)));
        return;
    }

    // clang-format off
    const auto reply = [call, completion](Result<QJsonObject> result) {
        if (!result.hasValue()) {
            completion({call.id, result.error().message, true});
            return;
        }
        QStringList sections;
        for (const auto& value : result.value().value(QStringLiteral("messages")).toArray()) {
            sections.append(value.toObject().value(QStringLiteral("content")).toObject().value(QStringLiteral("text")).toString());
        }
        completion({call.id, AiToolRegistryHelper::boundedText(sections.join(QStringLiteral("\n"))), false});
    };
    // clang-format on
    client->getPrompt(name, call.arguments.value(QStringLiteral("arguments")).toObject(), reply);
}

void AiToolRegistry::callMcpTool(const ToolCall& call, const ToolCompletion& completion) {
    const auto entry = m_mcpTools.value(call.name);

    if (entry.first.isNull() || !entry.first->ready()) {
        completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.mcp-unavailable")).arg(call.name)));
        return;
    }

    // clang-format off
    const auto reply = [call, completion](Result<QJsonObject> result) { if (!result.hasValue()) { completion({call.id, result.error().message, true}); return; } completion(AiToolRegistryHelper::mcpToolResult(call, result.value())); };
    // clang-format on
    entry.first->callTool(entry.second, call.arguments, reply);
}

void AiToolRegistry::readFile(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion) {
    const auto resolved = resolveSandboxPath(sandboxRoot, call.arguments.value(QStringLiteral("path")).toString());

    if (!resolved.hasValue()) {
        completion(AiToolRegistryHelper::failure(call, resolved.error().message));
        return;
    }

    const int firstLine = call.arguments.value(QStringLiteral("start_line")).toInt(0);
    const int lastLine = call.arguments.value(QStringLiteral("end_line")).toInt(0);

    if (firstLine < 0 || lastLine < 0 || (firstLine > 0 && lastLine > 0 && lastLine < firstLine)) {
        completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-argument-value")).arg(call.name, QStringLiteral("start_line"))));
        return;
    }

    auto future = m_host.readFile(resolved.value(), maximumReadBytes);
    // clang-format off
    const QString notText = m_host.translate(QStringLiteral("ai.error.tool-not-text")).arg(resolved.value());
    const auto answerRead = [call, completion, firstLine, lastLine, notText](Result<QByteArray> result) {
        if (!result.hasValue()) {
            completion(AiToolRegistryHelper::failure(call, result.error().message));
            return;
        }
        const auto content = AiToolRegistryHelper::decodedText(result.value());
        if (!content.has_value()) {
            completion(AiToolRegistryHelper::failure(call, notText));
            return;
        }
        completion({call.id, AiToolRegistryHelper::boundedText(AiToolRegistryHelper::selectedLines(content.value(), firstLine, lastLine)), false});
    };
    future.then(this, answerRead);
    // clang-format on
}

void AiToolRegistry::createDirectory(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion) {
    const auto resolved = resolveSandboxPath(sandboxRoot, call.arguments.value(QStringLiteral("path")).toString());

    if (!resolved.hasValue()) {
        completion(AiToolRegistryHelper::failure(call, resolved.error().message));
        return;
    }

    auto future = m_host.createDirectory(resolved.value());
    // clang-format off
    future.then(this, [call, completion](Result<void> result) { completion(result.hasValue() ? ToolResult{call.id, QStringLiteral("created"), false} : AiToolRegistryHelper::failure(call, result.error().message)); });
    // clang-format on
}

void AiToolRegistry::movePath(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion) {
    const auto source = resolveSandboxPath(sandboxRoot, call.arguments.value(QStringLiteral("source")).toString());
    const auto destination = resolveSandboxPath(sandboxRoot, call.arguments.value(QStringLiteral("destination")).toString());

    if (!source.hasValue() || !destination.hasValue()) {
        completion(AiToolRegistryHelper::failure(call, source.hasValue() ? destination.error().message : source.error().message));
        return;
    }

    auto future = m_host.movePath(source.value(), destination.value());
    // clang-format off
    future.then(this, [call, completion](Result<void> result) { completion(result.hasValue() ? ToolResult{call.id, QStringLiteral("moved"), false} : AiToolRegistryHelper::failure(call, result.error().message)); });
    // clang-format on
}

void AiToolRegistry::copyFile(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion) {
    const auto source = resolveSandboxPath(sandboxRoot, call.arguments.value(QStringLiteral("source")).toString());
    const auto destination = resolveSandboxPath(sandboxRoot, call.arguments.value(QStringLiteral("destination")).toString());

    if (!source.hasValue() || !destination.hasValue()) {
        completion(AiToolRegistryHelper::failure(call, source.hasValue() ? destination.error().message : source.error().message));
        return;
    }

    auto future = m_host.copyFile(source.value(), destination.value());
    // clang-format off
    future.then(this, [call, completion](Result<void> result) { completion(result.hasValue() ? ToolResult{call.id, QStringLiteral("copied"), false} : AiToolRegistryHelper::failure(call, result.error().message)); });
    // clang-format on
}

void AiToolRegistry::removePath(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion) {
    const auto resolved = resolveSandboxPath(sandboxRoot, call.arguments.value(QStringLiteral("path")).toString());

    if (!resolved.hasValue()) {
        completion(AiToolRegistryHelper::failure(call, resolved.error().message));
        return;
    }

    const QFileInfo information(resolved.value());

    if (!information.exists()) {
        completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-path-missing")).arg(call.arguments.value(QStringLiteral("path")).toString())));
        return;
    }

    // Removing a directory takes everything inside it, so an agent that did not ask for that is told instead of losing the contents.
    const bool recursive = call.arguments.value(QStringLiteral("recursive")).toBool();

    if (information.isDir() && !recursive && !QDir(resolved.value()).isEmpty()) {
        completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-remove-not-empty")).arg(call.arguments.value(QStringLiteral("path")).toString())));
        return;
    }

    auto future = information.isDir() ? m_host.removeDirectory(resolved.value()) : m_host.removeFile(resolved.value());
    // clang-format off
    future.then(this, [call, completion](Result<void> result) { completion(result.hasValue() ? ToolResult{call.id, QStringLiteral("removed"), false} : AiToolRegistryHelper::failure(call, result.error().message)); });
    // clang-format on
}

void AiToolRegistry::describePath(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion) const {
    const auto resolved = resolveSandboxPath(sandboxRoot, call.arguments.value(QStringLiteral("path")).toString());

    if (!resolved.hasValue()) {
        completion(AiToolRegistryHelper::failure(call, resolved.error().message));
        return;
    }

    const QFileInfo information(resolved.value());

    if (!information.exists()) {
        completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-path-missing")).arg(call.arguments.value(QStringLiteral("path")).toString())));
        return;
    }

    const QJsonObject described{{QStringLiteral("type"), information.isDir() ? QStringLiteral("directory") : QStringLiteral("file")}, {QStringLiteral("byteSize"), information.size()}, {QStringLiteral("modifiedAtUtc"), persistence::StoredValues::storedTimestamp(information.lastModified(QTimeZone::UTC))}, {QStringLiteral("readable"), information.isReadable()}, {QStringLiteral("writable"), information.isWritable()}};
    completion({call.id, QString::fromUtf8(QJsonDocument(described).toJson(QJsonDocument::Indented)), false});
}

// A search stays inside the working directory and stops at its bounded result count.
void AiToolRegistry::searchFiles(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion) {
    const QString pattern = call.arguments.value(QStringLiteral("pattern")).toString().trimmed();
    const auto resolved = resolveSandboxPath(sandboxRoot, call.arguments.value(QStringLiteral("path")).toString());

    if (pattern.isEmpty() || !resolved.hasValue()) {
        completion(AiToolRegistryHelper::failure(call, resolved.hasValue() ? m_host.translate(QStringLiteral("ai.error.tool-argument-value")).arg(call.name, QStringLiteral("pattern")) : resolved.error().message));
        return;
    }

    const QString contains = call.arguments.value(QStringLiteral("contains")).toString();
    // A search walks a tree of a size the workspace decides and reads every file it opens, so it never runs on the thread that draws.
    // clang-format off
    auto found = QtConcurrent::run([root = resolved.value(), pattern, contains]() {
        const QDir base(root);
        QStringList matches;
        QDirIterator iterator(root, QStringList{pattern}, QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext() && matches.size() < maximumListedEntries) {
            const QString path = iterator.next();
            if (!contains.isEmpty()) {
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly) || !QString::fromUtf8(file.read(maximumReadBytes)).contains(contains)) {
                    continue;
                }
            }
            matches.append(base.relativeFilePath(path));
        }
        matches.sort();
        return matches;
    });
    const QString empty = m_host.translate(QStringLiteral("ai.error.tool-no-match"));
    found.then(this, [call, completion, empty](const QStringList& matches) { completion({call.id, matches.isEmpty() ? empty : AiToolRegistryHelper::boundedText(matches.join(QLatin1Char('\n'))), false}); });
    // clang-format on
}

// A command the agent runs is bound to the same working directory its file tools are bound to.
void AiToolRegistry::runCommand(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion) {
    const QString command = call.arguments.value(QStringLiteral("command")).toString().trimmed();

    if (command.isEmpty() || sandboxRoot.isEmpty()) {
        completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-sandbox-missing"))));
        return;
    }

    const int timeoutSeconds = call.arguments.value(QStringLiteral("timeout_seconds")).toInt(defaultCommandTimeoutSeconds);

    if (timeoutSeconds < 0) {
        completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-argument-value")).arg(call.name, QStringLiteral("timeout_seconds"))));
        return;
    }

    auto* runner = new AiCommandRunner(this);
    m_runningCommands.insert(call.id, {runner, completion});
    // clang-format off
    connect(runner, &AiCommandRunner::finished, this, [this, call, completion, runner](int exitCode, const QString& output) { m_runningCommands.remove(call.id); runner->deleteLater(); const QString reported = output.isEmpty() ? m_host.translate(QStringLiteral("ai.tool.command-no-output")) : output; completion(exitCode == 0 ? ToolResult{call.id, AiToolRegistryHelper::boundedText(reported), false} : ToolResult{call.id, AiToolRegistryHelper::boundedText(m_host.translate(QStringLiteral("ai.error.exit-code")).arg(QString::number(exitCode)) + QLatin1Char('\n') + reported), true}); });
    const auto translate = [this](const QString& key) { return m_host.translate(key); };
    connect(runner, &AiCommandRunner::failed, this, [this, call, completion, runner, translate](const Error& error) { m_runningCommands.remove(call.id); runner->deleteLater(); completion(AiToolRegistryHelper::failure(call, CommandOutput::commandFailureMessage(error, translate))); });
    // clang-format on
    runner->start(command, sandboxRoot, timeoutSeconds);
}

void AiToolRegistry::writeFile(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion) {
    const auto resolved = resolveSandboxPath(sandboxRoot, call.arguments.value(QStringLiteral("path")).toString());

    if (!resolved.hasValue()) {
        completion(AiToolRegistryHelper::failure(call, resolved.error().message));
        return;
    }

    auto future = m_host.writeFile(resolved.value(), call.arguments.value(QStringLiteral("content")).toString().toUtf8());
    const QString path = resolved.value();
    // clang-format off
    future.then(this, [call, completion, path](Result<void> result) { completion(result.hasValue() ? ToolResult{call.id, path, false} : AiToolRegistryHelper::failure(call, result.error().message)); });
    // clang-format on
}

// A whole rewrite spends the output budget of the model and risks losing what it did not mean to touch, so an edit names the passage it replaces.
void AiToolRegistry::editFile(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion) {
    const auto resolved = resolveSandboxPath(sandboxRoot, call.arguments.value(QStringLiteral("path")).toString());

    if (!resolved.hasValue()) {
        completion(AiToolRegistryHelper::failure(call, resolved.error().message));
        return;
    }

    if (call.arguments.value(QStringLiteral("old_text")).toString().isEmpty()) {
        completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-argument-value")).arg(call.name, QStringLiteral("old_text"))));
        return;
    }

    const QString path = resolved.value();
    const QString oldText = call.arguments.value(QStringLiteral("old_text")).toString();
    const QString newText = call.arguments.value(QStringLiteral("new_text")).toString();
    const bool replaceAll = call.arguments.value(QStringLiteral("replace_all")).toBool();
    auto future = m_host.readFile(path, maximumReadBytes);
    // clang-format off
    const auto applyEdit = [this, call, completion, path, oldText, newText, replaceAll](Result<QByteArray> result) {
        if (!result.hasValue()) {
            completion(AiToolRegistryHelper::failure(call, result.error().message));
            return;
        }

        const auto decoded = AiToolRegistryHelper::decodedText(result.value());
        if (!decoded.has_value()) {
            completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-not-text")).arg(path)));
            return;
        }

        const QString content = decoded.value();
        const qsizetype occurrences = content.count(oldText);
        if (occurrences == 0) {
            completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-edit-absent")).arg(path)));
            return;
        }
        if (occurrences > 1 && !replaceAll) {
            completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-edit-ambiguous")).arg(QString::number(occurrences))));
            return;
        }

        QString edited = content;
        if (replaceAll) {
            edited.replace(oldText, newText);
        } else {
            edited.replace(content.indexOf(oldText), oldText.size(), newText);
        }
        auto written = m_host.writeFile(path, edited.toUtf8());
        written.then(this, [call, completion, path, occurrences, replaceAll](Result<void> saved) { completion(saved.hasValue() ? ToolResult{call.id, QStringLiteral("%1 %2").arg(path, QString::number(replaceAll ? occurrences : 1)), false} : AiToolRegistryHelper::failure(call, saved.error().message)); });
    };
    future.then(this, applyEdit);
    // clang-format on
}

// A model that does not declare image input cannot be handed one, so the tool says which model refused instead of sending bytes it will reject.
void AiToolRegistry::readImage(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion) {
    const ProviderDescriptor* provider = ProviderCatalog::findProvider(m_taskConnection.providerId);

    if (provider == nullptr || !ProviderCatalog::modelTraits(*provider, m_taskConnection.modelId).contains(ModelTrait::Vision)) {
        completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-image-unsupported")).arg(m_taskConnection.modelId)));
        return;
    }

    const auto resolved = resolveSandboxPath(sandboxRoot, call.arguments.value(QStringLiteral("path")).toString());

    if (!resolved.hasValue()) {
        completion(AiToolRegistryHelper::failure(call, resolved.error().message));
        return;
    }

    const QByteArray mediaType = AiToolRegistryHelper::imageMediaType(resolved.value());

    if (mediaType.isEmpty()) {
        completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-image-format")).arg(resolved.value())));
        return;
    }

    const QString path = resolved.value();
    auto future = m_host.readFile(path, maximumViewedImageBytes + 1);
    // clang-format off
    future.then(this, [this, call, completion, path, mediaType](Result<QByteArray> result) {
        if (!result.hasValue()) {
            completion(AiToolRegistryHelper::failure(call, result.error().message));
            return;
        }
        if (result.value().isEmpty() || result.value().size() > maximumViewedImageBytes) {
            completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-image-size")).arg(path)));
            return;
        }
        completion({call.id, path, false, result.value(), mediaType});
    });
    // clang-format on
}

void AiToolRegistry::listDirectory(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion) {
    const auto resolved = resolveSandboxPath(sandboxRoot, call.arguments.value(QStringLiteral("path")).toString());

    if (!resolved.hasValue()) {
        completion(AiToolRegistryHelper::failure(call, resolved.error().message));
        return;
    }

    // What a directory holds is decided by whoever filled it, so it is listed away from the thread that draws.
    auto future = m_host.listDirectory(resolved.value(), maximumListedEntries);
    // clang-format off
    future.then(this, [call, completion](Result<QVector<filesystem::DirectoryEntry>> listed) {
        if (!listed.hasValue()) {
            completion(AiToolRegistryHelper::failure(call, listed.error().message));
            return;
        }
        QStringList entries;
        for (const auto& entry : listed.value()) {
            entries.append(entry.directory ? entry.name + QLatin1Char('/') : entry.name);
        }
        completion({call.id, AiToolRegistryHelper::boundedText(entries.join(QLatin1Char('\n'))), false});
    });
    // clang-format on
}

void AiToolRegistry::fetchUrl(const ToolCall& call, const ToolCompletion& completion) {
    const QUrl url(call.arguments.value(QStringLiteral("url")).toString());

    if (!url.isValid() || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https")) || url.host().isEmpty()) {
        completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-url")).arg(call.arguments.value(QStringLiteral("url")).toString())));
        return;
    }

    QNetworkRequest request(url);
    request.setTransferTimeout(fetchTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_network.get(request);
    auto answer = agent::BoundedReplies::boundReply(reply, maximumFetchBytes);
    // clang-format off
    connect(reply, &QNetworkReply::finished, this, [this, call, completion, reply, answer]() {
        reply->deleteLater();
        answer->bytes.append(reply->read(maximumFetchBytes - answer->bytes.size()));
        const QByteArray payload = answer->bytes;
        if (!answer->truncated && reply->error() != QNetworkReply::NoError) {
            completion(AiToolRegistryHelper::failure(call, AiToolRegistryHelper::serviceErrorMessage(payload, reply->errorString())));
            return;
        }
        auto decoded = QtConcurrent::run([payload]() { return AiToolRegistryHelper::boundedText(AiToolRegistryHelper::readableText(payload)); });
        decoded.then(this, [call, completion](const QString& text) { completion({call.id, text, false}); });
    });
    // clang-format on
}

void AiToolRegistry::setMediaConfiguration(const ModelConnection& connection, const QString& address) {
    m_mediaAddress = address;
    m_media = connection;
}

// A generated file lands exactly where the agent asked for it, inside the working directory the task declares.
Result<void> AiToolRegistry::writeGeneratedFile(const QString& path, const QByteArray& content) const {
    const QDir parent = QFileInfo(path).dir();

    if (!parent.exists() && !parent.mkpath(QStringLiteral("."))) {
        return Result<void>::failure({"ai_tool_write_failed", "The destination directory is unavailable", parent.path()});
    }

    QFile file(path);

    if (!file.open(QIODevice::WriteOnly)) {
        return Result<void>::failure({"ai_tool_write_failed", "The generated file could not be written", path});
    }

    file.write(content);
    return Result<void>::success();
}

// Every supported search service answers with its own shape, so each one is read by the contract it publishes.
void AiToolRegistry::searchWeb(const ToolCall& call, const ToolCompletion& completion) {
    const QString query = call.arguments.value(QStringLiteral("query")).toString().trimmed();
    const QString address = m_searchAddress;

    if (query.isEmpty() || address.isEmpty()) {
        completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-search-unconfigured"))));
        return;
    }

    const auto apiKey = Secrets::resolveSecret(m_search.apiKey);

    if (!apiKey.hasValue()) {
        completion(AiToolRegistryHelper::failure(call, apiKey.error().message));
        return;
    }

    const int requested = call.arguments.value(QStringLiteral("count")).toInt(defaultSearchResults);
    const int count = std::clamp(requested, 1, maximumSearchResults);

    QNetworkRequest request;
    request.setTransferTimeout(searchTimeoutMs);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply* reply = nullptr;

    if (m_search.provider == SearchProvider::Tavily) {
        QUrl endpoint(address);
        endpoint.setPath(endpoint.path() + QStringLiteral("/search"));
        request.setUrl(endpoint);
        request.setRawHeader(QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + apiKey.value().toUtf8());
        const QJsonObject body{{QStringLiteral("query"), query}, {QStringLiteral("max_results"), count}};
        reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    } else {
        QUrl endpoint(address);
        endpoint.setPath(endpoint.path() + (m_search.provider == SearchProvider::Brave ? QStringLiteral("/res/v1/web/search") : QStringLiteral("/search")));
        QUrlQuery parameters{{QStringLiteral("q"), query}};
        if (m_search.provider == SearchProvider::Brave) {
            parameters.addQueryItem(QStringLiteral("count"), QString::number(count));
            request.setRawHeader(QByteArrayLiteral("X-Subscription-Token"), apiKey.value().toUtf8());
            request.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));
        } else {
            parameters.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
        }
        endpoint.setQuery(parameters);
        request.setUrl(endpoint);
        reply = m_network.get(request);
    }

    auto answer = agent::BoundedReplies::boundReply(reply, maximumFetchBytes);
    // clang-format off
    connect(reply, &QNetworkReply::finished, this, [this, call, completion, reply, count, answer]() {
        reply->deleteLater();
        answer->bytes.append(reply->read(maximumFetchBytes - answer->bytes.size()));
        const QByteArray payload = answer->bytes;
        if (!answer->truncated && reply->error() != QNetworkReply::NoError) {
            completion(AiToolRegistryHelper::failure(call, AiToolRegistryHelper::serviceErrorMessage(payload, reply->errorString())));
            return;
        }
        auto decoded = QtConcurrent::run([payload, provider = m_search.provider, count, empty = m_host.translate(QStringLiteral("ai.search.no-result"))]() { return AiToolRegistryHelper::boundedText(AiToolRegistryHelper::formatSearchResults(provider, QJsonDocument::fromJson(payload).object(), count, empty)); });
        decoded.then(this, [call, completion](const QString& text) { completion({call.id, text, false}); });
    });
    // clang-format on
}

void AiToolRegistry::generateImage(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion) {
    const QString prompt = call.arguments.value(QStringLiteral("prompt")).toString();

    if (prompt.trimmed().isEmpty()) {
        completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-image-unconfigured"))));
        return;
    }

    const auto destination = resolveSandboxPath(sandboxRoot, call.arguments.value(QStringLiteral("path")).toString());

    if (!destination.hasValue()) {
        completion(AiToolRegistryHelper::failure(call, destination.error().message));
        return;
    }

    const auto apiKey = Secrets::resolveSecret(m_media.apiKey);

    if (!apiKey.hasValue()) {
        completion(AiToolRegistryHelper::failure(call, apiKey.error().message));
        return;
    }

    const ProviderDescriptor* provider = ProviderCatalog::findProvider(m_media.providerId);
    const auto image = ModelConnections::resolveEndpoint(m_media.providerId, m_mediaAddress, ModelEndpoint::Image);

    if (provider == nullptr || !image.has_value()) {
        completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.endpoint-unavailable"))));
        return;
    }

    const EndpointDescriptor& descriptor = provider->endpoints.value(ModelEndpoint::Image);
    QNetworkRequest request{QUrl(image.value().url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(imageTimeoutMs);

    if (!apiKey.value().isEmpty()) {
        request.setRawHeader(descriptor.authHeader.toUtf8(), descriptor.authPrefix.toUtf8() + apiKey.value().toUtf8());
    }

    QJsonObject body = descriptor.body;
    body.insert(QStringLiteral("model"), descriptor.model.isEmpty() ? m_media.modelId : descriptor.model);
    body.insert(descriptor.textField, prompt);
    const QString size = call.arguments.value(QStringLiteral("size")).toString();

    if (!size.isEmpty()) {
        body.insert(QStringLiteral("size"), size);
    }

    QNetworkReply* reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    auto answer = agent::BoundedReplies::boundReply(reply, maximumImageBytes);
    // clang-format off
    connect(reply, &QNetworkReply::finished, this, [this, call, completion, reply, answer, path = destination.value()]() {
        reply->deleteLater();
        answer->bytes.append(reply->read(maximumImageBytes - answer->bytes.size()));
        const QByteArray payload = answer->bytes;
        // A file the service cut short is not the file the agent asked for, so it is refused rather than written broken.
        if (answer->truncated) {
            completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-answer-truncated"))));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            const QJsonObject error = QJsonDocument::fromJson(payload).object().value(QStringLiteral("error")).toObject();
            completion(AiToolRegistryHelper::failure(call, error.value(QStringLiteral("message")).toString(reply->errorString())));
            return;
        }

        auto decoded = QtConcurrent::run([payload]() { const QJsonArray data = QJsonDocument::fromJson(payload).object().value(QStringLiteral("data")).toArray(); return data.isEmpty() ? QByteArray{} : QByteArray::fromBase64(data.first().toObject().value(QStringLiteral("b64_json")).toString().toUtf8()); });
        decoded.then(this, [this, call, completion, path](const QByteArray& picture) {
            if (picture.isEmpty()) {
                completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-image-empty"))));
                return;
            }
            const auto stored = writeGeneratedFile(path, picture);
            completion(stored.hasValue() ? ToolResult{call.id, path, false} : AiToolRegistryHelper::failure(call, stored.error().message));
        });
    });
    // clang-format on
}

void AiToolRegistry::setSearchConfiguration(const SearchSettings& settings, const QString& address) {
    m_search = settings;
    m_searchAddress = address;
}

void AiToolRegistry::setSpeechConfiguration(const SpeechSettings& settings, const QString& address) {
    m_speechAddress = address;
    m_speech = settings;
}

// Each speech service authenticates with its own header and answers audio bytes rather than a JSON envelope.
void AiToolRegistry::generateSpeech(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion) {
    const QString text = call.arguments.value(QStringLiteral("text")).toString();
    const QString voice = call.arguments.value(QStringLiteral("voice")).toString().trimmed().isEmpty() ? m_speech.voiceId : call.arguments.value(QStringLiteral("voice")).toString().trimmed();
    const EndpointDescriptor* descriptor = TaskContracts::speechEndpoint(m_speech.providerId);
    const auto resolved = ModelConnections::resolveEndpoint(m_speech.providerId, m_speechAddress, ModelEndpoint::Speech);

    if (text.trimmed().isEmpty() || descriptor == nullptr || !resolved.has_value() || voice.isEmpty()) {
        completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-speech-unconfigured"))));
        return;
    }

    const auto destination = resolveSandboxPath(sandboxRoot, call.arguments.value(QStringLiteral("path")).toString());

    if (!destination.hasValue()) {
        completion(AiToolRegistryHelper::failure(call, destination.error().message));
        return;
    }

    const auto apiKey = Secrets::resolveSecret(m_speech.apiKey);

    if (!apiKey.hasValue()) {
        completion(AiToolRegistryHelper::failure(call, apiKey.error().message));
        return;
    }

    QNetworkRequest request{QUrl(QString(resolved->url).replace(QString::fromLatin1(endpointVoicePlaceholder), voice))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(imageTimeoutMs);
    request.setRawHeader(descriptor->authHeader.toUtf8(), descriptor->authPrefix.toUtf8() + apiKey.value().toUtf8());

    QJsonObject body = descriptor->body;
    body.insert(descriptor->textField, text);

    if (!descriptor->voiceField.isEmpty()) {
        body.insert(descriptor->voiceField, voice);
    }
    if (!descriptor->model.isEmpty()) {
        body.insert(QStringLiteral("model"), descriptor->model);
    }
    QNetworkReply* reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    auto answer = agent::BoundedReplies::boundReply(reply, maximumImageBytes);
    // clang-format off
    connect(reply, &QNetworkReply::finished, this, [this, call, completion, reply, answer, path = destination.value()]() {
        reply->deleteLater();
        answer->bytes.append(reply->read(maximumImageBytes - answer->bytes.size()));
        const QByteArray payload = answer->bytes;
        // A file the service cut short is not the file the agent asked for, so it is refused rather than written broken.
        if (answer->truncated) {
            completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-answer-truncated"))));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            completion(AiToolRegistryHelper::failure(call, AiToolRegistryHelper::serviceErrorMessage(payload, reply->errorString())));
            return;
        }
        if (payload.isEmpty()) {
            completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-speech-empty"))));
            return;
        }

        const auto stored = writeGeneratedFile(path, payload);
        completion(stored.hasValue() ? ToolResult{call.id, path, false} : AiToolRegistryHelper::failure(call, stored.error().message));
    });
    // clang-format on
}

// A service with a closed voice set answers from its own declaration, and an account catalog is read from the service.
void AiToolRegistry::listVoices(const ToolCall& call, const ToolCompletion& completion) {
    const EndpointDescriptor* descriptor = TaskContracts::speechEndpoint(m_speech.providerId);

    if (descriptor == nullptr) {
        completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-speech-unconfigured"))));
        return;
    }
    if (!descriptor->voices.isEmpty()) {
        completion({call.id, descriptor->voices.join(QLatin1Char('\n')), false});
        return;
    }

    const ProviderDescriptor* provider = ProviderCatalog::findProvider(m_speech.providerId);
    const QString base = m_speechAddress.isEmpty() ? (provider == nullptr ? QString{} : provider->baseUrl) : m_speechAddress;

    if (base.isEmpty()) {
        completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-speech-unconfigured"))));
        return;
    }

    const auto apiKey = Secrets::resolveSecret(m_speech.apiKey);

    if (!apiKey.hasValue()) {
        completion(AiToolRegistryHelper::failure(call, apiKey.error().message));
        return;
    }

    QNetworkRequest request{QUrl(base + descriptor->voiceCatalogPath)};
    request.setTransferTimeout(searchTimeoutMs);
    request.setRawHeader(descriptor->authHeader.toUtf8(), descriptor->authPrefix.toUtf8() + apiKey.value().toUtf8());

    QNetworkReply* reply = m_network.get(request);
    auto answer = agent::BoundedReplies::boundReply(reply, maximumFetchBytes);
    // clang-format off
    connect(reply, &QNetworkReply::finished, this, [this, call, completion, reply, answer]() {
        reply->deleteLater();
        answer->bytes.append(reply->read(maximumFetchBytes - answer->bytes.size()));
        const QByteArray payload = answer->bytes;
        if (!answer->truncated && reply->error() != QNetworkReply::NoError) {
            completion(AiToolRegistryHelper::failure(call, AiToolRegistryHelper::serviceErrorMessage(payload, reply->errorString())));
            return;
        }

        auto parsed = QtConcurrent::run([payload]() {
            QStringList entries;
            for (const auto& value : QJsonDocument::fromJson(payload).object().value(QStringLiteral("voices")).toArray()) {
                const QJsonObject entry = value.toObject();
                entries.append(QStringLiteral("%1 | %2 | %3").arg(entry.value(QStringLiteral("voice_id")).toString(), entry.value(QStringLiteral("name")).toString(), entry.value(QStringLiteral("category")).toString()));
            }
            return entries;
        });
        parsed.then(this, [this, call, completion](const QStringList& voices) {
            if (voices.isEmpty()) {
                completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-voices-empty"))));
                return;
            }
            completion({call.id, voices.join(QLatin1Char('\n')), false});
        });
    });
    // clang-format on
}

void AiToolRegistry::listSkills(const QString& sandboxRoot, const ToolCompletion& completion, const ToolCall& call) {
    // clang-format off
    m_skills->discover(sandboxRoot, [this, completion, call](const QVector<agent::ResourceDescriptor>& catalog) {
        QStringList lines;
        for (const auto& skill : agent::AgentResourceCatalog::ofKind(catalog, agent::ResourceKind::Skill)) {
            lines.append(QStringLiteral("%1: %2 (%3)").arg(skill.name, skill.description, skill.root));
        }
        completion({call.id, lines.isEmpty() ? m_host.translate(QStringLiteral("ai.skill.none")) : lines.join(QLatin1Char('\n')), false});
    });
    // clang-format on
}

void AiToolRegistry::searchSkills(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion) {
    const QString query = call.arguments.value(QStringLiteral("query")).toString().trimmed();

    if (query.isEmpty()) {
        completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-argument-value")).arg(call.name, QStringLiteral("query"))));
        return;
    }

    // clang-format off
    m_skills->discover(sandboxRoot, [this, completion, call, query](const QVector<agent::ResourceDescriptor>& catalog) {
        QStringList lines;
        for (const auto& skill : agent::AgentResourceCatalog::ofKind(catalog, agent::ResourceKind::Skill)) {
            if (skill.name.contains(query, Qt::CaseInsensitive) || skill.description.contains(query, Qt::CaseInsensitive)) {
                lines.append(QStringLiteral("%1: %2").arg(skill.name, skill.description));
            }
        }
        completion({call.id, lines.isEmpty() ? m_host.translate(QStringLiteral("ai.skill.no-match")).arg(query) : lines.join(QLatin1Char('\n')), false});
    });
    // clang-format on
}

void AiToolRegistry::readSkill(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion) {
    const QString name = call.arguments.value(QStringLiteral("name")).toString().trimmed();
    // clang-format off
    m_skills->discover(sandboxRoot, [this, completion, call, name](const QVector<agent::ResourceDescriptor>& catalog) {
        const agent::ResourceDescriptor* skill = AiToolRegistryHelper::findSkill(catalog, name);
        if (skill == nullptr) {
            completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.skill.unknown")).arg(name)));
            return;
        }
        auto future = m_host.readFile(skill->path, maximumSkillBytes);
        future.then(this, [this, completion, call, name](Result<QByteArray> content) {
            if (!content.hasValue()) {
                completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-path-missing")).arg(name)));
                return;
            }
            completion({call.id, AiToolRegistryHelper::boundedText(QString::fromUtf8(content.value())), false});
        });
    });
    // clang-format on
}

// A skill ships its reference, its README and its scripts beside the instructions, and the agent reads them from inside that bundle and nowhere else.
void AiToolRegistry::readSkillFile(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion) {
    const QString name = call.arguments.value(QStringLiteral("name")).toString().trimmed();
    const QString relative = call.arguments.value(QStringLiteral("path")).toString().trimmed();

    if (relative.isEmpty()) {
        completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.error.tool-argument-value")).arg(call.name, QStringLiteral("path"))));
        return;
    }

    // clang-format off
    m_skills->discover(sandboxRoot, [this, completion, call, name, relative](const QVector<agent::ResourceDescriptor>& catalog) {
        const agent::ResourceDescriptor* skill = AiToolRegistryHelper::findSkill(catalog, name);
        if (skill == nullptr) {
            completion(AiToolRegistryHelper::failure(call, m_host.translate(QStringLiteral("ai.skill.unknown")).arg(name)));
            return;
        }
        const QString bundle = QFileInfo(skill->path).absolutePath();
        const auto resolved = resolveSandboxPath(bundle, relative);
        if (!resolved.hasValue()) {
            completion(AiToolRegistryHelper::failure(call, resolved.error().message));
            return;
        }
        auto future = m_host.readFile(resolved.value(), maximumSkillBytes);
        future.then(this, [completion, call](Result<QByteArray> content) {
            if (!content.hasValue()) {
                completion(AiToolRegistryHelper::failure(call, content.error().message));
                return;
            }
            completion({call.id, AiToolRegistryHelper::boundedText(QString::fromUtf8(content.value())), false});
        });
    });
    // clang-format on
}

void AiToolRegistry::setTaskContext(const AiTask& task, const ModelConnection& connection) {
    m_task = task;
    m_taskConnection = connection;
}

void AiToolRegistry::describeTask(const ToolCall& call, const ToolCompletion& completion) const {
    QJsonObject described{{QStringLiteral("title"), m_task.title}, {QStringLiteral("description"), m_task.description}, {QStringLiteral("prompt"), m_task.prompt}, {QStringLiteral("column"), AiTaskRepository::columnName(m_task.column)}, {QStringLiteral("workingDirectory"), m_task.workdir}};

    if (!m_task.issueUrl.isEmpty()) {
        described.insert(QStringLiteral("issueUrl"), m_task.issueUrl);
    }

    if (m_task.schedule.has_value()) {
        described.insert(QStringLiteral("scheduled"), true);
    }

    completion({call.id, QString::fromUtf8(QJsonDocument(described).toJson(QJsonDocument::Indented)), false});
}

} // namespace workpane::plugins::ai
